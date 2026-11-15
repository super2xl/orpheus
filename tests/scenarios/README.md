# Scenario tests

Live user-journey tests against a running Orpheus + DMA rig. Node 18+, no dependencies.

Prerequisites:
- Orpheus running as Administrator, rig attached, server on http://127.0.0.1:8765
- `%APPDATA%\Orpheus\config\mcp_config.json` has `"require_auth": false`
- A target process is running (default `explorer.exe`)

Env vars: `ORPHEUS_MCP_URL` (default http://127.0.0.1:8765), `ORPHEUS_API_KEY` (sent as Bearer if set),
`ORPHEUS_TARGET_PROCESS` (default explorer.exe), `ORPHEUS_DEVICE` (default fpga),
`ORPHEUS_TIMEOUT_MS`, `ORPHEUS_CONNECT_TIMEOUT_MS`, `ORPHEUS_TASK_TIMEOUT_MS`.

Journeys: 1 connect, 2 process/memory, 3 scan, 4 snapshot/diff (KUSER_SHARED_DATA), 5 disassemble/decompile/CFG.
Run everything: `node tests/scenarios/run.mjs` (ui_parity, then journeys 1-5; exits nonzero on failure).
Offline only: `node tests/scenarios/ui_parity.mjs` (no server or rig needed).
Single journey: `node tests/scenarios/journey4_snapshot.mjs`.
