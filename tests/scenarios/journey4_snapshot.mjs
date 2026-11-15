// Journey 4: snapshot -> change -> diff (the "find a value" workflow), without needing write access.
// Stable data: first 0x1000 bytes of the main module (PE headers). Changing data: KUSER_SHARED_DATA at 0x7FFE0000.
import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import { call, resolveTarget, expectServerError, assertAlive, sleep } from './lib.mjs';

const KUSER = '0x7FFE0000';
const RUN = `j4_${process.pid}_${Date.now()}`;
const created = [];
let t;

const snap = async (suffix, address, size) => {
  const name = `${RUN}_${suffix}`;
  const r = await call('memory_snapshot', { pid: t.pid, address, size, name });
  created.push(name);
  return { name, r };
};

after(async () => {
  for (const name of created) {
    try {
      await call('memory_snapshot_delete', { name });
    } catch {
      /* already deleted by a test */
    }
  }
});

test('setup: resolve target', async () => {
  t = await resolveTarget();
});

test('memory_snapshot returns name/address/size/pid', async () => {
  const { name, r } = await snap('basic', t.main.base, 0x1000);
  assert.equal(r.name, name);
  assert.equal(r.size, 0x1000);
  assert.equal(r.pid, t.pid);
  assert.equal(BigInt(r.address), BigInt(t.main.base));
});

test('stable: two snapshots of the module header -> 0 changed bytes (snapshot_vs_snapshot)', async () => {
  const a = await snap('stable_a', t.main.base, 0x1000);
  const b = await snap('stable_b', t.main.base, 0x1000);
  const d = await call('memory_diff', { mode: 'snapshot_vs_snapshot', snapshot_a: a.name, snapshot_b: b.name, filter: 'changed' });
  assert.equal(d.total_changed, 0);
  assert.equal(d.results_returned, 0);
  assert.deepEqual(d.differences, []);
  assert.equal(d.total_values_checked, 0x1000 / 4); // default value_size is 4
  assert.equal(d.snapshot_a, a.name);
  assert.equal(d.snapshot_b, b.name);
  assert.equal(BigInt(d.base_address), BigInt(t.main.base));
});

test('stable: snapshot vs live memory (default mode) -> 0 changed', async () => {
  const d = await call('memory_diff', { snapshot: `${RUN}_stable_a`, filter: 'changed' });
  assert.equal(d.snapshot_b, 'current');
  assert.equal(d.total_changed, 0);
  assert.equal(d.results_returned, 0);
});

test('stable: filter "unchanged" with value_size 1 returns every byte', async () => {
  const d = await call('memory_diff', { snapshot: `${RUN}_stable_a`, filter: 'unchanged', value_size: 1, max_results: 5000 });
  assert.equal(d.value_size, 1);
  assert.equal(d.total_values_checked, 0x1000);
  assert.equal(d.results_returned, 0x1000);
  assert.ok(d.differences.every((x) => x.change === 'unchanged'));
});

test('changing: KUSER_SHARED_DATA differs after ~1.5s (snapshot_vs_snapshot)', async () => {
  const a = await snap('kuser_a', KUSER, 0x1000);
  await sleep(1500);
  const b = await snap('kuser_b', KUSER, 0x1000);
  const d = await call('memory_diff', {
    mode: 'snapshot_vs_snapshot', snapshot_a: a.name, snapshot_b: b.name, filter: 'changed', value_size: 4, max_results: 1000,
  });
  assert.ok(d.total_changed >= 1, 'no bytes changed in KUSER_SHARED_DATA after 1.5s');
  assert.equal(d.results_returned, d.differences.length);
  assert.ok(d.differences.every((x) => x.old_value !== x.new_value && ['increased', 'decreased'].includes(x.change)));
  assert.ok(d.differences.some((x) => x.offset < 0x400), 'no change within the first 0x400 bytes (InterruptTime/SystemTime/TickCount)');
  const first = d.differences[0];
  assert.equal(BigInt(first.address), BigInt(KUSER) + BigInt(first.offset));
  assert.equal(first.delta, first.new_value - first.old_value);
});

test('truncated diff (max_results=1) still reports full totals', async () => {
  const args = { mode: 'snapshot_vs_snapshot', snapshot_a: `${RUN}_kuser_a`, snapshot_b: `${RUN}_kuser_b`, filter: 'all', value_size: 4 };
  const full = await call('memory_diff', { ...args, max_results: 100000 });
  const cut = await call('memory_diff', { ...args, max_results: 1 });
  assert.equal(full.total_values_checked, 0x1000 / 4);
  assert.equal(cut.results_returned, 1);
  assert.equal(cut.truncated, true);
  assert.equal(cut.total_values_checked, full.total_values_checked, 'total_values_checked stops counting at max_results');
  assert.equal(cut.total_changed, full.total_changed, 'total_changed stops counting at max_results');
});

test('changing: snapshot vs live memory also detects the change', async () => {
  const d = await call('memory_diff', { snapshot: `${RUN}_kuser_a`, filter: 'changed', max_results: 1000 });
  assert.ok(d.total_changed >= 1);
  assert.ok(d.differences.some((x) => x.offset < 0x400));
});

test('memory_snapshot_list contains our snapshots', async () => {
  const l = await call('memory_snapshot_list', {});
  assert.equal(l.count, l.snapshots.length);
  for (const n of ['stable_a', 'stable_b', 'kuser_a', 'kuser_b']) {
    const s = l.snapshots.find((x) => x.name === `${RUN}_${n}`);
    assert.ok(s, `snapshot ${n} missing from list`);
    assert.equal(s.size, 0x1000);
    assert.equal(s.pid, t.pid);
    assert.equal(typeof s.timestamp, 'string');
  }
  const k = l.snapshots.find((x) => x.name === `${RUN}_kuser_a`);
  assert.equal(BigInt(k.address), BigInt(KUSER));
});

test('memory_snapshot_delete removes a snapshot; second delete errors', async () => {
  const name = `${RUN}_stable_b`;
  const r = await call('memory_snapshot_delete', { name });
  assert.equal(r.name, name);
  const l = await call('memory_snapshot_list', {});
  assert.ok(!l.snapshots.some((x) => x.name === name), 'snapshot still listed after delete');
  await expectServerError(call('memory_snapshot_delete', { name }), /not found/i);
  await expectServerError(call('memory_diff', { snapshot: name }), /not found/i);
});

test('negative: diff with unknown snapshot -> clean error (both modes)', async () => {
  await expectServerError(call('memory_diff', { snapshot: 'no_such_snapshot' }), /not found/i);
  await expectServerError(
    call('memory_diff', { mode: 'snapshot_vs_snapshot', snapshot_a: 'no_such_a', snapshot_b: 'no_such_b' }),
    /not found/i,
  );
  await expectServerError(call('memory_snapshot_delete', { name: 'no_such_snapshot' }), /not found/i);
  await assertAlive();
});

test('negative: diff with missing/invalid parameters -> clean error', async () => {
  await expectServerError(call('memory_diff', {}));
  await expectServerError(call('memory_diff', { mode: 'snapshot_vs_snapshot', snapshot_a: `${RUN}_stable_a` }));
  await expectServerError(call('memory_diff', { snapshot: `${RUN}_stable_a`, value_size: 3 }), /value_size/i);
  await assertAlive();
});

test('negative: snapshot_vs_snapshot with different sizes -> clean error', async () => {
  const small = await snap('small', t.main.base, 0x100);
  await expectServerError(
    call('memory_diff', { mode: 'snapshot_vs_snapshot', snapshot_a: `${RUN}_stable_a`, snapshot_b: small.name }),
    /different sizes/i,
  );
  await assertAlive();
});

test('negative: snapshot with size 0 -> error (a 0-byte read returns empty -> "Failed to read memory")', async () => {
  await expectServerError(call('memory_snapshot', { pid: t.pid, address: t.main.base, size: 0, name: `${RUN}_zero` }), /fail|size/i);
  const l = await call('memory_snapshot_list', {});
  assert.ok(!l.snapshots.some((x) => x.name === `${RUN}_zero`), 'zero-size snapshot must not be stored');
  await assertAlive();
});

test('negative: snapshot over 16 MB (limit) -> error, not clamped', async () => {
  for (const size of [16 * 1024 * 1024 + 1, 0x10000000000]) {
    await expectServerError(call('memory_snapshot', { pid: t.pid, address: t.main.base, size, name: `${RUN}_huge` }), /exceeds maximum/i);
  }
  const l = await call('memory_snapshot_list', {});
  assert.ok(!l.snapshots.some((x) => x.name === `${RUN}_huge`));
  await assertAlive();
});

test('negative: snapshot with missing fields / bad pid / null address -> clean error', async () => {
  await expectServerError(call('memory_snapshot', { address: t.main.base, size: 16 }));
  await expectServerError(call('memory_snapshot', { pid: t.pid, size: 16 }));
  await expectServerError(call('memory_snapshot', { pid: 4294967294, address: '0x1000', size: 16 }, { timeout: 15000 }));
  await expectServerError(call('memory_snapshot', { pid: t.pid, address: '0x0', size: 16, name: `${RUN}_null` }), /fail/i);
  await assertAlive();
});
