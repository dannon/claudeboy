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

describe('validatePushBody', () => {
  it('accepts a well-formed body, including a provider with no text or chart', () => {
    const r = validatePushBody(VALID);
    expect(r.ok).toBe(true);
    if (r.ok) {
      expect(r.value.providers).toHaveLength(2);
      expect(r.value.providers[1]!.text).toBeUndefined();
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

  it('rejects a progress line missing a field', () => {
    reject((b) => delete b.providers[0].progress[0].label, 'label');
    reject((b) => delete b.providers[0].progress[0].periodSec, 'periodSec');
  });

  it('rejects malformed text and chart entries', () => {
    reject((b) => (b.providers[0].text = [{ label: 'x' }]), 'value');
    reject((b) => (b.providers[0].chart = [{ label: 'x', value: 'big' }]), 'value');
  });

  it('accepts an empty providers array', () => {
    expect(validatePushBody({ providers: [] }).ok).toBe(true);
  });
});
