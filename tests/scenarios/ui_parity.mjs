// Offline static check: every endpoint the UI and the MCP bridge call must be registered by the server
// (same path AND same HTTP method). Server-only routes are informational. No server needed.
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, dirname, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const rel = (p) => relative(ROOT, p).replaceAll('\\', '/');

function walk(dir, exts, out = []) {
  for (const name of readdirSync(dir)) {
    if (name === 'node_modules' || name === 'dist' || name === 'target') continue;
    const p = join(dir, name);
    if (statSync(p).isDirectory()) walk(p, exts, out);
    else if (exts.some((e) => name.endsWith(e))) out.push(p);
  }
  return out;
}

const lineOf = (text, idx) => text.slice(0, idx).split('\n').length;

// ---- server routes: key "METHOD /path" -> where registered ----
const server = new Map();
for (const f of walk(join(ROOT, 'src', 'mcp'), ['.cpp'])) {
  const src = readFileSync(f, 'utf8');
  for (const m of src.matchAll(/ROUTE_(POST|GET)(?:_PERM)?\(\s*"([^"]+)"/g)) server.set(`${m[1]} ${m[2]}`, `${rel(f)}:${lineOf(src, m.index)}`);
  for (const m of src.matchAll(/server->(Get|Post)\(\s*"([^"]+)"/g)) server.set(`${m[1].toUpperCase()} ${m[2]}`, `${rel(f)}:${lineOf(src, m.index)}`);
}
const serverPaths = new Set([...server.keys()].map((k) => k.split(' ')[1]));

// ---- UI: orpheus.request(endpoint, body?) -> GET when no body argument, POST otherwise (see ui/src/api/client.ts) ----
const ui = new Map(); // key -> [locations]
const uiNotes = [];
const add = (map, key, loc) => map.set(key, [...(map.get(key) || []), loc]);
const uiFiles = walk(join(ROOT, 'ui', 'src'), ['.ts', '.tsx']);
for (const f of uiFiles) {
  const src = readFileSync(f, 'utf8');
  const isClient = rel(f) === 'ui/src/api/client.ts';
  for (const m of src.matchAll(/\.request(?:<[\s\S]*?>)?\(\s*(['"`])([^'"`]+)\1\s*([,)])/g)) {
    if (m[2].includes('${')) { uiNotes.push(`dynamic endpoint not checked: ${rel(f)}:${lineOf(src, m.index)} ${m[2]}`); continue; }
    add(ui, `${m[3] === ',' ? 'POST' : 'GET'} /${m[2].replace(/^\/+/, '')}`, `${rel(f)}:${lineOf(src, m.index)}`);
  }
  if (!isClient) {
    for (const m of src.matchAll(/\bfetch\(/g)) uiNotes.push(`raw fetch() bypasses client, not checked: ${rel(f)}:${lineOf(src, m.index)}`);
    for (const m of src.matchAll(/\.request\(\s*[^'"`\s]/g)) uiNotes.push(`non-literal request() endpoint not checked: ${rel(f)}:${lineOf(src, m.index)}`);
  }
  for (const m of src.matchAll(/invoke(?:<[^>]*>)?\(\s*['"]([^'"]+)['"]/g)) {
    if (m[1] !== 'get_api_key') uiNotes.push(`tauri invoke('${m[1]}') not an HTTP route, not checked: ${rel(f)}:${lineOf(src, m.index)}`);
  }
}
// Tauri backend: does it talk to the HTTP server itself?
for (const f of walk(join(ROOT, 'ui', 'src-tauri', 'src'), ['.rs'])) {
  const src = readFileSync(f, 'utf8');
  for (const m of src.matchAll(/["']\/?tools\/[a-z_/]+["']|reqwest|ureq/g)) uiNotes.push(`tauri backend HTTP use, not checked: ${rel(f)}:${lineOf(src, m.index)} ${m[0]}`);
}

// ---- bridge: TOOL_ENDPOINT_MAP + MCP_TOOLS names ----
const bridgeFile = join(ROOT, 'mcp_bridge.js');
const bsrc = readFileSync(bridgeFile, 'utf8');
const mapStart = bsrc.indexOf('const TOOL_ENDPOINT_MAP');
const toolsStart = bsrc.indexOf('const MCP_TOOLS');
const bridge = new Map();
const bridgeTools = new Map();
for (const m of bsrc.slice(mapStart).matchAll(/'([\w]+)':\s*\{\s*method:\s*'(GET|POST)',\s*path:\s*'([^']+)'/g)) {
  bridgeTools.set(m[1], `${m[2]} ${m[3]}`);
  add(bridge, `${m[2]} ${m[3]}`, `${rel(bridgeFile)}:${lineOf(bsrc, mapStart + m.index)} (${m[1]})`);
}
const declared = new Set([...bsrc.slice(toolsStart, mapStart).matchAll(/^\s{4}name:\s*'([\w]+)'/gm)].map((m) => m[1]));

// ---- compare ----
const failures = [];
function check(label, calls) {
  for (const [key, locs] of calls) {
    if (server.has(key)) continue;
    const path = key.split(' ')[1];
    const why = serverPaths.has(path)
      ? `path exists but not for ${key.split(' ')[0]} (server has: ${[...server.keys()].filter((k) => k.endsWith(' ' + path)).join(', ')})`
      : 'path not registered on server';
    failures.push(`${label} calls ${key}: ${why}  [${locs.join('; ')}]`);
  }
}
check('UI', ui);
check('bridge', bridge);
for (const name of declared) if (!bridgeTools.has(name)) failures.push(`bridge declares tool "${name}" but TOOL_ENDPOINT_MAP has no route for it (calls would fail "Unknown tool")`);

const called = new Set([...ui.keys(), ...bridge.keys()]);
const serverOnly = [...server.keys()].filter((k) => !called.has(k)).sort();
const undeclared = [...bridgeTools.keys()].filter((n) => !declared.has(n));

console.log(`server routes: ${server.size} | UI call sites: ${[...ui.values()].flat().length} (${ui.size} unique) | bridge routes: ${bridge.size} | bridge tools declared: ${declared.size}`);
if (uiNotes.length) { console.log('\nINFO unchecked UI call sites:'); uiNotes.forEach((n) => console.log('  ' + n)); }
if (undeclared.length) console.log(`\nINFO bridge routes with no MCP tool declaration (unreachable by AI clients): ${undeclared.join(', ')}`);
console.log(`\nINFO server-only routes (not used by UI or bridge): ${serverOnly.length}`);
serverOnly.forEach((k) => console.log('  ' + k));
if (failures.length) {
  console.log(`\nFAIL ${failures.length} mismatch(es):`);
  failures.forEach((f) => console.log('  ' + f));
  process.exit(1);
}
console.log('\nPASS: every UI and bridge endpoint is registered on the server');
