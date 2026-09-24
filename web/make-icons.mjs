// Home-screen icons cut from the blessed STAT golden: Vault Boy as the board
// actually draws him, phosphor and all, scaled up by whole pixels. Rerun after
// re-blessing if he changes; the PNGs are committed so serving needs no build.
import { readFileSync, writeFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const raw = readFileSync(here('../goldens/ambient-claude.raw'));
const SW = 320;
// BOY_X/BOY_Y/BOY_W/BOY_H from core/screen.h, padded out to a square.
const BOX = 128, X0 = 6 - 1, Y0 = 24 - 11;

// core/palette.cpp through RGB565, the same way src/web/main.cpp shows it.
function rgb(i) {
  const sq = (i * i + 1 + ((i * i) >> 8)) >> 8;   // fastdiv.h div255
  const r = Math.floor((sq * 30) / 100), g = i, b = Math.floor((sq * 45) / 100);
  return [((r >> 3) * 255 / 31) | 0, ((g >> 2) * 255 / 63) | 0, ((b >> 3) * 255 / 31) | 0];
}

const crc = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return (buf) => { let c = ~0; for (const b of buf) c = t[(c ^ b) & 0xff] ^ (c >>> 8); return ~c >>> 0; };
})();
function chunk(type, data) {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'ascii');
  data.copy(out, 8);
  out.writeUInt32BE(crc(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
}

for (const size of [256, 512]) {
  const k = size / BOX;
  const rows = Buffer.alloc(size * (1 + size * 3));
  for (let y = 0; y < size; y++) {
    rows[y * (1 + size * 3)] = 0;
    for (let x = 0; x < size; x++) {
      const sx = X0 + Math.floor(x / k), sy = Y0 + Math.floor(y / k);
      // The tab rule sits above him, and bloom off the hero gauges reaches a few pixels left of their x=126.
      const i = sx >= 6 && sx < 121 && sy >= 22 && sy < 24 + 105 ? raw[sy * SW + sx] : 0;
      rows.set(rgb(i), y * (1 + size * 3) + 1 + x * 3);
    }
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(size, 0); ihdr.writeUInt32BE(size, 4);
  ihdr[8] = 8; ihdr[9] = 2;
  writeFileSync(here(`icon-${size}.png`), Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', deflateSync(rows)), chunk('IEND', Buffer.alloc(0)),
  ]));
  console.log(`wrote icon-${size}.png`);
}
