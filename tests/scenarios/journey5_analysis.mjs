// Journey 5: disassemble -> decompile -> signature -> pattern scan -> function recovery -> CFG, anchored at the main module entry point.
import test from 'node:test';
import assert from 'node:assert/strict';
import { call, resolveTarget, expectServerError, assertAlive, OrpheusError } from './lib.mjs';

const hex = (v) => '0x' + BigInt(v).toString(16);
let t, entry, funcStart, funcSize, sig;

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

test('recover_functions uses (nearly) all of the module .pdata table', async () => {
  // Read the PE exception directory size; each RUNTIME_FUNCTION is 12 bytes. A few pages may be
  // paged out on the target, but the parser must not stop at the first one (it used to keep ~22%).
  const rd = async (addr, size) => Buffer.from((await call('read_memory', { pid: t.pid, address: hex(addr), size, format: 'hex' })).hex, 'hex');
  const base = BigInt(t.main.base);
  const peOff = (await rd(base, 64)).readUInt32LE(0x3c);
  const nt = await rd(base + BigInt(peOff), 0x108);
  const pdataEntries = nt.readUInt32LE(0x18 + 0x70 + 3 * 8 + 4) / 12;
  const r = await call('recover_functions', { pid: t.pid, module_base: t.main.base }, { timeout: 120000 });
  const fromPdata = r.summary.from_pdata;
  assert.ok(fromPdata >= pdataEntries * 0.8, `only ${fromPdata} of ${pdataEntries} .pdata entries recovered`);
});

test('get_function_containing(entry): start <= entry < start+size', async () => {
  const r = await call('get_function_containing', { pid: t.pid, address: hex(entry) });
  assert.equal(r.found, true, 'entry not inside any recovered function');
  funcStart = BigInt(r.function_start);
  funcSize = BigInt(r.size || 0);
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

// A function with exact bounds taken straight from the module's .pdata table (the first
// RUNTIME_FUNCTION on a readable page), so these checks never depend on the entry point's
// .pdata page being resident on the target.
let pdataFn;
const pdataFunction = async () => {
  if (pdataFn) return pdataFn;
  const rd = async (addr, size) => Buffer.from((await call('read_memory', { pid: t.pid, address: hex(addr), size, format: 'hex' })).hex, 'hex');
  const base = BigInt(t.main.base);
  const peOff = (await rd(base, 64)).readUInt32LE(0x3c);
  const nt = await rd(base + BigInt(peOff), 0x108);
  const rva = nt.readUInt32LE(0x18 + 0x70 + 3 * 8);
  const size = nt.readUInt32LE(0x18 + 0x70 + 3 * 8 + 4);
  for (let off = 0; off < size; off += 4096) {
    let page;
    try { page = await rd(base + BigInt(rva + off), Math.min(4096, size - off)); } catch { continue; }
    // Skip the first few entries so the next one is preceded by other functions.
    for (let i = 8 * 12; i + 12 <= page.length; i += 12) {
      const begin = page.readUInt32LE(i), end = page.readUInt32LE(i + 4);
      if (begin && end > begin + 8) return (pdataFn = { start: base + BigInt(begin), end: base + BigInt(end) });
    }
  }
  return null;
};

test('find_function_bounds returns the exact .pdata range once functions are recovered', async (ctx) => {
  const f = await pdataFunction();
  if (!f) return ctx.skip('no readable .pdata page');
  const mid = f.start + (f.end - f.start) / 2n;
  const r = await call('find_function_bounds', { pid: t.pid, address: hex(mid) });
  assert.equal(BigInt(r.function_start), f.start, `start ${r.function_start} != .pdata ${hex(f.start)} (reason ${r.start_reason})`);
  assert.equal(BigInt(r.function_end), f.end, `end ${r.function_end} != .pdata ${hex(f.end)} (reason ${r.end_reason})`);
  assert.equal(r.confidence, 'high');
});

test('find_function_bounds end is an instruction boundary (not a stray 0xC3 byte)', async () => {
  // Inside the entry function, so the cache path cannot hide the heuristic: use an address
  // in a function the heuristic must handle, then check the end lands on a decoded boundary.
  const r = await call('find_function_bounds', { pid: t.pid, address: hex(entry) });
  if (r.function_end === undefined) return;
  const start = BigInt(r.function_start ?? entry);
  const end = BigInt(r.function_end);
  const d = await call('disassemble', { pid: t.pid, address: hex(start), count: 1000 }); // server max
  const ends = new Set(d.instructions.map((i) => BigInt(i.addr) + BigInt(i.bytes.split(' ').length)));
  assert.ok(ends.has(end), `function_end ${hex(end)} is not the end of any instruction decoded from ${hex(start)}`);
});

test('get_function_containing just past a .pdata function never returns an earlier function', async (ctx) => {
  const f = await pdataFunction();
  if (!f) return ctx.skip('no readable .pdata page');
  const r = await call('get_function_containing', { pid: t.pid, address: hex(f.end) });
  if (!r.found) return; // address is in padding between functions: correct
  const s = BigInt(r.function_start);
  assert.ok(s >= f.end, `returned function ${r.function_start} starts before ${hex(f.end)}, i.e. an earlier function was matched across a gap`);
  if (r.size > 0) assert.ok(f.end < s + BigInt(r.size));
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
