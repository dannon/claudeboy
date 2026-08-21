import { validatePushBody, type PushBody, type Snapshot } from './schema.ts';
import { shapeForClient } from './shape.ts';

export interface Env {
  SNAPSHOTS: KVNamespace;
  CLAUDEBOY_PUSH_TOKEN: string;
  CLAUDEBOY_READ_TOKEN: string;
}

export const KV_KEY = 'snapshot:current';

const JSON_HEADERS = {
  'content-type': 'application/json',
  'cache-control': 'no-store',
};

function bearer(request: Request): string | null {
  const header = request.headers.get('authorization');
  if (!header) return null;
  const match = /^Bearer\s+(.+)$/i.exec(header.trim());
  return match ? match[1]!.trim() : null;
}

/**
 * Constant-time-ish comparison. Workers gives us no timing-safe primitive, and
 * an attacker here is guessing a token over the public internet against
 * Cloudflare's own latency jitter, so this is belt and braces rather than a
 * defence we lean on.
 */
function tokenMatches(supplied: string | null, expected: string): boolean {
  if (supplied === null || expected.length === 0) return false;
  if (supplied.length !== expected.length) return false;
  let diff = 0;
  for (let i = 0; i < supplied.length; i++) {
    diff |= supplied.charCodeAt(i) ^ expected.charCodeAt(i);
  }
  return diff === 0;
}

function unauthorized(): Response {
  return new Response(null, { status: 401 });
}

async function handlePush(request: Request, env: Env): Promise<Response> {
  if (!tokenMatches(bearer(request), env.CLAUDEBOY_PUSH_TOKEN)) return unauthorized();

  let parsed: unknown;
  try {
    parsed = await request.json();
  } catch {
    return new Response(JSON.stringify({ error: 'body is not JSON' }), {
      status: 400, headers: JSON_HEADERS,
    });
  }

  const result = validatePushBody(parsed);
  if (!result.ok) {
    return new Response(JSON.stringify({ error: result.error }), {
      status: 400, headers: JSON_HEADERS,
    });
  }

  // Store the validated value, not the raw parse. That drops any extra field the
  // agent sent -- notably a serverTime, which must come from this Worker's clock
  // at serve time and never from the push.
  const body: PushBody = { providers: result.value.providers };
  await env.SNAPSHOTS.put(KV_KEY, JSON.stringify(body));
  return new Response(null, { status: 204 });
}

async function handleSnapshot(request: Request, env: Env): Promise<Response> {
  if (!tokenMatches(bearer(request), env.CLAUDEBOY_READ_TOKEN)) return unauthorized();

  const stored = await env.SNAPSHOTS.get(KV_KEY);
  if (stored === null) {
    return new Response(JSON.stringify({ error: 'no snapshot' }), {
      status: 503, headers: JSON_HEADERS,
    });
  }

  const body = JSON.parse(stored) as PushBody;
  const snapshot: Snapshot = {
    serverTime: Math.floor(Date.now() / 1000),
    providers: body.providers,
  };
  const client = new URL(request.url).searchParams.get('client');
  return new Response(JSON.stringify(shapeForClient(snapshot, client)), {
    status: 200, headers: JSON_HEADERS,
  });
}

export default {
  // ctx is unused but part of the handler signature Workers calls us with.
  async fetch(request: Request, env: Env, _ctx: ExecutionContext): Promise<Response> {
    const { pathname } = new URL(request.url);
    if (pathname === '/v1/push') {
      if (request.method !== 'POST') return new Response(null, { status: 405 });
      return handlePush(request, env);
    }
    if (pathname === '/v1/snapshot') {
      if (request.method !== 'GET') return new Response(null, { status: 405 });
      return handleSnapshot(request, env);
    }
    return new Response(null, { status: 404 });
  },
};
