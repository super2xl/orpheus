// Tiny Orpheus MCP HTTP client for scenario tests. Node 18+, zero dependencies.
import assert from 'node:assert/strict';

export const BASE = (process.env.ORPHEUS_MCP_URL || 'http://127.0.0.1:8765').replace(/\/+$/, '');
export const API_KEY = process.env.ORPHEUS_API_KEY || '';
export const DEFAULT_TIMEOUT = Number(process.env.ORPHEUS_TIMEOUT_MS || 30000);
export const TARGET = process.env.ORPHEUS_TARGET_PROCESS || 'explorer.exe';

export class OrpheusError extends Error {
  // kind: 'server' (server answered with an error), 'timeout', 'network', 'protocol' (non-JSON body)
  constructor(message, kind, status) {
    super(message);
    this.name = 'OrpheusError';
    this.kind = kind;
    this.status = status;
  }
}

// Bridge-style tool names whose HTTP route differs from /tools/<name> (or is GET).
const ROUTES = {
  get_processes: ['GET', '/tools/processes'],
  get_modules: ['POST', '/tools/modules'],
  dma_status: ['GET', '/tools/dma_status'],
  mcp_info: ['GET', '/tools/mcp_info'],
};

export async function request(method, path, body, { timeout = DEFAULT_TIMEOUT } = {}) {
  const headers = { 'Content-Type': 'application/json' };
  if (API_KEY) headers.Authorization = `Bearer ${API_KEY}`;
  let res, text;
  try {
    res = await fetch(BASE + path, {
      method,
      headers,
      body: method === 'POST' ? JSON.stringify(body ?? {}) : undefined,
      signal: AbortSignal.timeout(timeout),
    });
    text = await res.text();
  } catch (e) {
    if (e?.name === 'TimeoutError' || e?.name === 'AbortError') {
      throw new OrpheusError(`${method} ${path}: timed out after ${timeout}ms`, 'timeout');
    }
    throw new OrpheusError(`${method} ${path}: network error: ${e?.cause?.code || e?.message}`, 'network');
  }
  let json;
  try {
    json = JSON.parse(text);
  } catch {
    throw new OrpheusError(`${method} ${path}: HTTP ${res.status}, non-JSON body: ${text.slice(0, 120)}`, 'protocol', res.status);
  }
  if (!res.ok || json?.success === false) {
    const msg = json?.error || json?.message || `HTTP ${res.status}`;
    throw new OrpheusError(`${method} ${path}: ${msg}`, 'server', res.status);
  }
  // Most handlers wrap as {success:true,data:{...}}; some (dma_status, mcp_info, task_*) return the object bare.
  return json && typeof json === 'object' && 'data' in json && 'success' in json ? json.data : json;
}

export const get = (path, opts) => request('GET', path, undefined, opts);
export const post = (path, body, opts) => request('POST', path, body, opts);

export function call(tool, body = {}, opts) {
  const [method, path] = ROUTES[tool] || ['POST', `/tools/${tool}`];
  return request(method, path, body, opts);
}

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export async function poll(fn, { timeout = 60000, interval = 250, label = 'condition' } = {}) {
  const end = Date.now() + timeout;
  for (;;) {
    const v = await fn();
    if (v) return v;
    if (Date.now() > end) throw new Error(`timed out after ${timeout}ms waiting for ${label}`);
    await sleep(interval);
  }
}

// Assert the promise rejects with a clean *server-reported* error (not a hang/timeout/network drop).
export async function expectServerError(promise, pattern) {
  let err;
  try {
    await promise;
  } catch (e) {
    err = e;
  }
  assert.ok(err, 'expected the call to fail, but it succeeded');
  assert.equal(err.kind, 'server', `expected a clean server error, got ${err.kind}: ${err.message}`);
  if (pattern) assert.match(err.message, pattern);
  return err;
}

export async function assertAlive() {
  const h = await get('/health', { timeout: 5000 });
  assert.equal(h.status, 'ok', 'server did not answer /health after the negative case');
}

export const inRange = (addr, base, size) => BigInt(addr) >= BigInt(base) && BigInt(addr) < BigInt(base) + BigInt(size);

// Locate target process + its main module (shared by journeys 2 and 3).
export async function resolveTarget() {
  const procs = (await call('get_processes')).processes;
  const p = procs.find((x) => x.name.toLowerCase() === TARGET.toLowerCase());
  if (!p) throw new Error(`process "${TARGET}" not found (set ORPHEUS_TARGET_PROCESS); ${procs.length} processes visible`);
  const mods = (await call('get_modules', { pid: p.pid })).modules;
  const main = mods.find((m) => m.name.toLowerCase() === p.name.toLowerCase()) || mods[0];
  if (!main) throw new Error(`no modules returned for ${p.name} (pid ${p.pid})`);
  return { pid: p.pid, name: p.name, modules: mods, main };
}
