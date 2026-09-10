import { describe, expect, it, vi } from 'vitest';
import { EventEmitter } from 'node:events';
import { startBleSink } from '../src/ble-sink.ts';

function fakeSocket() {
  const writes: string[] = [];
  const sock = new EventEmitter() as any;
  sock.write = (s: string) => { writes.push(s); return true; };
  sock.destroy = vi.fn();
  sock.setEncoding = () => {};
  return { sock, writes };
}

describe('ble sink', () => {
  it('wraps the snapshot in an envelope, one line per send', () => {
    const { sock, writes } = fakeSocket();
    const connect = vi.fn(() => sock);
    const sink = startBleSink({ socketPath: '/x/ble.sock', log: () => {}, connectImpl: connect as any });
    sock.emit('connect');
    const pushBody = { providers: [], utcOffsetSec: -14400 };
    sink.send(JSON.stringify(pushBody));
    expect(writes).toHaveLength(1);
    expect(writes[0]!.endsWith('\n')).toBe(true);
    const { snapshot } = JSON.parse(writes[0]!);
    expect(snapshot.providers).toEqual([]);
    expect(snapshot.utcOffsetSec).toBe(-14400);
  });

  it('stamps a plausible serverTime on a PushBody that arrives without one', () => {
    const { sock, writes } = fakeSocket();
    const connect = vi.fn(() => sock);
    const sink = startBleSink({ socketPath: '/x/ble.sock', log: () => {}, connectImpl: connect as any });
    sock.emit('connect');
    const pushBody = { providers: [], utcOffsetSec: -14400 };
    const before = Math.floor(Date.now() / 1000);
    sink.send(JSON.stringify(pushBody));
    const after = Math.floor(Date.now() / 1000);
    const { snapshot } = JSON.parse(writes[0]!);
    expect(typeof snapshot.serverTime).toBe('number');
    expect(snapshot.serverTime).toBeGreaterThanOrEqual(before);
    expect(snapshot.serverTime).toBeLessThanOrEqual(after + 2);
    // The rest of the body survives unchanged.
    expect(snapshot.providers).toEqual(pushBody.providers);
    expect(snapshot.utcOffsetSec).toBe(pushBody.utcOffsetSec);
  });

  it('drops sends while disconnected rather than queueing them', () => {
    const { sock, writes } = fakeSocket();
    const connect = vi.fn(() => sock);
    const lines: string[] = [];
    const sink = startBleSink({ socketPath: '/x/ble.sock', log: (m) => lines.push(m), connectImpl: connect as any });
    // never emitted 'connect'
    sink.send('{"providers":[]}');
    expect(writes).toHaveLength(0);
    expect(lines.some((l) => l.includes('not connected'))).toBe(true);
  });

  it('logs the helper status lines', () => {
    const { sock } = fakeSocket();
    const connect = vi.fn(() => sock);
    const lines: string[] = [];
    startBleSink({ socketPath: '/x/ble.sock', log: (m) => lines.push(m), connectImpl: connect as any });
    sock.emit('connect');
    sock.emit('data', '{"state":"ready","mtu":244}\n{"wrote":4608}\n');
    expect(lines.some((l) => l.includes('ready'))).toBe(true);
    expect(lines.some((l) => l.includes('4608'))).toBe(true);
  });

  it('reassembles a status line split across two data events', () => {
    const { sock } = fakeSocket();
    const connect = vi.fn(() => sock);
    const lines: string[] = [];
    startBleSink({ socketPath: '/x/ble.sock', log: (m) => lines.push(m), connectImpl: connect as any });
    const firstHalf = '{"state":"re';
    const secondHalf = 'ady","mtu":512}';
    sock.emit('data', firstHalf);
    expect(lines).toHaveLength(0);
    sock.emit('data', `${secondHalf}\n`);
    expect(lines).toHaveLength(1);
    expect(lines[0]!).toBe(`ble helper: ${firstHalf}${secondHalf}`);
    expect(lines.some((l) => l === `ble helper: ${firstHalf}`)).toBe(false);
    expect(lines.some((l) => l === `ble helper: ${secondHalf}`)).toBe(false);
  });

  it('reconnects after the socket closes', async () => {
    const { sock } = fakeSocket();
    const connect = vi.fn(() => sock);
    startBleSink({ socketPath: '/x/ble.sock', log: () => {}, connectImpl: connect as any, retryMs: 1 });
    expect(connect).toHaveBeenCalledTimes(1);
    sock.emit('close');
    await new Promise((r) => setTimeout(r, 20));
    expect(connect).toHaveBeenCalledTimes(2);
  });

  it('never lets a socket failure reach the caller', () => {
    const { sock } = fakeSocket();
    const connect = vi.fn(() => sock);
    const sink = startBleSink({ socketPath: '/x/ble.sock', log: () => {}, connectImpl: connect as any });
    sock.emit('connect');
    sock.write = () => { throw new Error('EPIPE'); };
    expect(() => sink.send('{"providers":[]}')).not.toThrow();
    sock.emit('error', new Error('ECONNREFUSED'));   // must not throw either
  });
});
