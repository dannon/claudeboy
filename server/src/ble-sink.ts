import { connect, type Socket } from 'node:net';

export interface BleSink {
  send(json: string): void;
  stop(): void;
}

export interface BleSinkOptions {
  socketPath: string;
  log: (m: string) => void;
  connectImpl?: (path: string) => Socket;
  retryMs?: number;
}

const DEFAULT_RETRY_MS = 5_000;

/**
 * Talks to the CoreBluetooth helper over its Unix socket.
 *
 * The helper is its own launchd job, not a child of this process: macOS
 * attributes Bluetooth permission to the responsible process, so a helper we
 * spawned would be denied it. launchd's KeepAlive owns the restarts; all this
 * has to do is connect, and reconnect when the socket goes away.
 *
 * The board is a nice-to-have sink, not the agent's job. Every failure here is
 * logged and swallowed, because a helper that is down must never stop the Worker
 * push the watch depends on.
 */
export function startBleSink(opts: BleSinkOptions): BleSink {
  const connectFn = opts.connectImpl ?? ((path: string) => connect(path));
  const retryMs = opts.retryMs ?? DEFAULT_RETRY_MS;
  let sock: Socket | null = null;
  let live = false;
  let stopped = false;
  let carry = '';

  function open(): void {
    if (stopped) return;
    const s = connectFn(opts.socketPath);
    sock = s;

    s.on('connect', () => {
      live = true;
      opts.log(`ble helper: connected on ${opts.socketPath}`);
    });
    s.on('data', (chunk: Buffer | string) => {
      carry += chunk.toString();
      const lines = carry.split('\n');
      carry = lines.pop() ?? '';
      for (const line of lines) {
        if (line.trim()) opts.log(`ble helper: ${line.trim()}`);
      }
    });
    // Both of these fire on a helper that is not running yet. Log once and let
    // the reconnect timer handle it rather than treating it as fatal.
    s.on('error', (e: Error) => opts.log(`ble helper: socket error (${e.message})`));
    s.on('close', () => {
      live = false;
      sock = null;
      if (!stopped) setTimeout(open, retryMs);
    });
  }

  open();

  return {
    send(json: string): void {
      if (!sock || !live) {
        opts.log('ble helper: not connected, dropping this snapshot');
        return;
      }
      try {
        // PushBody carries no serverTime -- that's Snapshot's field, stamped by
        // worker.ts at serve time for the WiFi client. This sink is the BLE
        // client's equivalent serve point, so it has to do the same stamping;
        // nothing upstream of here (agent.ts, the Swift helper) ever will. Skip
        // this and clock_seed() in main.cpp never fires (it's gated on
        // served > 0), so the board's clock stays at zero forever: age clamps
        // to 0, freshness_of() reads Fresh permanently -- no dimming, no STALE,
        // no SIGNAL LOST -- and the burn needle sticks at "--" since
        // burn_observe() is gated on at > 0. A dead pipeline would look
        // identical to a healthy one.
        const snapshot = { ...JSON.parse(json), serverTime: Math.floor(Date.now() / 1000) };
        sock.write(`${JSON.stringify({ snapshot })}\n`);
      } catch (e) {
        opts.log(`ble helper: write failed (${e instanceof Error ? e.message : e})`);
      }
    },
    stop(): void {
      stopped = true;
      sock?.destroy();
    },
  };
}
