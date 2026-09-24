import { createServer, type Server } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, resolve, sep } from 'node:path';
import type { PushBody, Snapshot } from './schema.ts';
import { shapeForClient } from './shape.ts';

export interface WebHandlerOptions {
  /** The built `web/` directory. */
  root: string;
  /**
   * When set, /v1/snapshot answers only requests carrying this login in the
   * Tailscale-User-Login header, which `tailscale serve` injects and a direct
   * loopback request does not. Same gate Collie uses.
   */
  trustedUser?: string;
  now?: () => number;
}

export interface WebHandler {
  update(body: PushBody): void;
  fetch(request: Request): Promise<Response>;
}

const TYPES: Record<string, string> = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.webmanifest': 'application/manifest+json',
  '.png': 'image/png',
};

const JSON_HEADERS = { 'content-type': 'application/json', 'cache-control': 'no-store' };

/**
 * The PWA's origin: the page and its wasm, plus the same /v1/snapshot the
 * Worker serves, answered from the agent's own last reading instead of KV.
 * Nothing here holds a token -- the tailnet is the only way in, and the
 * listener is loopback-only so `tailscale serve` is the only front door.
 */
export function createWebHandler(opts: WebHandlerOptions): WebHandler {
  const root = resolve(opts.root);
  const now = opts.now ?? Date.now;
  let latest: PushBody | null = null;

  async function snapshot(request: Request): Promise<Response> {
    if (opts.trustedUser && request.headers.get('tailscale-user-login') !== opts.trustedUser) {
      return new Response(JSON.stringify({ error: 'identity required' }), {
        status: 403, headers: JSON_HEADERS,
      });
    }
    if (latest === null) {
      return new Response(JSON.stringify({ error: 'no snapshot' }), {
        status: 503, headers: JSON_HEADERS,
      });
    }
    // Stamped on GET, like the Worker, and for the same reason: the clients
    // seed their clock from it.
    const snap: Snapshot = { serverTime: Math.floor(now() / 1000), providers: latest.providers };
    if (latest.utcOffsetSec !== undefined) snap.utcOffsetSec = latest.utcOffsetSec;
    const client = new URL(request.url).searchParams.get('client');
    return new Response(JSON.stringify(shapeForClient(snap, client)), {
      status: 200, headers: JSON_HEADERS,
    });
  }

  async function file(pathname: string): Promise<Response> {
    let rel: string;
    try {
      rel = pathname === '/' ? 'index.html' : decodeURIComponent(pathname.slice(1));
    } catch {
      return new Response(null, { status: 404 });
    }
    const path = resolve(root, rel);
    if (path !== root && !path.startsWith(root + sep)) return new Response(null, { status: 404 });
    const type = TYPES[extname(path)];
    if (!type) return new Response(null, { status: 404 });
    try {
      // no-cache, not no-store: revalidate every load so a rebuilt wasm lands
      // at once, and let the service worker keep a copy for offline starts.
      return new Response(await readFile(path), {
        status: 200, headers: { 'content-type': type, 'cache-control': 'no-cache' },
      });
    } catch {
      return new Response(null, { status: 404 });
    }
  }

  return {
    update(body) { latest = body; },
    async fetch(request) {
      // HEAD falls through as a GET; node:http drops the body on the way out.
      if (request.method !== 'GET' && request.method !== 'HEAD') {
        return new Response(null, { status: 405, headers: { allow: 'GET, HEAD' } });
      }
      const { pathname } = new URL(request.url);
      if (pathname === '/v1/snapshot') return snapshot(request);
      return file(pathname);
    },
  };
}

export function startWebServer(
  handler: WebHandler,
  opts: { host: string; port: number; log: (m: string) => void },
): Server {
  const server = createServer(async (req, res) => {
    const headers = new Headers();
    for (const [k, v] of Object.entries(req.headers)) {
      if (typeof v === 'string') headers.set(k, v);
    }
    const response = await handler.fetch(
      new Request(`http://${opts.host}:${opts.port}${req.url ?? '/'}`, { method: req.method, headers }),
    );
    res.writeHead(response.status, Object.fromEntries(response.headers));
    res.end(Buffer.from(await response.arrayBuffer()));
  });
  server.on('error', (e) => opts.log(`web server: ${e.message}`));
  server.listen(opts.port, opts.host, () => opts.log(`web server on http://${opts.host}:${opts.port}`));
  return server;
}
