import { describe, it, expect, beforeEach } from 'vitest';
import { mkdtempSync, writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createWebHandler, type WebHandler } from '../src/web-server.ts';
import type { PushBody } from '../src/schema.ts';

const NOW_MS = 1_787_320_000_123;

const body: PushBody = {
  ...JSON.parse(readFileSync(
    new URL('../fixtures/snapshot-20260821.json', import.meta.url), 'utf8')) as PushBody,
  utcOffsetSec: -14400,
};

let root: string;
let web: WebHandler;

function get(path: string, headers: Record<string, string> = {}): Promise<Response> {
  return web.fetch(new Request(`http://127.0.0.1:6737${path}`, { headers }));
}

beforeEach(() => {
  root = mkdtempSync(join(tmpdir(), 'claudeboy-web-'));
  writeFileSync(join(root, 'index.html'), '<!doctype html>');
  writeFileSync(join(root, 'claudeboy.wasm'), Buffer.from([0, 0x61, 0x73, 0x6d]));
  mkdirSync(join(root, 'secret'));
  writeFileSync(join(root, 'secret', 'notes.txt'), 'no');
  web = createWebHandler({ root, now: () => NOW_MS });
});

describe('snapshot', () => {
  it('is a 503 until the agent has a reading', async () => {
    const res = await get('/v1/snapshot?client=cyd');
    expect(res.status).toBe(503);
  });

  it('stamps serverTime at serve time, in seconds', async () => {
    web.update(body);
    const res = await get('/v1/snapshot?client=cyd');
    expect(res.status).toBe(200);
    expect(res.headers.get('cache-control')).toBe('no-store');
    const snap = await res.json() as Record<string, unknown>;
    expect(snap['serverTime']).toBe(Math.floor(NOW_MS / 1000));
    expect(snap['utcOffsetSec']).toBe(-14400);
    expect(snap['providers']).toEqual(body.providers);
  });

  it('shapes per client exactly as the Worker does', async () => {
    web.update(body);
    const snap = await (await get('/v1/snapshot?client=watch')).json() as
      { providers: Array<Record<string, unknown>> };
    expect(body.providers[0]!.chart).toBeDefined();
    expect(snap.providers[0]!['chart']).toBeUndefined();
  });

  it('enforces the Tailscale identity when one is configured', async () => {
    web = createWebHandler({ root, trustedUser: 'me@example.com', now: () => NOW_MS });
    web.update(body);
    expect((await get('/v1/snapshot')).status).toBe(403);
    expect((await get('/v1/snapshot', { 'tailscale-user-login': 'you@example.com' })).status).toBe(403);
    expect((await get('/v1/snapshot', { 'tailscale-user-login': 'me@example.com' })).status).toBe(200);
  });

  it('leaves the app shell open behind the identity gate', async () => {
    web = createWebHandler({ root, trustedUser: 'me@example.com' });
    expect((await get('/')).status).toBe(200);
  });
});

describe('static files', () => {
  it('serves index.html at the root', async () => {
    const res = await get('/');
    expect(res.status).toBe(200);
    expect(res.headers.get('content-type')).toContain('text/html');
  });

  it('serves wasm with the type instantiateStreaming insists on', async () => {
    const res = await get('/claudeboy.wasm');
    expect(res.headers.get('content-type')).toBe('application/wasm');
  });

  it('refuses types it was not told about', async () => {
    expect((await get('/secret/notes.txt')).status).toBe(404);
  });

  it('refuses to climb out of the root', async () => {
    expect((await get('/%2e%2e/%2e%2e/etc/hosts')).status).toBe(404);
    expect((await get('/..%2f..%2fpackage.json')).status).toBe(404);
  });

  it('is a 404 for a malformed escape rather than a throw', async () => {
    expect((await get('/%E0%A4%A.html')).status).toBe(404);
  });

  it('answers only GET and HEAD', async () => {
    const res = await web.fetch(new Request('http://127.0.0.1:6737/', { method: 'POST' }));
    expect(res.status).toBe(405);
  });
});
