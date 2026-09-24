// Holds the WebAssembly build to the goldens the native tests use. The core is
// the same source, but a different compiler, libc and float path could still
// print a percent or place a needle one pixel off, and "looks the same" is not
// the claim this page makes.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

globalThis.self ??= globalThis;   // the ENVIRONMENT=web glue expects a window-ish global
const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const { default: load } = await import('./claudeboy.mjs');
const mod = await load({
  instantiateWasm(imports, done) {
    WebAssembly.instantiate(readFileSync(here('./claudeboy.wasm')), imports)
      .then(({ instance }) => done(instance));
  },
});

const pages = [['Stat', 'ambient-claude'], ['Data', 'ambient-data'], ['All', 'ambient-all']];
let failed = 0;
pages.forEach(([name, file], page) => {
  const golden = readFileSync(here(`../goldens/${file}.raw`));
  const ptr = mod._cb_reference(page);
  const got = mod.HEAPU8.subarray(ptr, ptr + golden.length);
  let diff = 0;
  for (let i = 0; i < golden.length; i++) if (got[i] !== golden[i]) diff++;
  console.log(`${name}: ${diff === 0 ? 'identical' : `${diff} pixels differ`}`);
  if (diff) failed++;
});
process.exit(failed ? 1 : 0);
