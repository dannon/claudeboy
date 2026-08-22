import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { transformOpenUsage } from './transform.ts';

export type PollOutcome = 'pushed' | 'unchanged' | 'source-unavailable' | 'push-failed';

export interface AgentConfig {
  usageUrl: string;
  pushUrl: string;
  pushToken: string;
  fetchImpl?: typeof fetch;
  log?: (message: string) => void;
}

export interface AgentState {
  lastPushedJson: string | null;
  /** Consecutive unchanged polls, for the heartbeat. Callers may omit it. */
  quietPolls?: number;
}

// Neither fetch has a natural deadline. An OpenUsage that accepts the connection
// and then never answers would otherwise hold the loop until undici's 300s
// headersTimeout -- five poll intervals of silence with no log line to explain
// them. Ten seconds is generous for a localhost read and for a Worker PUT.
const FETCH_TIMEOUT_MS = 10_000;

const DEFAULT_INTERVAL_SEC = 60;

// A floor, not a recommendation. This exists so a mangled env var cannot turn
// the loop into a spin against localhost:6736 -- it is deliberately far below
// the 60s default, because refusing a value the operator meant is worse than
// polling a bit eagerly. OpenUsage only refreshes every five minutes or so, so
// anything under 60 is already buying nothing.
const MIN_INTERVAL_SEC = 5;

// ~30 minutes at the 60s default.
const QUIET_HEARTBEAT_POLLS = 30;

/**
 * Poll interval from the environment, in milliseconds.
 *
 * Number('') is 0 and Number('nonsense') is NaN, and setTimeout treats both as
 * zero -- so a typo in the plist turns the poll loop into a spin against
 * OpenUsage and the Worker. Anything unusable falls back to the default, loudly:
 * a silent fallback is how you end up debugging the wrong thing.
 */
export function intervalMsFromEnv(
  raw: string | undefined,
  log: (message: string) => void,
): number {
  if (raw === undefined) return DEFAULT_INTERVAL_SEC * 1000;
  const sec = Number(raw);
  if (!Number.isFinite(sec) || sec < MIN_INTERVAL_SEC) {
    log(
      `CLAUDEBOY_INTERVAL_SEC=${JSON.stringify(raw)} is not a usable interval ` +
        `(want a number of at least ${MIN_INTERVAL_SEC}); falling back to ${DEFAULT_INTERVAL_SEC}s`,
    );
    return DEFAULT_INTERVAL_SEC * 1000;
  }
  return Math.round(sec * 1000);
}

/**
 * One poll cycle: read OpenUsage, transform, push if it changed.
 *
 * Deduplication is not a nicety. Pushing every minute would be 1,440 KV writes a
 * day against a 1,000-write free-tier cap. The body carries no clock of its own,
 * so the only thing that moves it is fetchedAt, which OpenUsage advances about
 * every five minutes -- which puts the real write rate near 288 a day.
 */
export async function pollOnce(
  config: AgentConfig,
  state: AgentState,
): Promise<PollOutcome> {
  const doFetch = config.fetchImpl ?? fetch;
  const log = config.log ?? ((m: string) => console.log(m));

  let raw: unknown;
  try {
    const response = await doFetch(config.usageUrl, {
      signal: AbortSignal.timeout(FETCH_TIMEOUT_MS),
    });
    if (!response.ok) {
      log(`openusage returned ${response.status}; keeping the last snapshot`);
      return 'source-unavailable';
    }
    raw = await response.json();
  } catch (e) {
    log(`openusage unreachable (${e instanceof Error ? e.message : e}); keeping the last snapshot`);
    return 'source-unavailable';
  }

  const body = transformOpenUsage(raw);
  // getTimezoneOffset() is minutes WEST of UTC, so it is the negation of what
  // the clients want to add. Recomputed every poll, which is what makes DST a
  // non-event rather than a twice-yearly bug.
  body.utcOffsetSec = -new Date().getTimezoneOffset() * 60;
  if (body.providers.length === 0) {
    // An empty result means OpenUsage is up but has nothing, usually because it
    // just launched. Never overwrite good data with nothing.
    log('openusage returned no usable providers; keeping the last snapshot');
    return 'source-unavailable';
  }

  const json = JSON.stringify(body);
  if (json === state.lastPushedJson) {
    // Deduplication means the healthy steady state is silence, which reads
    // exactly like a crashed agent when someone tails the log to check. Emit a
    // heartbeat occasionally so "quiet" and "dead" are distinguishable.
    state.quietPolls = (state.quietPolls ?? 0) + 1;
    if (state.quietPolls % QUIET_HEARTBEAT_POLLS === 0) {
      log(`no change for ${state.quietPolls} polls; last push still current`);
    }
    return 'unchanged';
  }
  state.quietPolls = 0;

  try {
    const response = await doFetch(config.pushUrl, {
      method: 'POST',
      headers: {
        authorization: `Bearer ${config.pushToken}`,
        'content-type': 'application/json',
      },
      body: json,
      signal: AbortSignal.timeout(FETCH_TIMEOUT_MS),
    });
    if (!response.ok) {
      log(`push failed with ${response.status}; will retry next poll`);
      return 'push-failed';
    }
  } catch (e) {
    log(`push failed (${e instanceof Error ? e.message : e}); will retry next poll`);
    return 'push-failed';
  }

  // Only remember it after the push actually landed, so a failure retries.
  state.lastPushedJson = json;
  // Encoded length, not json.length: the whole design turns on byte counts
  // against a 400-800 B/s link, and the U+00B7 separators OpenUsage puts in its
  // text values cost two bytes each while counting as one UTF-16 code unit.
  const bytes = new TextEncoder().encode(json).length;
  log(`pushed ${body.providers.length} providers (${bytes} bytes)`);
  return 'pushed';
}

function requireEnv(name: string): string {
  const value = process.env[name];
  if (!value) {
    console.error(`${name} is not set`);
    process.exit(1);
  }
  return value;
}

async function main(): Promise<void> {
  const config: AgentConfig = {
    usageUrl: process.env['CLAUDEBOY_USAGE_URL'] ?? 'http://127.0.0.1:6736/v1/usage',
    pushUrl: requireEnv('CLAUDEBOY_PUSH_URL'),
    pushToken: requireEnv('CLAUDEBOY_PUSH_TOKEN'),
    log: (m) => console.log(`[${new Date().toISOString()}] ${m}`),
  };
  const intervalMs = intervalMsFromEnv(process.env['CLAUDEBOY_INTERVAL_SEC'], config.log!);
  const state: AgentState = { lastPushedJson: null };

  config.log!(`claudeboy agent starting, polling every ${intervalMs / 1000}s`);
  for (;;) {
    await pollOnce(config, state);
    await new Promise((resolve) => setTimeout(resolve, intervalMs));
  }
}

// Only run the loop when executed directly, so importing this module in tests
// does not start an infinite poll. Compare resolved paths rather than matching
// on the filename: a false positive here hangs the whole test run.
if (process.argv[1] && fileURLToPath(import.meta.url) === resolve(process.argv[1])) {
  void main();
}
