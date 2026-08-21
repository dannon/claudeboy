import { describe, it, expect, beforeEach } from 'vitest';
import { readFileSync } from 'node:fs';
import worker, { type Env } from '../src/worker.ts';

// Spelled out rather than imported: the Worker cannot export it, and a literal
// here also fails loudly if the key is ever renamed under a live namespace.
const KV_KEY = 'snapshot:current';

const body = JSON.parse(
  readFileSync(new URL('../fixtures/snapshot-20260821.json', import.meta.url), 'utf8'),
);

class FakeKV {
  store = new Map<string, string>();
  async get(key: string) { return this.store.get(key) ?? null; }
  async put(key: string, value: string) { this.store.set(key, value); }
}

let kv: FakeKV;
let env: Env;

beforeEach(() => {
  kv = new FakeKV();
  env = {
    SNAPSHOTS: kv as unknown as KVNamespace,
    CLAUDEBOY_PUSH_TOKEN: 'push-secret',
    CLAUDEBOY_READ_TOKEN: 'read-secret',
  };
});

const ctx = {} as ExecutionContext;

function push(token: string, payload: unknown) {
  return worker.fetch(
    new Request('https://x/v1/push', {
      method: 'POST',
      headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' },
      body: JSON.stringify(payload),
    }),
    env,
    ctx,
  );
}

// Sends the authorization header verbatim, so a value that is not a Bearer
// credential actually reaches the scheme check.
function rawAuth(header: string) {
  return worker.fetch(
    new Request('https://x/v1/push', {
      method: 'POST',
      headers: { authorization: header, 'content-type': 'application/json' },
      body: JSON.stringify(body),
    }),
    env,
    ctx,
  );
}

function get(token: string | null, query = '') {
  return worker.fetch(
    new Request(`https://x/v1/snapshot${query}`, {
      headers: token ? { authorization: `Bearer ${token}` } : {},
    }),
    env,
    ctx,
  );
}

describe('POST /v1/push', () => {
  it('accepts a valid body with the push token and stores it', async () => {
    const r = await push('push-secret', body);
    expect(r.status).toBe(204);
    expect(JSON.parse(kv.store.get(KV_KEY)!)).toEqual(body);
  });

  it('rejects a missing, malformed or wrong token with 401', async () => {
    expect((await push('read-secret', body)).status).toBe(401);
    expect((await push('nonsense', body)).status).toBe(401);
    // The Bearer prefix is the only thing between a raw header value and
    // tokenMatches, so a correct token without it must still be rejected.
    expect((await rawAuth('push-secret')).status).toBe(401);
    expect((await rawAuth('Basic cHVzaC1zZWNyZXQ=')).status).toBe(401);
    expect((await rawAuth('Bearerpush-secret')).status).toBe(401);
    const bare = await worker.fetch(
      new Request('https://x/v1/push', { method: 'POST', body: '{}' }), env, ctx);
    expect(bare.status).toBe(401);
  });

  it('rejects an invalid body with 400 and does not touch KV', async () => {
    const r = await push('push-secret', { providers: [{ id: 'x' }] });
    expect(r.status).toBe(400);
    expect(kv.store.size).toBe(0);
    // The field-level diagnostic from the schema is what makes a rejected push
    // debuggable from the agent side, so it has to survive the Worker.
    expect(await r.json()).toEqual({ error: 'providers[0].displayName must be a string' });
  });

  it('rejects unparseable JSON with 400', async () => {
    const r = await worker.fetch(
      new Request('https://x/v1/push', {
        method: 'POST',
        headers: { authorization: 'Bearer push-secret' },
        body: 'not json',
      }), env, ctx);
    expect(r.status).toBe(400);
    expect(await r.json()).toEqual({ error: 'body is not JSON' });
  });

  it('stores only providers, never a serverTime', async () => {
    await push('push-secret', { ...body, serverTime: 1 });
    expect(JSON.parse(kv.store.get(KV_KEY)!)).not.toHaveProperty('serverTime');
  });
});

describe('GET /v1/snapshot', () => {
  it('rejects a missing or wrong token with 401', async () => {
    await push('push-secret', body);
    expect((await get(null)).status).toBe(401);
    const wrong = await get('push-secret');
    expect(wrong.status).toBe(401);
    expect(wrong.headers.get('www-authenticate')).toBe('Bearer');
  });

  it('returns 503 when nothing has been pushed yet', async () => {
    const r = await get('read-secret');
    expect(r.status).toBe(503);
    expect(await r.json()).toEqual({ error: 'no snapshot' });
  });

  it('401s rather than 500s when the token secret was never deployed', async () => {
    env.CLAUDEBOY_READ_TOKEN = undefined as unknown as string;
    expect((await get('read-secret')).status).toBe(401);
  });

  it('degrades to 503 when the stored value is not JSON', async () => {
    kv.store.set(KV_KEY, 'not json');
    const r = await get('read-secret');
    expect(r.status).toBe(503);
    expect(await r.json()).toEqual({ error: 'no snapshot' });
  });

  // Valid JSON of the wrong shape used to reach the shaper and throw there, which
  // is a 500 -- a status neither client has a state for, unlike the 503 both
  // already render as "keep the last good snapshot".
  it.each(['null', '{}', '[]', '{"providers":"x"}'])(
    'degrades to 503 when the stored value is %s',
    async (stored) => {
      kv.store.set(KV_KEY, stored);
      const plain = await get('read-secret');
      expect(plain.status, `stored ${stored}`).toBe(503);
      expect(await plain.json()).toEqual({ error: 'no snapshot' });
      // The watch path is the one that calls .map, so it has to be checked too.
      const watch = await get('read-secret', '?client=watch');
      expect(watch.status, `stored ${stored}, client=watch`).toBe(503);
    },
  );

  it('stamps serverTime from the Worker clock, not from the push', async () => {
    await push('push-secret', body);
    const before = Math.floor(Date.now() / 1000);
    const snap = (await (await get('read-secret')).json()) as any;
    expect(snap.serverTime).toBeGreaterThanOrEqual(before);
    expect(snap.serverTime).toBeLessThan(before + 5);
    expect(snap.serverTime).toBeLessThan(1e11);
  });

  it('shapes for the watch when asked', async () => {
    await push('push-secret', body);
    const snap = (await (await get('read-secret', '?client=watch')).json()) as any;
    for (const p of snap.providers) {
      // Both, not just chart: dropping chart and keeping text is the cheaper
      // regression and it still doubles what the watch has to pull over BLE.
      expect('chart' in p).toBe(false);
      expect('text' in p).toBe(false);
      expect(p.progress.length).toBeGreaterThan(0);
    }
  });

  it('returns the full document for client=cyd', async () => {
    await push('push-secret', body);
    const snap = (await (await get('read-secret', '?client=cyd')).json()) as any;
    expect(snap.providers[0].chart.length).toBe(31);
  });

  it('sets no-store so a stale snapshot is never cached anywhere', async () => {
    await push('push-secret', body);
    const r = await get('read-secret');
    expect(r.headers.get('cache-control')).toContain('no-store');
    expect(r.headers.get('content-type')).toContain('application/json');
  });
});

describe('routing', () => {
  it('404s an unknown path', async () => {
    const r = await worker.fetch(new Request('https://x/'), env, ctx);
    expect(r.status).toBe(404);
  });

  it('405s a GET on push and a POST on snapshot', async () => {
    const g = await worker.fetch(
      new Request('https://x/v1/push', { headers: { authorization: 'Bearer push-secret' } }),
      env, ctx);
    expect(g.status).toBe(405);
    expect(g.headers.get('allow')).toBe('POST');
    const p = await worker.fetch(
      new Request('https://x/v1/snapshot', {
        method: 'POST', headers: { authorization: 'Bearer read-secret' },
      }), env, ctx);
    expect(p.status).toBe(405);
    expect(p.headers.get('allow')).toBe('GET');
  });
});

describe('module shape', () => {
  // workerd treats every named export of the entrypoint as an entrypoint class
  // and refuses to start the Worker over a plain value, which a plain module
  // import here would never notice.
  it('exports nothing but the default handler', async () => {
    const module = await import('../src/worker.ts');
    expect(Object.keys(module).filter((k) => k !== 'default')).toEqual([]);
  });
});
