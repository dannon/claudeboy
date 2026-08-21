import { describe, it, expect, beforeEach } from 'vitest';
import { readFileSync } from 'node:fs';
import { pollOnce, type AgentConfig, type AgentState } from '../src/agent.ts';

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
