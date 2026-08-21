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
  if (client !== 'watch') return snapshot;
  return {
    serverTime: snapshot.serverTime,
    providers: snapshot.providers.map((p): Provider => ({
      id: p.id,
      displayName: p.displayName,
      plan: p.plan,
      fetchedAt: p.fetchedAt,
      progress: p.progress,
    })),
  };
}
