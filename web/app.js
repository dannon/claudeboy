// The page is a board: it fetches what the WiFi firmware fetches, on the same
// cadence, and hands every byte to the same core. What it adds is only what a
// browser tab needs and a desk never does -- surviving a cold start offline,
// and catching up the moment a phone wakes.
import load from './claudeboy.mjs';

const SNAPSHOT_URL = 'v1/snapshot?client=cyd';
const POLL_MS = 60_000;    // net.cpp POLL_MS
const RETRY_MS = 15_000;   // net.cpp RETRY_MS
// Phosphor decay is per frame, so the frame rate is part of the look. This is
// the board's cadence give or take -- its loop is a render, an SPI push and a
// 40ms delay -- not the browser's 60Hz, which would fade four times too fast.
const FRAME_MS = 120;
const STORE_KEY = 'claudeboy:last';
const PARSE_OK = 0;

const mod = await load();
mod._cb_init();

const W = mod._cb_width();
const H = mod._cb_height();
const canvas = document.getElementById('panel');
const ctx = canvas.getContext('2d');
const image = ctx.createImageData(W, H);
const pixels = new Uint8ClampedArray(mod.HEAPU8.buffer, mod._cb_pixels(), W * H * 4);

const localMs = () => Math.floor(performance.now()) >>> 0;
const encoder = new TextEncoder();

function accept(text, ageMs) {
  const bytes = encoder.encode(text);
  if (bytes.length > mod._cb_body_cap()) return -1;   // the board refuses these too
  mod.HEAPU8.set(bytes, mod._cb_body());
  return mod._cb_accept(bytes.length, localMs(), ageMs);
}

// A cold start with no network would otherwise be NO SIGNAL until the first
// fetch, where the board would still be showing its last numbers dimmed. Age
// is at least 1ms so the core knows this is a restore, not a live reading.
try {
  const saved = JSON.parse(localStorage.getItem(STORE_KEY) ?? 'null');
  if (saved?.body) accept(saved.body, Math.max(1, Date.now() - saved.at));
} catch { /* storage unavailable or garbled: start empty, like the board */ }

let pollTimer = 0;
async function poll() {
  clearTimeout(pollTimer);
  let wait = RETRY_MS;
  try {
    const res = await fetch(SNAPSHOT_URL, { cache: 'no-store' });
    if (res.ok) {
      const text = await res.text();
      if (accept(text, 0) === PARSE_OK) {
        wait = POLL_MS;
        try { localStorage.setItem(STORE_KEY, JSON.stringify({ body: text, at: Date.now() })); } catch {}
      }
    }
  } catch { /* offline: keep what is on screen and let it age */ }
  pollTimer = setTimeout(poll, wait);
}

canvas.addEventListener('pointerdown', (e) => {
  const r = canvas.getBoundingClientRect();
  const x = Math.floor(((e.clientX - r.left) / r.width) * W);
  const y = Math.floor(((e.clientY - r.top) / r.height) * H);
  mod._cb_tap(x, y);
});

// An ambient display that lets the phone lock itself is not one.
let wakeLock = null;
async function holdScreen() {
  try { wakeLock ??= await navigator.wakeLock?.request('screen'); } catch {}
  wakeLock?.addEventListener('release', () => { wakeLock = null; }, { once: true });
}

document.addEventListener('visibilitychange', () => {
  if (document.visibilityState !== 'visible') return;
  poll();
  holdScreen();
});

setInterval(() => {
  if (document.visibilityState !== 'visible') return;
  mod._cb_frame(localMs());
  image.data.set(pixels);
  ctx.putImageData(image, 0, 0);
}, FRAME_MS);

poll();
holdScreen();
if ('serviceWorker' in navigator) navigator.serviceWorker.register('sw.js');
