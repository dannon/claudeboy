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
  return v;
}

function epochSec(o: Record<string, unknown>, key: string, where: string): number {
  const v = int(o, key, where);
  if (Math.abs(v) >= MILLIS_THRESHOLD) {
    throw new Error(`${where}.${key} looks like milliseconds; must be epoch seconds`);
  }
  return v;
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
    used: int(u, 'used', where),
    limit: int(u, 'limit', where),
    resetsAt: epochSec(u, 'resetsAt', where),
    periodSec: int(u, 'periodSec', where),
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
  // Absent is the contract for "none"; an empty array would be a different thing
  // on the wire and clients should never see it.
  if (u['text'] !== undefined) {
    p.text = arr(u, 'text', where).map((l, i) => textLine(l, `${where}.text[${i}]`));
  }
  if (u['chart'] !== undefined) {
    p.chart = arr(u, 'chart', where).map((l, i) => chartPoint(l, `${where}.chart[${i}]`));
  }
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
