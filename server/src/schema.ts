// The wire contract. Shared by the transform, the Worker and the agent so the
// producer and the validator cannot drift apart.
//
// Every timestamp here is epoch SECONDS. Monkey C's Number is 32-bit signed and
// overflows on epoch milliseconds, so the watch could not hold them.

export interface ProgressLine {
  label: string;
  used: number;
  limit: number;
  resetsAt: number;
  periodSec: number;
}

export interface TextLine {
  label: string;
  value: string;
}

export interface ChartPoint {
  label: string;
  value: number;
}

export interface Provider {
  id: string;
  displayName: string;
  plan: string;
  fetchedAt: number;
  progress: ProgressLine[];
  text?: TextLine[];
  chart?: ChartPoint[];
}

/** What the agent POSTs. Carries no clock of its own -- see Snapshot. */
export interface PushBody {
  providers: Provider[];
}

/** What a client GETs. serverTime is stamped by the Worker at serve time. */
export interface Snapshot extends PushBody {
  serverTime: number;
}

export type ClientKind = 'cyd' | 'watch';

export type ValidationResult =
  | { ok: true; value: PushBody }
  | { ok: false; error: string };

// Any epoch value at or above this is milliseconds, not seconds. 1e11 seconds is
// year 5138; 1e11 milliseconds is 1973. Nothing legitimate lands between.
const MILLIS_THRESHOLD = 1e11;

// No usage window runs longer than a year -- the longest OpenUsage reports is a
// week -- so anything above this is periodDurationMs with the /1000 forgotten.
// 604800000 fits in an int32, so without this ceiling nothing else catches it.
// A bound, not a proof: a short window left in milliseconds still lands under it.
const MAX_PERIOD_SEC = 366 * 24 * 60 * 60;

// Monkey C's Number is signed 32-bit, so every integer on the wire has to fit
// here or the watch silently wraps it.
const INT32_MIN = -2147483648;
const INT32_MAX = 2147483647;

function isRecord(u: unknown): u is Record<string, unknown> {
  return typeof u === 'object' && u !== null && !Array.isArray(u);
}

function str(o: Record<string, unknown>, key: string, where: string): string {
  const v = o[key];
  if (typeof v !== 'string') throw new Error(`${where}.${key} must be a string`);
  return v;
}

function int(o: Record<string, unknown>, key: string, where: string): number {
  const v = o[key];
  if (typeof v !== 'number' || !Number.isInteger(v)) {
    throw new Error(`${where}.${key} must be an integer`);
  }
  if (v < INT32_MIN || v > INT32_MAX) {
    throw new Error(`${where}.${key} does not fit in a signed 32-bit integer`);
  }
  return v;
}

function intAtLeast(
  o: Record<string, unknown>,
  key: string,
  where: string,
  min: number,
): number {
  const v = int(o, key, where);
  if (v < min) throw new Error(`${where}.${key} must be at least ${min}`);
  return v;
}

function epochSec(o: Record<string, unknown>, key: string, where: string): number {
  // Ahead of the int32 check on purpose: epoch millis blows both bounds and the
  // millisecond diagnosis is the one that tells you where the bug actually is.
  const v = o[key];
  if (typeof v === 'number' && Math.abs(v) >= MILLIS_THRESHOLD) {
    throw new Error(`${where}.${key} looks like milliseconds; must be epoch seconds`);
  }
  return int(o, key, where);
}

function durationSec(o: Record<string, unknown>, key: string, where: string): number {
  // Ahead of the int32 check for the same reason as epochSec: a monthly period in
  // milliseconds blows both bounds and only this message names the actual bug.
  const v = o[key];
  if (typeof v === 'number' && v > MAX_PERIOD_SEC) {
    throw new Error(`${where}.${key} looks like milliseconds; must be a duration in seconds`);
  }
  return intAtLeast(o, key, where, 1);
}

function arr(o: Record<string, unknown>, key: string, where: string): unknown[] {
  const v = o[key];
  if (!Array.isArray(v)) throw new Error(`${where}.${key} must be an array`);
  return v;
}

function progressLine(u: unknown, where: string): ProgressLine {
  if (!isRecord(u)) throw new Error(`${where} must be an object`);
  return {
    label: str(u, 'label', where),
    used: intAtLeast(u, 'used', where, 0),
    // A zero limit or period is a divide-by-zero in every percentage the Worker,
    // the CYD and the watch compute, so it never gets past the push.
    limit: intAtLeast(u, 'limit', where, 1),
    resetsAt: epochSec(u, 'resetsAt', where),
    periodSec: durationSec(u, 'periodSec', where),
  };
}

function textLine(u: unknown, where: string): TextLine {
  if (!isRecord(u)) throw new Error(`${where} must be an object`);
  return { label: str(u, 'label', where), value: str(u, 'value', where) };
}

function chartPoint(u: unknown, where: string): ChartPoint {
  if (!isRecord(u)) throw new Error(`${where} must be an object`);
  return { label: str(u, 'label', where), value: int(u, 'value', where) };
}

function optional<T>(
  u: Record<string, unknown>,
  key: string,
  where: string,
  item: (v: unknown, where: string) => T,
): T[] | undefined {
  if (u[key] === undefined) return undefined;
  const out = arr(u, key, where).map((v, i) => item(v, `${where}.${key}[${i}]`));
  // Absent is the contract for "none"; an empty array is a different thing on the
  // wire, so it is normalised away here rather than left to the producer.
  return out.length ? out : undefined;
}

function provider(u: unknown, where: string): Provider {
  if (!isRecord(u)) throw new Error(`${where} must be an object`);
  const p: Provider = {
    id: str(u, 'id', where),
    displayName: str(u, 'displayName', where),
    plan: str(u, 'plan', where),
    fetchedAt: epochSec(u, 'fetchedAt', where),
    progress: arr(u, 'progress', where).map((l, i) =>
      progressLine(l, `${where}.progress[${i}]`),
    ),
  };
  const text = optional(u, 'text', where, textLine);
  if (text) p.text = text;
  const chart = optional(u, 'chart', where, chartPoint);
  if (chart) p.chart = chart;
  return p;
}

export function validatePushBody(u: unknown): ValidationResult {
  try {
    if (!isRecord(u)) throw new Error('body must be a JSON object');
    const providers = arr(u, 'providers', 'body').map((p, i) =>
      provider(p, `providers[${i}]`),
    );
    return { ok: true, value: { providers } };
  } catch (e) {
    return { ok: false, error: e instanceof Error ? e.message : String(e) };
  }
}
