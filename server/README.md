# ClaudeBoy server

Turns OpenUsage into the ClaudeBoy wire contract and serves it to two clients: the
CYD board on the desk and a Garmin Instinct 2X Solar on the wrist.

The contract, and why it looks the way it does, is specified at
`~/work/brain/plans/claudeboy-live-data/README.md`. Read that before changing a
field name.

## Layout

| file | what it is |
|---|---|
| `src/schema.ts` | the wire contract, plus a hand-rolled validator |
| `src/transform.ts` | OpenUsage's shape to ours |
| `src/shape.ts` | trims the payload per client |
| `src/worker.ts` | the Cloudflare Worker: two routes, two tokens, KV between them |
| `src/agent.ts` | the Mac side: poll, dedupe, push |
| `fixtures/` | a live OpenUsage capture and the golden it transforms into |

## Running the tests

    npm install
    npm test
    npm run typecheck

## Locally

    cp .dev.vars.example .dev.vars     # then edit in real tokens
    npx wrangler dev --port 8787

## Deploying

    npx wrangler kv namespace create SNAPSHOTS   # paste the id into wrangler.jsonc
    npx wrangler secret put CLAUDEBOY_PUSH_TOKEN
    npx wrangler secret put CLAUDEBOY_READ_TOKEN
    npx wrangler deploy

## The agent

    cp launchd/com.dannonbaker.claudeboy-agent.plist.example \
       ~/Library/LaunchAgents/com.dannonbaker.claudeboy-agent.plist
    # fill in the two REPLACE_ values, check `which node`, then
    launchctl load ~/Library/LaunchAgents/com.dannonbaker.claudeboy-agent.plist

Logs land in `/tmp/claudeboy-agent.log`.

## Things that will bite you

**Timestamps are epoch seconds, never milliseconds.** Monkey C's `Number` is 32-bit
signed and cannot hold epoch millis. The validator rejects anything at or above
1e11 for exactly this reason.

**`serverTime` is stamped on GET, not on push.** Clients seed their own clock from
it. If it carried the push time, a sleeping Mac would have every client computing
pace against a "now" hours in the past. `fetchedAt` carries the age instead.

**Never emit `text: []` or `chart: []`.** Absent means none. Antigravity already
ships progress lines and nothing else.

**KV free tier allows 1,000 writes a day.** The agent dedupes by comparing the
serialised body, which is why the body has no clock in it.

**The Worker exports nothing but its default handler.** workerd reads every other
named export as an entrypoint class and refuses to start over a plain value, which
`vitest` importing the module will not catch.
