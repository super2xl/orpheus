// Journey 1: server is up, rig is connected, MCP info is retrievable.
import test from 'node:test';
import assert from 'node:assert/strict';
import { get, post, poll, call } from './lib.mjs';

const DEVICE = process.env.ORPHEUS_DEVICE || 'fpga';
const CONNECT_TIMEOUT = Number(process.env.ORPHEUS_CONNECT_TIMEOUT_MS || 60000);

test('GET /health', async () => {
  const h = await get('/health');
  assert.equal(h.status, 'ok');
  assert.equal(typeof h.version, 'string');
});

test('GET /version', async () => {
  const v = await get('/version');
  assert.match(v.version, /^\d+\.\d+\.\d+/);
  assert.equal(typeof v.version_major, 'number');
});

test('DMA connected (connect if needed)', async () => {
  let s = await call('dma_status');
  assert.equal(typeof s.connected, 'boolean');
  if (!s.connected && !s.connecting) {
    // Returns immediately with connecting:true; the real work happens on a background thread.
    await post('/tools/connect_dma', { device_type: DEVICE });
  }
  s = await poll(
    async () => {
      const st = await call('dma_status');
      if (st.connected) return st;
      if (!st.connecting && st.error) throw new Error(`DMA connect failed: ${st.error}`);
      return null;
    },
    { timeout: CONNECT_TIMEOUT, interval: 500, label: 'dma_status.connected' },
  );
  assert.equal(s.connected, true);
  assert.ok(s.device_type, 'connected status should name the device type');
});

test('GET /tools/mcp_info', async () => {
  const i = await call('mcp_info');
  assert.equal(typeof i.port, 'number');
  assert.match(i.url, /^http:\/\//);
  assert.equal(typeof i.api_key, 'string');
});
