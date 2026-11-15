// Journey 2: processes -> modules -> read PE header -> regions; plus the negative cases a user hits.
import test from 'node:test';
import assert from 'node:assert/strict';
import { call, resolveTarget, expectServerError, assertAlive, inRange, TARGET } from './lib.mjs';

let t; // { pid, name, modules, main }

test('get_processes returns entries with pid + name', async () => {
  const r = await call('get_processes');
  assert.ok(Array.isArray(r.processes) && r.processes.length > 0, 'process list is empty');
  for (const p of r.processes.slice(0, 50)) {
    assert.equal(typeof p.pid, 'number');
    assert.equal(typeof p.name, 'string');
  }
  assert.equal(r.count, r.processes.length);
});

test(`get_modules for ${TARGET}: main module has base + size`, async () => {
  t = await resolveTarget();
  assert.ok(t.modules.length > 0);
  assert.match(t.main.base, /^0x[0-9a-f]+$/i);
  assert.ok(t.main.size > 0, 'main module size must be > 0');
});

test('read_memory at module base is "MZ"', async () => {
  const r = await call('read_memory', { pid: t.pid, address: t.main.base, size: 2, format: 'hex' });
  assert.equal(r.size, 2);
  assert.equal(r.hex.toLowerCase(), '4d5a');
});

test('memory_regions contains the module base', async () => {
  const r = await call('memory_regions', { pid: t.pid });
  assert.ok(r.regions.length > 0);
  assert.equal(r.count, r.regions.length);
  const hit = r.regions.find((g) => inRange(t.main.base, g.base, g.size));
  assert.ok(hit, `no region covers module base ${t.main.base}`);
});

test('negative: read_memory with invalid pid -> clean error', async () => {
  await expectServerError(
    call('read_memory', { pid: 4294967294, address: '0x1000', size: 2 }, { timeout: 15000 }),
    /process not found|does not exist/i,
  );
  await assertAlive();
});

test('negative: read_memory at address 0 -> clean error', async () => {
  await expectServerError(
    call('read_memory', { pid: t.pid, address: '0x0', size: 2 }, { timeout: 15000 }),
    /null|invalid address/i,
  );
  await assertAlive();
});

test('negative: read_memory with missing fields -> clean error', async () => {
  await expectServerError(call('read_memory', { address: '0x1000', size: 2 }, { timeout: 15000 }));
  await assertAlive();
});
