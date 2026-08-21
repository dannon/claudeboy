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
    const response = await doFetch(config.usageUrl);
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
  if (body.providers.length === 0) {
    // An empty result means OpenUsage is up but has nothing, usually because it
    // just launched. Never overwrite good data with nothing.
    log('openusage returned no usable providers; keeping the last snapshot');
    return 'source-unavailable';
  }

  const json = JSON.stringify(body);
  if (json === state.lastPushedJson) return 'unchanged';

  try {
    const response = await doFetch(config.pushUrl, {
      method: 'POST',
      headers: {
        authorization: `Bearer ${config.pushToken}`,
        'content-type': 'application/json',
      },
      body: json,
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
  log(`pushed ${body.providers.length} providers (${json.length} bytes)`);
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
  const intervalMs = Number(process.env['CLAUDEBOY_INTERVAL_SEC'] ?? 60) * 1000;
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
