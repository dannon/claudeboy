import type { Provider, Snapshot } from './schema.ts';

/**
 * Trim a snapshot to what a given client can actually use.
 *
 * The watch gets progress lines only. It moves 400-800 bytes per second over BLE
 * and its glance mode holds the raw response and the parsed Dictionary at the
 * same time inside about 28KB, so a 31-point chart it has no room to draw is
 * ten seconds of transfer spent on nothing.
 *
 * An unknown client gets everything. A new client should see the whole contract
 * before someone teaches this function about it.
 */
export function shapeForClient(snapshot: Snapshot, client: string | null): Snapshot {
  // Case-folded on purpose. The caller is a string literal in Monkey C source,
  // and a stray ?client=WATCH would fall through to the unknown-client rule and
  // hand the watch the whole 3.5KB document -- four seconds of BLE and a real
  // -402 risk, with nothing in the response hinting that capitalisation did it.
  if (client?.toLowerCase() !== 'watch') return snapshot;
  return {
    serverTime: snapshot.serverTime,
    utcOffsetSec: snapshot.utcOffsetSec,
    providers: snapshot.providers.map((p): Provider => ({
      id: p.id,
      displayName: p.displayName,
      plan: p.plan,
      fetchedAt: p.fetchedAt,
      progress: p.progress,
    })),
  };
}
