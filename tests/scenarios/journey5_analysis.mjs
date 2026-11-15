// Journey 5: disassemble -> decompile -> signature -> pattern scan -> function recovery -> CFG, anchored at the main module entry point.
import test from 'node:test';
import assert from 'node:assert/strict';
import { call, resolveTarget, expectServerError, assertAlive, OrpheusError } from './lib.mjs';

const hex = (v) => '0x' + BigInt(v).toString(16);
let t, entry, funcStart, sig;

test('setup: main module entry point', async () => {
  t = await resolveTarget();
  entry = BigInt(t.main.entry);
  const base = BigInt(t.main.base);
  if (entry !== 0n && entry < base) entry += base; // tolerate an RVA being reported
  assert.ok(entry >= base && entry < base + BigInt(t.main.size), `entry ${t.main.entry} not inside main module ${t.main.base}+${t.main.size}`);
});

test('disassemble at entry: first instruction is at entry, fields well-formed', async () => {
  const r = await call('disassemble', { pid: t.pid, address: hex(entry), count: 8 });
  assert.ok(Array.isArray(r.instructions) && r.instructions.length > 0, 'no instructions');
  assert.ok(r.instructions.length <= 8);
  assert.equal(r.count, r.instructions.length);
  assert.equal(BigInt(r.instructions[0].addr), entry);
  let prev = -1n;
  for (const i of r.instructions) {
    assert.match(i.addr, /^0x[0-9a-f]+$/i);
    assert.ok(BigInt(i.addr) > prev, 'addresses must increase');
    prev = BigInt(i.addr);
    assert.equal(typeof i.text, 'string');
    assert.ok(i.text.length > 0, 'empty instruction text');
    assert.match(i.bytes, /^[0-9a-f]{2}( [0-9a-f]{2})*$/i);
  }
  assert.equal(r.summary.total, r.instructions.length);
});

test('decompile at entry: C-like output', async (ctx) => {
  let r;
  try {
    r = await call('decompile', { pid: t.pid, address: hex(entry) }, { timeout: 60000 });
  } catch (e) {
    if (e instanceof OrpheusError && e.kind === 'server' && /not available|SLEIGH/i.test(e.message)) return ctx.skip(e.message);
    throw e;
  }
  assert.equal(r.success, true, `decompile failed: ${r.error}`);
  assert.equal(typeof r.c_code, 'string');
  assert.ok(r.c_code.includes('(') && r.c_code.includes('{'), 'output does not look like C');
  assert.equal(BigInt(r.address), entry);
});

test('generate_signature at entry, then scan_pattern over the main module finds entry', async (ctx) => {
  sig = await call('generate_signature', { pid: t.pid, address: hex(entry) });
  assert.equal(typeof sig.pattern, 'string');
  assert.match(sig.pattern, /^([0-9A-F]{2}|\?\?)( ([0-9A-F]{2}|\?\?))*$/i);
  assert.ok(sig.length > 0 && sig.instruction_count > 0);
  const r = await call('scan_pattern', { pid: t.pid, base: t.main.base, size: t.main.size, pattern: sig.pattern }, { timeout: 60000 });
  assert.equal(r.count, r.addresses.length);
  const hit = r.addresses.some((a) => BigInt(a) === entry);
  // The server caps scan hits at 100 in address order, so a weak signature may legitimately truncate before reaching entry.
  if (!hit && r.count >= 100) return ctx.skip(`signature too generic (${r.count}+ hits, capped); is_valid=${sig.is_valid}`);
  assert.ok(hit, `entry ${hex(entry)} not among ${r.count} hits for "${sig.pattern}"`);
});

test('recover_functions on main module', async () => {
  const r = await call('recover_functions', { pid: t.pid, module_base: t.main.base }, { timeout: 120000 });
  assert.ok(['cached', 'recovered'].includes(r.status), `unexpected status ${r.status}`);
  assert.ok(r.count > 0, 'no functions recovered');
  assert.equal(BigInt(r.module_base), BigInt(t.main.base));
});

test('get_function_containing(entry): start <= entry < start+size', async () => {
  const r = await call('get_function_containing', { pid: t.pid, address: hex(entry) });
  assert.equal(r.found, true, 'entry not inside any recovered function');
  funcStart = BigInt(r.function_start);
  assert.ok(funcStart <= entry);
  assert.equal(BigInt(r.offset_in_function), entry - funcStart);
  if (r.size > 0) assert.ok(entry < funcStart + BigInt(r.size), 'entry past end of function');
});

test('find_function_bounds(entry): start <= entry < end where reported', async () => {
  const r = await call('find_function_bounds', { pid: t.pid, address: hex(entry) });
  const hasStart = r.function_start !== undefined;
  const hasEnd = r.function_end !== undefined;
  assert.ok(hasStart || hasEnd, 'neither function start nor end detected');
  if (hasStart) assert.ok(BigInt(r.function_start) <= entry);
  if (hasEnd) assert.ok(BigInt(r.function_end) > entry);
  assert.ok(['low', 'medium', 'high'].includes(r.confidence));
});

test('build_cfg at the entry function: entry node address == function start', async () => {
  const start = funcStart ?? entry;
  const r = await call('build_cfg', { pid: t.pid, address: hex(start), max_size: 0x4000 }, { timeout: 60000 });
  assert.ok(r.node_count >= 1);
  assert.equal(r.nodes.length, r.node_count);
  assert.equal(BigInt(r.function_address), start);
  const node = r.nodes.find((n) => n.type === 'entry');
  assert.ok(node, 'no node of type "entry"');
  assert.equal(BigInt(node.address), start);
  assert.ok(Array.isArray(r.edges));
});

test('negative: disassemble / decompile / cfg at 0x0 -> clean error', async () => {
  await expectServerError(call('disassemble', { pid: t.pid, address: '0x0', count: 4 }), /null|invalid address/i);
  await expectServerError(call('decompile', { pid: t.pid, address: '0x0' }), /null|invalid address|not available/i);
  await expectServerError(call('build_cfg', { pid: t.pid, address: '0x0' }), /null|invalid address/i);
  await assertAlive();
});

test('negative: disassemble count out of range -> clean error', async () => {
  await expectServerError(call('disassemble', { pid: t.pid, address: hex(entry), count: 0 }), /count/i);
  await expectServerError(call('disassemble', { pid: t.pid, address: hex(entry), count: 5000 }), /count/i);
  await assertAlive();
});

test('negative: decompile at an unmapped address -> clean failure, server alive', async (ctx) => {
  // A decompiler-level failure is reported as {success:false,error} inside a successful HTTP response.
  let r;
  try {
    r = await call('decompile', { pid: t.pid, address: '0x10000' }, { timeout: 60000 });
  } catch (e) {
    if (e instanceof OrpheusError && e.kind === 'server') {
      if (/not available|SLEIGH/i.test(e.message)) return ctx.skip(e.message);
      await assertAlive();
      return; // a server-level error is also a clean failure
    }
    throw e;
  }
  assert.equal(r.success, false, 'decompile of an unmapped address unexpectedly succeeded');
  assert.ok(r.error, 'failure carried no error message');
  await assertAlive();
});

test('negative: disassemble at an unmapped address / invalid pid -> clean error', async () => {
  await expectServerError(call('disassemble', { pid: t.pid, address: '0x10000', count: 4 }, { timeout: 15000 }), /fail|unmapped/i);
  await expectServerError(call('disassemble', { pid: 4294967294, address: hex(entry), count: 4 }, { timeout: 15000 }), /process not found/i);
  await assertAlive();
});
