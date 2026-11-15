// Journey 6: resilience. Processes with no modules, a pid that has exited,
// and a DMA disconnect/reconnect in the middle of a workflow.
import test from 'node:test';
import assert from 'node:assert/strict';
import { call, post, poll, sleep, expectServerError, assertAlive, resolveTarget } from './lib.mjs';

const DEVICE = process.env.ORPHEUS_DEVICE || 'fpga';
const CONNECT_TIMEOUT = Number(process.env.ORPHEUS_CONNECT_TIMEOUT_MS || 60000);
const TASK_TIMEOUT = Number(process.env.ORPHEUS_TASK_TIMEOUT_MS || 120000);
const TERMINAL = new Set(['completed', 'failed', 'cancelled']);

async function waitConnected() {
  return poll(
    async () => {
      const st = await call('dma_status');
      if (st.connected) return st;
      if (!st.connecting && st.error) throw new Error(`DMA connect failed: ${st.error}`);
      return null;
    },
    { timeout: CONNECT_TIMEOUT, interval: 500, label: 'dma_status.connected' },
  );
}

async function ensureConnected() {
  const s = await call('dma_status');
  if (s.connected) return s;
  if (!s.connecting) await post('/tools/connect_dma', { device_type: DEVICE });
  return waitConnected();
}

let target;
test.before(async () => {
  await ensureConnected();
  target = await resolveTarget();
});
// Never leave the rig disconnected for the next journey.
test.after(async () => {
  await ensureConnected();
});

test('live process with no modules reports 0 modules, not a stale-session error', async () => {
  const procs = (await call('get_processes')).processes;
  const p = procs.find((x) => x.name.toLowerCase() === 'memcompression');
  if (!p) return; // not every target has it (memory compression can be disabled)
  const r = await call('get_modules', { pid: p.pid });
  assert.equal(r.count, 0);
  assert.deepEqual(r.modules, []);
  // Its memory regions still read, so the session is plainly not stale.
  const v = await call('memory_regions', { pid: p.pid });
  assert.ok(v.count > 0, 'MemCompression should still have memory regions');
});

test('exited pid: every per-process tool fails cleanly and the server stays up', async () => {
  const pids = new Set((await call('get_processes')).processes.map((p) => p.pid));
  let dead = 0x7ffffffc;
  while (pids.has(dead)) dead -= 4;
  const base = target.main.base;
  await expectServerError(call('get_modules', { pid: dead }));
  await expectServerError(call('memory_regions', { pid: dead }));
  await expectServerError(call('read_memory', { pid: dead, address: base, size: 16, format: 'hex' }));
  await expectServerError(call('scan_pattern', { pid: dead, base, size: 0x1000, pattern: '4D 5A' }));
  await expectServerError(call('memory_snapshot', { pid: dead, address: base, size: 0x1000, name: `j6_dead_${Date.now()}` }));
  await assertAlive();
});

test('disconnect mid-scan: the task ends, nothing hangs, reconnect restores everything', async () => {
  // Scan the largest region so the task is still running when the disconnect lands.
  const regions = (await call('memory_regions', { pid: target.pid })).regions;
  const big = regions.reduce((a, r) => (Number(r.size) > Number(a.size) ? r : a));
  const size = Math.min(Number(big.size), 512 * 1024 * 1024);
  const start = await call('scan_pattern_async', {
    pid: target.pid, base: big.base, size, pattern: '13 37 C0 DE 13 37 C0 DE',
  });
  assert.ok(start.task_id, 'async scan should return a task_id');
  await sleep(100);
  const mid = await call('task_status', { task_id: start.task_id });
  console.log(`# scan of ${(size / 1048576).toFixed(0)} MB was ${mid.state} at disconnect`);

  await post('/tools/disconnect_dma', {});
  const off = await call('dma_status');
  assert.equal(off.connected, false, 'dma_status should report disconnected');

  const final = await poll(
    async () => {
      const s = await call('task_status', { task_id: start.task_id });
      return TERMINAL.has(s.state) ? s : null;
    },
    { timeout: TASK_TIMEOUT, interval: 250, label: 'scan task to finish after disconnect' },
  );
  assert.ok(TERMINAL.has(final.state));

  // While disconnected, tools refuse cleanly instead of returning empty data.
  await expectServerError(call('get_modules', { pid: target.pid }), /not connected/i);
  await expectServerError(call('read_memory', { pid: target.pid, address: target.main.base, size: 8, format: 'hex' }), /not connected/i);
  await assertAlive();

  await post('/tools/connect_dma', { device_type: DEVICE });
  await waitConnected();

  const after = await resolveTarget();
  assert.equal(after.pid, target.pid, 'target pid should be unchanged across a reconnect');
  assert.ok(after.modules.length > 0, 'modules should load again after reconnect');
  const mz = await call('read_memory', { pid: after.pid, address: after.main.base, size: 2, format: 'hex' });
  assert.match(JSON.stringify(mz), /4d\s*5a/i, 'main module should still start with MZ');
});

test('reconnect twice in a row without waiting stays consistent', async () => {
  await post('/tools/disconnect_dma', {});
  await post('/tools/connect_dma', { device_type: DEVICE });
  await sleep(200);
  await post('/tools/disconnect_dma', {}).catch(() => {}); // may be refused while connecting
  await ensureConnected();
  const r = await call('get_modules', { pid: target.pid });
  assert.ok(r.count > 0);
});
