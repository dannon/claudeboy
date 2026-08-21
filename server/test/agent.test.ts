import { describe, it, expect, beforeEach } from 'vitest';
import { readFileSync } from 'node:fs';
import {
  intervalMsFromEnv,
  pollOnce,
  type AgentConfig,
  type AgentState,
} from '../src/agent.ts';

const rawUsage = readFileSync(
  new URL('../fixtures/openusage-20260821.json', import.meta.url), 'utf8');

let pushes: Array<{ url: string; token: string | null; body: string }>;
let state: AgentState;

function makeConfig(
  over: Partial<AgentConfig> & { usageResponse?: () => Response },
): AgentConfig {
  const { usageResponse, ...rest } = over;   // usageResponse is ours, not AgentConfig's
  return {
    usageUrl: 'http://127.0.0.1:6736/v1/usage',
    pushUrl: 'https://claudeboy.example/v1/push',
    pushToken: 'push-secret',
    log: () => {},
    fetchImpl: async (input, init) => {
      const url = typeof input === 'string' ? input : (input as Request).url;
      if (url.includes('/v1/usage')) {
        return usageResponse ? usageResponse() : new Response(rawUsage, { status: 200 });
      }
      pushes.push({
        url,
        token: (init?.headers as Record<string, string>)?.['authorization'] ?? null,
        body: String(init?.body ?? ''),
      });
      return new Response(null, { status: 204 });
    },
    ...rest,
  };
}

beforeEach(() => {
  pushes = [];
  state = { lastPushedJson: null };
});

// What AbortSignal.timeout rejects with once the deadline passes.
function timedOut(): Error {
  return new DOMException('The operation was aborted due to timeout', 'TimeoutError');
}

describe('pollOnce', () => {
  it('pushes a transformed snapshot on the first successful poll', async () => {
    expect(await pollOnce(makeConfig({}), state)).toBe('pushed');
    expect(pushes).toHaveLength(1);
    expect(pushes[0]!.token).toBe('Bearer push-secret');
    const sent = JSON.parse(pushes[0]!.body);
    expect(sent.providers.map((p: any) => p.id)).toEqual(['claude', 'codex', 'antigravity']);
    expect(sent).not.toHaveProperty('serverTime');
  });

  it('skips the push when nothing changed', async () => {
    await pollOnce(makeConfig({}), state);
    expect(await pollOnce(makeConfig({}), state)).toBe('unchanged');
    expect(pushes).toHaveLength(1);
  });

  it('pushes again once the data actually moves', async () => {
    await pollOnce(makeConfig({}), state);
    const moved = JSON.parse(rawUsage);
    moved[0].lines[0].used = 61;
    expect(
      await pollOnce(makeConfig({ usageResponse: () => new Response(JSON.stringify(moved)) }), state),
    ).toBe('pushed');
    expect(pushes).toHaveLength(2);
  });

  it('pushes nothing and keeps prior state when OpenUsage is down', async () => {
    await pollOnce(makeConfig({}), state);
    const before = state.lastPushedJson;
    const outcome = await pollOnce(
      makeConfig({ usageResponse: () => { throw new Error('ECONNREFUSED'); } }), state);
    expect(outcome).toBe('source-unavailable');
    expect(pushes).toHaveLength(1);
    expect(state.lastPushedJson).toBe(before);
  });

  it('treats a non-200 from OpenUsage as unavailable', async () => {
    expect(
      await pollOnce(makeConfig({ usageResponse: () => new Response('', { status: 500 }) }), state),
    ).toBe('source-unavailable');
    expect(pushes).toHaveLength(0);
  });

  it('never pushes an empty snapshot over a good one', async () => {
    await pollOnce(makeConfig({}), state);
    const outcome = await pollOnce(
      makeConfig({ usageResponse: () => new Response('[]', { status: 200 }) }), state);
    expect(outcome).toBe('source-unavailable');
    expect(pushes).toHaveLength(1);
  });

  it('logs the pushed size in wire bytes, not UTF-16 code units', async () => {
    const logged: string[] = [];
    await pollOnce(makeConfig({ log: (m) => logged.push(m) }), state);
    const json = pushes[0]!.body;
    const bytes = new TextEncoder().encode(json).length;
    // The fixture's text values carry U+00B7 separators, so the two counts differ
    // -- which is the only reason this assertion says anything at all.
    expect(bytes).toBeGreaterThan(json.length);
    expect(logged.join('\n')).toContain(`(${bytes} bytes)`);
  });

  it('passes an abort signal to both fetches so neither can hang the loop', async () => {
    const signals: Array<AbortSignal | null | undefined> = [];
    const cfg = makeConfig({});
    cfg.fetchImpl = async (input, init) => {
      const url = typeof input === 'string' ? input : (input as Request).url;
      signals.push(init?.signal);
      if (url.includes('/v1/usage')) return new Response(rawUsage, { status: 200 });
      return new Response(null, { status: 204 });
    };
    expect(await pollOnce(cfg, state)).toBe('pushed');
    expect(signals).toHaveLength(2);
    for (const signal of signals) expect(signal).toBeInstanceOf(AbortSignal);
  });

  it('treats a usage fetch that times out as unavailable', async () => {
    const outcome = await pollOnce(
      makeConfig({ usageResponse: () => { throw timedOut(); } }), state);
    expect(outcome).toBe('source-unavailable');
    expect(pushes).toHaveLength(0);
  });

  it('treats a push that times out as push-failed, and retries next poll', async () => {
    const cfg = makeConfig({});
    cfg.fetchImpl = async (input) => {
      const url = typeof input === 'string' ? input : (input as Request).url;
      if (url.includes('/v1/usage')) return new Response(rawUsage, { status: 200 });
      throw timedOut();
    };
    expect(await pollOnce(cfg, state)).toBe('push-failed');
    expect(state.lastPushedJson).toBeNull();
    expect(await pollOnce(makeConfig({}), state)).toBe('pushed');
  });

  it('does not remember a push that failed, so the next poll retries it', async () => {
    const cfg = makeConfig({});
    cfg.fetchImpl = async (input) => {
      const url = typeof input === 'string' ? input : (input as Request).url;
      if (url.includes('/v1/usage')) return new Response(rawUsage, { status: 200 });
      return new Response('nope', { status: 500 });
    };
    expect(await pollOnce(cfg, state)).toBe('push-failed');
    expect(state.lastPushedJson).toBeNull();
    expect(await pollOnce(makeConfig({}), state)).toBe('pushed');
  });
});

describe('intervalMsFromEnv', () => {
  let logged: string[];
  const log = (m: string) => logged.push(m);

  beforeEach(() => { logged = []; });

  it('defaults to 60s when the variable is not set at all', () => {
    expect(intervalMsFromEnv(undefined, log)).toBe(60_000);
    expect(logged).toEqual([]);
  });

  it('takes a usable value', () => {
    expect(intervalMsFromEnv('120', log)).toBe(120_000);
    expect(intervalMsFromEnv('5', log)).toBe(5_000);
    expect(logged).toEqual([]);
  });

  // '', '   ', 'sixty', '0' and '-30' all come out of Number() as zero or NaN,
  // and setTimeout treats both as fire-immediately -- a spin, not a slow poll.
  // '1' parses fine and is just below the floor.
  it.each(['', '   ', 'sixty', '0', '-30', '1', 'NaN', 'Infinity'])(
    'falls back loudly on %o',
    (raw) => {
      expect(intervalMsFromEnv(raw, log)).toBe(60_000);
      expect(logged).toHaveLength(1);
      expect(logged[0]).toContain('CLAUDEBOY_INTERVAL_SEC');
    },
  );
});
