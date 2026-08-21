import type { ChartPoint, ProgressLine, Provider, PushBody, TextLine } from './schema.ts';

// OpenUsage's own shape. Deliberately loose: we read what we need and ignore the
// rest, so a new field upstream is not a breaking change down here.
interface RawLine {
  type?: unknown;
  label?: unknown;
  used?: unknown;
  limit?: unknown;
  resetsAt?: unknown;
  periodDurationMs?: unknown;
  value?: unknown;
  points?: unknown;
}

function isRecord(u: unknown): u is Record<string, unknown> {
  return typeof u === 'object' && u !== null && !Array.isArray(u);
}

/** ISO 8601 to epoch seconds. Returns null when the input is not a usable date. */
function toEpochSec(u: unknown): number | null {
  if (typeof u !== 'string') return null;
  const ms = Date.parse(u);
  if (!Number.isFinite(ms)) return null;
  return Math.floor(ms / 1000);
}

function toInt(u: unknown): number | null {
  return typeof u === 'number' && Number.isFinite(u) ? Math.round(u) : null;
}

function progressFrom(line: RawLine): ProgressLine | null {
  const label = typeof line.label === 'string' ? line.label : null;
  const used = toInt(line.used);
  const limit = toInt(line.limit);
  const resetsAt = toEpochSec(line.resetsAt);
  const periodMs = toInt(line.periodDurationMs);
  if (label === null || used === null || limit === null) return null;
  if (resetsAt === null || periodMs === null) return null;
  return { label, used, limit, resetsAt, periodSec: Math.round(periodMs / 1000) };
}

function textFrom(line: RawLine): TextLine | null {
  if (typeof line.label !== 'string' || typeof line.value !== 'string') return null;
  return { label: line.label, value: line.value };
}

function chartFrom(line: RawLine): ChartPoint[] {
  if (!Array.isArray(line.points)) return [];
  const out: ChartPoint[] = [];
  for (const pt of line.points) {
    if (!isRecord(pt)) continue;
    const label = typeof pt['label'] === 'string' ? pt['label'] : null;
    const value = toInt(pt['value']);
    if (label === null || value === null) continue;
    out.push({ label, value });
  }
  return out;
}

function providerFrom(raw: unknown): Provider | null {
  if (!isRecord(raw)) return null;
  const id = typeof raw['providerId'] === 'string' ? raw['providerId'] : null;
  const displayName = typeof raw['displayName'] === 'string' ? raw['displayName'] : null;
  const fetchedAt = toEpochSec(raw['fetchedAt']);
  if (id === null || displayName === null || fetchedAt === null) return null;

  const plan = typeof raw['plan'] === 'string' ? raw['plan'] : '';
  const lines = Array.isArray(raw['lines']) ? (raw['lines'] as RawLine[]) : [];

  const progress: ProgressLine[] = [];
  const text: TextLine[] = [];
  let chart: ChartPoint[] = [];

  for (const line of lines) {
    if (!isRecord(line)) continue;
    switch (line.type) {
      case 'progress': {
        const p = progressFrom(line);
        if (p) progress.push(p);
        break;
      }
      case 'text': {
        const t = textFrom(line);
        if (t) text.push(t);
        break;
      }
      case 'barChart': {
        // Last one wins; OpenUsage has only ever sent one per provider.
        const c = chartFrom(line);
        if (c.length) chart = c;
        break;
      }
      default:
        break; // an unknown line type is not an error, it is just not ours yet
    }
  }

  const out: Provider = { id, displayName, plan, fetchedAt, progress };
  if (text.length) out.text = text;
  if (chart.length) out.chart = chart;
  return out;
}

/**
 * Convert an OpenUsage /v1/usage response into our push body.
 *
 * Deliberately total: anything malformed is dropped, never thrown on. The agent
 * runs unattended and a single bad line upstream should cost that line, not the
 * whole snapshot. Nothing here reads a clock -- see PushBody.
 */
export function transformOpenUsage(raw: unknown): PushBody {
  if (!Array.isArray(raw)) return { providers: [] };
  const providers: Provider[] = [];
  for (const p of raw) {
    const converted = providerFrom(p);
    if (converted) providers.push(converted);
  }
  return { providers };
}
