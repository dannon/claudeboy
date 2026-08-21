import { describe, it, expect } from 'vitest';
import { validatePushBody } from '../src/schema.ts';

const VALID = {
  providers: [
    {
      id: 'claude',
      displayName: 'Claude',
      plan: 'Max 5x',
      fetchedAt: 1787319799,
      progress: [
        { label: 'Session', used: 59, limit: 100, resetsAt: 1787340600, periodSec: 18000 },
      ],
      text: [{ label: 'Today', value: '$201.28' }],
      chart: [{ label: 'Jul 22', value: 7059800 }],
    },
    {
      id: 'antigravity',
      displayName: 'Antigravity',
      plan: 'Pro',
      fetchedAt: 1787319783,
      progress: [
        { label: 'Session', used: 0, limit: 100, resetsAt: 1787339272, periodSec: 18000 },
      ],
    },
  ],
};

function reject(mutate: (b: any) => void, needle: string) {
  const body = structuredClone(VALID);
  mutate(body);
  const r = validatePushBody(body);
  expect(r.ok, `expected rejection mentioning "${needle}"`).toBe(false);
  if (!r.ok) expect(r.error).toContain(needle);
}

function accept(mutate: (b: any) => void) {
  const body = structuredClone(VALID);
  mutate(body);
  const r = validatePushBody(body);
  if (!r.ok) throw new Error(`expected acceptance, got: ${r.error}`);
  return r.value;
}

describe('validatePushBody', () => {
  it('accepts a well-formed body, including a provider with no text or chart', () => {
    const r = validatePushBody(VALID);
    expect(r.ok).toBe(true);
    if (r.ok) {
      expect(r.value.providers).toHaveLength(2);
      // Every field has to survive into the returned value -- this is what the
      // Worker stores and serves, so a dropped field is a wire regression.
      expect(r.value.providers[0]).toEqual(VALID.providers[0]);
      expect(r.value.providers[0]!.text).toEqual([{ label: 'Today', value: '$201.28' }]);
      expect(r.value.providers[0]!.chart).toEqual([{ label: 'Jul 22', value: 7059800 }]);
      expect(r.value.providers[1]).toEqual(VALID.providers[1]);
      expect(r.value.providers[1]!.text).toBeUndefined();
      expect(r.value.providers[1]!.chart).toBeUndefined();
    }
  });

  it('rejects a non-object body', () => {
    expect(validatePushBody(null).ok).toBe(false);
    expect(validatePushBody('nope').ok).toBe(false);
    expect(validatePushBody([]).ok).toBe(false);
  });

  it('rejects a missing or non-array providers field', () => {
    reject((b) => delete b.providers, 'providers');
    reject((b) => (b.providers = {}), 'providers');
  });

  it('rejects a provider missing a required field', () => {
    reject((b) => delete b.providers[0].id, 'id');
    reject((b) => delete b.providers[0].displayName, 'displayName');
    reject((b) => delete b.providers[0].plan, 'plan');
    reject((b) => delete b.providers[0].fetchedAt, 'fetchedAt');
    reject((b) => delete b.providers[0].progress, 'progress');
  });

  it('rejects a timestamp that looks like milliseconds', () => {
    reject((b) => (b.providers[0].fetchedAt = 1787319799000), 'seconds');
    reject((b) => (b.providers[0].progress[0].resetsAt = 1787340600000), 'seconds');
  });

  it('rejects non-integer numeric fields', () => {
    reject((b) => (b.providers[0].progress[0].used = 59.5), 'used');
    reject((b) => (b.providers[0].fetchedAt = 'yesterday'), 'fetchedAt');
  });

  it('rejects integers the 32-bit watch cannot hold', () => {
    reject((b) => (b.providers[0].chart[0].value = 9e15), 'value');
    reject((b) => (b.providers[0].progress[0].periodSec = 2147483648), 'periodSec');
    reject((b) => (b.providers[0].progress[0].used = -2147483649), 'used');
  });

  it('rejects a periodSec that looks like milliseconds', () => {
    // The weekly window with the /1000 dropped. It fits in an int32, so the only
    // thing standing between it and a clean push is the one-year ceiling.
    reject((b) => (b.providers[0].progress[0].periodSec = 604800000), 'milliseconds');
    // A year itself is fine; the longest real window is a week.
    expect(accept((b) => (b.providers[0].progress[0].periodSec = 366 * 24 * 60 * 60))
      .providers[0]!.progress[0]!.periodSec).toBe(31622400);
  });

  it('rejects progress numbers that break every percentage downstream', () => {
    reject((b) => (b.providers[0].progress[0].used = -5), 'used');
    reject((b) => (b.providers[0].progress[0].limit = 0), 'limit');
    reject((b) => (b.providers[0].progress[0].periodSec = 0), 'periodSec');
  });

  it('rejects a progress line missing a field', () => {
    reject((b) => delete b.providers[0].progress[0].label, 'label');
    reject((b) => delete b.providers[0].progress[0].periodSec, 'periodSec');
  });

  it('rejects malformed text and chart entries', () => {
    reject((b) => (b.providers[0].text = [{ label: 'x' }]), 'value');
    reject((b) => (b.providers[0].chart = [{ label: 'x', value: 'big' }]), 'value');
  });

  it('normalises empty text and chart to absent, never []', () => {
    const value = accept((b) => {
      b.providers[0].text = [];
      b.providers[0].chart = [];
    });
    expect(value.providers[0]!.text).toBeUndefined();
    expect(value.providers[0]!.chart).toBeUndefined();
    const wire = JSON.stringify(value);
    expect(wire).not.toContain('"text"');
    expect(wire).not.toContain('"chart"');
  });

  it('accepts an empty providers array', () => {
    expect(validatePushBody({ providers: [] }).ok).toBe(true);
  });
});
