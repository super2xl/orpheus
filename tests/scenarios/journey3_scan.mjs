// Journey 3: sync/async pattern scan, string scan, xrefs, malformed pattern.
import test from 'node:test';
import assert from 'node:assert/strict';
import { call, resolveTarget, poll, expectServerError, assertAlive, inRange } from './lib.mjs';

const DOS = 'This program cannot be run in DOS mode';
let t, size, header, needle, syncRes;

test('setup: target module + first 8 bytes', async () => {
  t = await resolveTarget();
  size = Math.min(t.main.size, 0x1000); // headers + DOS stub only: fast and deterministic
  const r = await call('read_memory', { pid: t.pid, address: t.main.base, size: 8, format: 'hex' });
  header = r.hex.match(/../g).join(' ').toUpperCase();
  assert.match(header, /^4D 5A/);
});

test('scan_pattern finds header at module base', async () => {
  syncRes = await call('scan_pattern', { pid: t.pid, base: t.main.base, size, pattern: header });
  assert.ok(syncRes.count >= 1);
  assert.equal(syncRes.count, syncRes.addresses.length);
  assert.ok(syncRes.addresses.some((a) => BigInt(a) === BigInt(t.main.base)), 'no hit at module base');
});

test('scan_strings with contains filter', async () => {
  needle = DOS;
  let r = await call('scan_strings', { pid: t.pid, base: t.main.base, size, min_length: 4, contains: needle });
  if (r.count === 0) {
    // Non-standard stub: derive a needle from the first 0x100 bytes instead.
    const raw = await call('read_memory', { pid: t.pid, address: t.main.base, size: 0x100, format: 'hex' });
    const text = Buffer.from(raw.hex, 'hex').toString('latin1');
    const run = (text.match(/[\x20-\x7e]{8,}/g) || []).sort((a, b) => b.length - a.length)[0];
    assert.ok(run, 'no DOS message and no printable run in header to derive a needle from');
    needle = run.slice(0, 24);
    r = await call('scan_strings', { pid: t.pid, base: t.main.base, size, min_length: 4, contains: needle });
  }
  assert.ok(r.count >= 1, `no string containing "${needle}"`);
  assert.equal(r.count, r.strings.length);
  const s = r.strings[0];
  assert.ok(s.value.toLowerCase().includes(needle.toLowerCase()));
  assert.ok(inRange(s.address, t.main.base, size), 'string address outside scanned range');
});

test('scan_pattern_async matches sync result', async () => {
  const start = await call('scan_pattern_async', { pid: t.pid, base: t.main.base, size, pattern: header });
  assert.equal(typeof start.task_id, 'string');
  const task = await poll(
    async () => {
      const s = await call('task_status', { task_id: start.task_id });
      return ['completed', 'failed', 'cancelled'].includes(s.state) ? s : null;
    },
    { timeout: Number(process.env.ORPHEUS_TASK_TIMEOUT_MS || 60000), label: `task ${start.task_id}` },
  );
  assert.equal(task.state, 'completed', `task ended ${task.state}: ${task.error || ''}`);
  assert.equal(task.result.count, syncRes.count);
  assert.deepEqual([...task.result.addresses].sort(), [...syncRes.addresses].sort());
  const list = await call('task_list', {});
  assert.ok(list.tasks.some((x) => x.id === start.task_id), 'task_list does not contain the async task');
});

test('find_xrefs returns a well-formed response', async () => {
  const r = await call('find_xrefs', { pid: t.pid, target: t.main.base, base: t.main.base, size, max_results: 50 });
  assert.ok(Array.isArray(r.xrefs));
  assert.equal(r.count, r.xrefs.length);
  for (const x of r.xrefs) {
    assert.match(x.address, /^0x[0-9a-f]+$/i);
    assert.ok(['ptr64', 'rel32'].includes(x.type));
  }
});

test('negative: malformed patterns -> clean error', async () => {
  for (const pattern of ['ZZ ZZ', '4D 5', '']) {
    await expectServerError(call('scan_pattern', { pid: t.pid, base: t.main.base, size, pattern }), /pattern/i);
  }
  await expectServerError(
    call('scan_pattern_async', { pid: t.pid, base: t.main.base, size, pattern: 'ZZ ZZ' }),
    /pattern/i,
  );
  await assertAlive();
});

test('negative: task_status with missing/unknown task_id -> clean JSON error', async () => {
  await expectServerError(call('task_status', {}));
  await expectServerError(call('task_status', { task_id: 'does-not-exist' }));
  await assertAlive();
});
