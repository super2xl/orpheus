// Runs ui_parity, then journeys 1 -> 2 -> 3. Journeys 2/3 are skipped if journey 1 fails.
// Usage: node tests/scenarios/run.mjs
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const dir = dirname(fileURLToPath(import.meta.url));
const steps = ['ui_parity', 'journey1_connect', 'journey2_process_memory', 'journey3_scan'];
const rows = [];
let journey1Failed = false;

for (const name of steps) {
  if (journey1Failed && /^journey[23]/.test(name)) {
    rows.push({ name, status: 'SKIP', ms: 0, detail: 'journey1 failed' });
    continue;
  }
  const t0 = Date.now();
  const r = spawnSync(process.execPath, [join(dir, `${name}.mjs`)], { encoding: 'utf8', timeout: 10 * 60 * 1000 });
  const out = `${r.stdout || ''}${r.stderr || ''}`;
  const ok = r.status === 0;
  const pass = out.match(/(?:#|ℹ)\s*pass\s+(\d+)/)?.[1];
  const fail = out.match(/(?:#|ℹ)\s*fail\s+(\d+)/)?.[1];
  rows.push({
    name,
    status: ok ? 'PASS' : 'FAIL',
    ms: Date.now() - t0,
    detail: pass !== undefined ? `${pass} passed, ${fail ?? 0} failed` : '',
    out: ok ? '' : out,
  });
  if (!ok && name === 'journey1_connect') journey1Failed = true;
}

console.log('\nscenario'.padEnd(28) + 'result'.padEnd(8) + 'time'.padEnd(10) + 'detail');
for (const r of rows) {
  console.log(r.name.padEnd(27) + ' ' + r.status.padEnd(7) + `${(r.ms / 1000).toFixed(1)}s`.padEnd(10) + r.detail);
}
for (const r of rows.filter((x) => x.out)) {
  console.log(`\n--- ${r.name} output (tail) ---\n${r.out.trim().split('\n').slice(-40).join('\n')}`);
}
process.exit(rows.some((r) => r.status === 'FAIL') ? 1 : 0);
