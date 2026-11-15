// Soak: hold one DMA session open for hours and check, every interval, that the
// target's modules, memory regions and MZ header still read. This is the only way
// to catch the long-session stale state (empty maps for every process).
// Not part of run.mjs. Usage:
//   ORPHEUS_SOAK_HOURS=4 ORPHEUS_SOAK_INTERVAL_S=60 node tests/scenarios/soak.mjs
// Writes one JSON line per sample to tests/scenarios/soak-<timestamp>.jsonl (gitignored)
// and prints a summary at the end. Exit code 1 if any sample failed and never recovered.
import { appendFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { call, sleep, TARGET } from './lib.mjs';

const HOURS = Number(process.env.ORPHEUS_SOAK_HOURS || 4);
const INTERVAL = Number(process.env.ORPHEUS_SOAK_INTERVAL_S || 60) * 1000;
// A modules call slower than this most likely ran the refresh-and-retry path.
const SLOW_MS = Number(process.env.ORPHEUS_SOAK_SLOW_MS || 1000);
const out = join(dirname(fileURLToPath(import.meta.url)), `soak-${new Date().toISOString().replace(/[:.]/g, '-')}.jsonl`);

const timed = async (fn) => {
  const t = performance.now();
  try {
    const v = await fn();
    return { ok: true, ms: Math.round(performance.now() - t), v };
  } catch (e) {
    return { ok: false, ms: Math.round(performance.now() - t), err: String(e.message).slice(0, 200) };
  }
};

async function sample() {
  const s = { t: new Date().toISOString() };
  const st = await timed(() => call('dma_status'));
  s.connected = st.ok && st.v.connected;
  const procs = await timed(() => call('get_processes'));
  s.processes = procs.ok ? procs.v.processes.length : procs.err;
  const p = procs.ok && procs.v.processes.find((x) => x.name.toLowerCase() === TARGET.toLowerCase());
  if (!p) { s.ok = false; s.why = `target ${TARGET} not found`; return s; }
  s.pid = p.pid;
  const m = await timed(() => call('get_modules', { pid: p.pid }));
  s.modules = m.ok ? m.v.count : m.err; s.modules_ms = m.ms;
  const r = await timed(() => call('memory_regions', { pid: p.pid }));
  s.regions = r.ok ? r.v.count : r.err; s.regions_ms = r.ms;
  let mz = false;
  if (m.ok && m.v.count > 0) {
    const main = m.v.modules.find((x) => x.name.toLowerCase() === p.name.toLowerCase()) || m.v.modules[0];
    const rd = await timed(() => call('read_memory', { pid: p.pid, address: main.base, size: 2, format: 'hex' }));
    mz = rd.ok && /4d\s*5a/i.test(JSON.stringify(rd.v));
  }
  s.mz = mz;
  s.slow = m.ms > SLOW_MS;
  s.ok = s.connected && m.ok && m.v.count > 0 && r.ok && r.v.count > 0 && mz;
  return s;
}

const end = Date.now() + HOURS * 3600e3;
let n = 0, bad = 0, slow = 0, recovered = 0, wasBad = false;
console.log(`soak: ${HOURS} h, every ${INTERVAL / 1000} s, target ${TARGET}, log ${out}`);
while (Date.now() < end) {
  const s = await sample();
  n++;
  if (s.slow) slow++;
  if (!s.ok) { bad++; wasBad = true; }
  else if (wasBad) { recovered++; wasBad = false; }
  appendFileSync(out, JSON.stringify(s) + '\n');
  if (!s.ok || s.slow) console.log(JSON.stringify(s));
  await sleep(INTERVAL);
}
console.log(`soak done: ${n} samples, ${bad} failed, ${recovered} recoveries, ${slow} slow (>${SLOW_MS} ms) modules calls, ended ${wasBad ? 'FAILING' : 'healthy'}`);
process.exit(wasBad ? 1 : 0);
