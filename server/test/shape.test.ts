import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { shapeForClient } from '../src/shape.ts';
import type { Snapshot } from '../src/schema.ts';

const body = JSON.parse(
  readFileSync(new URL('../fixtures/snapshot-20260821.json', import.meta.url), 'utf8'),
);
const snap: Snapshot = { serverTime: 1787319874, ...body };

describe('shapeForClient', () => {
  it('drops text and chart for the watch', () => {
    for (const p of shapeForClient(snap, 'watch').providers) {
      expect('text' in p).toBe(false);
      expect('chart' in p).toBe(false);
      expect(p.progress.length).toBeGreaterThan(0);
    }
  });

  it('keeps text and chart for the board', () => {
    const claude = shapeForClient(snap, 'cyd').providers[0]!;
    expect(claude.text!.length).toBeGreaterThan(0);
    expect(claude.chart!.length).toBe(31);
  });

  it('returns the full document for an absent or unknown client', () => {
    expect(shapeForClient(snap, null)).toEqual(snap);
    expect(shapeForClient(snap, 'toaster')).toEqual(snap);
  });

  it('preserves serverTime, provider order, and every progress field', () => {
    const shaped = shapeForClient(snap, 'watch');
    expect(shaped.serverTime).toBe(1787319874);
    expect(shaped.providers.map((p) => p.id)).toEqual(snap.providers.map((p) => p.id));
    expect(shaped.providers[0]!.progress).toEqual(snap.providers[0]!.progress);
    expect(shaped.providers[0]!.plan).toBe(snap.providers[0]!.plan);
    expect(shaped.providers[0]!.fetchedAt).toBe(snap.providers[0]!.fetchedAt);
  });

  it('does not mutate its input', () => {
    const before = JSON.stringify(snap);
    shapeForClient(snap, 'watch');
    expect(JSON.stringify(snap)).toBe(before);
  });

  // The whole reason the watch variant exists. Garmin moves 400-800 bytes/s over
  // BLE and glance mode has ~28KB for the raw bytes AND the parsed Dictionary.
  it('keeps the watch payload under 4096 bytes', () => {
    const bytes = new TextEncoder().encode(
      JSON.stringify(shapeForClient(snap, 'watch')),
    ).length;
    expect(bytes, `watch payload was ${bytes} bytes`).toBeLessThan(4096);
  });

  it('shows the watch variant is dramatically smaller than the full one', () => {
    const enc = (s: Snapshot) => new TextEncoder().encode(JSON.stringify(s)).length;
    expect(enc(shapeForClient(snap, 'watch'))).toBeLessThan(
      enc(shapeForClient(snap, 'cyd')) / 2,
    );
  });
});
