import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { transformOpenUsage } from '../src/transform.ts';
import { validatePushBody } from '../src/schema.ts';

const raw = JSON.parse(
  readFileSync(new URL('../fixtures/openusage-20260821.json', import.meta.url), 'utf8'),
);

describe('transformOpenUsage', () => {
  it('produces a body that passes its own validator', () => {
    const r = validatePushBody(transformOpenUsage(raw));
    expect(r.ok, r.ok ? '' : r.error).toBe(true);
  });

  it('preserves provider order from OpenUsage', () => {
    expect(transformOpenUsage(raw).providers.map((p) => p.id)).toEqual([
      'claude',
      'codex',
      'antigravity',
    ]);
  });

  it('converts ISO timestamps to epoch seconds', () => {
    const p = transformOpenUsage(raw).providers[0]!;
    expect(Number.isInteger(p.fetchedAt)).toBe(true);
    expect(p.fetchedAt).toBeLessThan(1e11);
    expect(p.fetchedAt).toBeGreaterThan(1.7e9);
    for (const line of p.progress) {
      expect(Number.isInteger(line.resetsAt)).toBe(true);
      expect(line.resetsAt).toBeLessThan(1e11);
    }
  });

  it('converts periodDurationMs to periodSec', () => {
    const session = transformOpenUsage(raw).providers[0]!.progress.find(
      (l) => l.label === 'Session',
    )!;
    expect(session.periodSec).toBe(18000);
    const weekly = transformOpenUsage(raw).providers[0]!.progress.find(
      (l) => l.label === 'Weekly',
    )!;
    expect(weekly.periodSec).toBe(604800);
  });

  it('omits text and chart entirely for a provider that has neither', () => {
    const anti = transformOpenUsage(raw).providers.find((p) => p.id === 'antigravity')!;
    expect(anti.progress.length).toBeGreaterThan(0);
    expect('text' in anti).toBe(false);
    expect('chart' in anti).toBe(false);
  });

  it('keeps text and chart for providers that have them', () => {
    const claude = transformOpenUsage(raw).providers[0]!;
    expect(claude.text!.length).toBeGreaterThan(0);
    expect(claude.chart!.length).toBe(31);
    expect(claude.chart![0]!).toHaveProperty('label');
    expect(typeof claude.chart![0]!.value).toBe('number');
  });

  it('drops line types it does not understand rather than failing', () => {
    const doctored = structuredClone(raw);
    doctored[0].lines.push({ type: 'sparkline', label: 'New', data: [1, 2, 3] });
    const out = transformOpenUsage(doctored);
    expect(validatePushBody(out).ok).toBe(true);
    expect(out.providers[0]!.progress.map((l) => l.label)).not.toContain('New');
  });

  it('skips a progress line with an unparseable resetsAt rather than emitting NaN', () => {
    const doctored = structuredClone(raw);
    doctored[0].lines[0].resetsAt = 'not a date';
    const out = transformOpenUsage(doctored);
    expect(validatePushBody(out).ok).toBe(true);
    expect(out.providers[0]!.progress).toHaveLength(2);
  });

  it('carries no clock of its own, so it is byte-stable across calls', () => {
    expect(JSON.stringify(transformOpenUsage(raw))).toBe(
      JSON.stringify(transformOpenUsage(raw)),
    );
    expect(JSON.stringify(transformOpenUsage(raw))).not.toContain('serverTime');
  });

  it('matches the committed golden', () => {
    const golden = JSON.parse(
      readFileSync(new URL('../fixtures/snapshot-20260821.json', import.meta.url), 'utf8'),
    );
    expect(transformOpenUsage(raw)).toEqual(golden);
  });
});
