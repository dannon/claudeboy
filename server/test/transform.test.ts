import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { transformOpenUsage } from '../src/transform.ts';
import { validatePushBody } from '../src/schema.ts';

const raw = JSON.parse(
  readFileSync(new URL('../fixtures/openusage-20260821.json', import.meta.url), 'utf8'),
);

// Counts are derived from the fixture rather than pinned to this capture's
// numbers, so a re-capture moves them instead of breaking a test about
// something else entirely.
function linesOf(provider: unknown): Array<Record<string, unknown>> {
  return (provider as { lines: Array<Record<string, unknown>> }).lines;
}

function chartPointsOf(provider: unknown): Array<Record<string, unknown>> {
  const bar = linesOf(provider).find((l) => l['type'] === 'barChart')!;
  return bar['points'] as Array<Record<string, unknown>>;
}

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
    expect(claude.chart!.length).toBe(chartPointsOf(raw[0]).length);
    expect(claude.chart![0]!).toHaveProperty('label');
    expect(typeof claude.chart![0]!.value).toBe('number');
  });

  it('drops line types it does not understand rather than failing', () => {
    const baseline = transformOpenUsage(raw).providers[0]!;
    const doctored = structuredClone(raw);
    doctored[0].lines.push({ type: 'sparkline', label: 'New', data: [1, 2, 3] });
    const out = transformOpenUsage(doctored);
    expect(validatePushBody(out).ok).toBe(true);
    // An unknown type has to leave every bucket alone, not just progress: it must
    // not be mis-routed into text or chart, and it must not take a real line with
    // it on the way out.
    const after = out.providers[0]!;
    expect(after.progress).toEqual(baseline.progress);
    expect(after.text).toEqual(baseline.text);
    expect(after.chart).toEqual(baseline.chart);
    expect(after.progress.map((l) => l.label)).not.toContain('New');
    expect(after.text!.map((l) => l.label)).not.toContain('New');
    expect(after.chart!.map((c) => c.label)).not.toContain('New');
  });

  it('skips a progress line with an unparseable resetsAt rather than emitting NaN', () => {
    const before = transformOpenUsage(raw).providers[0]!.progress.length;
    const doctored = structuredClone(raw);
    // The count below only says anything if the line being broken is a progress one.
    expect(linesOf(doctored[0])[0]!['type']).toBe('progress');
    doctored[0].lines[0].resetsAt = 'not a date';
    const out = transformOpenUsage(doctored);
    expect(validatePushBody(out).ok).toBe(true);
    expect(out.providers[0]!.progress).toHaveLength(before - 1);
  });

  it('drops a progress line the validator would refuse, not the whole push', () => {
    const before = transformOpenUsage(raw).providers[0]!.progress.length;
    const doctored = structuredClone(raw);
    doctored[0].lines.push({
      type: 'progress',
      label: 'Degenerate',
      used: -5,
      limit: 0,
      resetsAt: doctored[0].lines[0].resetsAt,
      periodDurationMs: 400,
    });
    const out = transformOpenUsage(doctored);
    const r = validatePushBody(out);
    expect(r.ok, r.ok ? '' : r.error).toBe(true);
    expect(out.providers[0]!.progress).toHaveLength(before);
  });

  it('drops a chart point too big for a 32-bit Number, not the whole push', () => {
    const before = transformOpenUsage(raw).providers[0]!.chart!.length;
    const doctored = structuredClone(raw);
    chartPointsOf(doctored[0]).push({ value: 3e9, valueLabel: '3B', label: 'Overflow' });
    const out = transformOpenUsage(doctored);
    const r = validatePushBody(out);
    expect(r.ok, r.ok ? '' : r.error).toBe(true);
    expect(out.providers[0]!.chart).toHaveLength(before);
  });

  it('drops a provider with a malformed header, shifting every later index down', () => {
    const doctored = structuredClone(raw);
    // The middle one, so the shift is visible: killing the last would only shorten.
    expect(doctored[1].providerId).toBe('codex');
    delete doctored[1].providerId;
    const out = transformOpenUsage(doctored);
    expect(validatePushBody(out).ok).toBe(true);
    // codex is gone and antigravity has slid into the slot it used to hold, so
    // providers[1] now names a different provider than it did last poll. This is
    // the drop policy working as intended and it is why clients key by id.
    expect(out.providers.map((p) => p.id)).toEqual(['claude', 'antigravity']);
  });

  it('drops a provider with an unparseable fetchedAt rather than emitting NaN', () => {
    const doctored = structuredClone(raw);
    doctored[1].fetchedAt = 'not a date';
    const out = transformOpenUsage(doctored);
    expect(validatePushBody(out).ok).toBe(true);
    expect(out.providers.map((p) => p.id)).toEqual(['claude', 'antigravity']);
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
