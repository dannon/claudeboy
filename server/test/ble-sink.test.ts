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
    sink.send('{"serverTime":1,"providers":[]}');
    expect(writes).toHaveLength(1);
    expect(writes[0]!.endsWith('\n')).toBe(true);
    expect(JSON.parse(writes[0]!)).toEqual({ snapshot: { serverTime: 1, providers: [] } });
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
