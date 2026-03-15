import { useState, useCallback, useEffect, useRef } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useEmulator } from '../hooks/useEmulator';
import { useModules } from '../hooks/useModules';
import { useConnection } from '../hooks/useConnection';
import { useContextMenu } from '../hooks/useContextMenu';
import ContextMenu from '../components/ContextMenu';
import { copyToClipboard } from '../utils/clipboard';

const GPR_NAMES = [
  'rax', 'rbx', 'rcx', 'rdx', 'rsi', 'rdi', 'rbp', 'rsp',
  'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15',
];

const SPECIAL_NAMES = ['rip', 'rflags'];

const ALL_REGISTERS = [...GPR_NAMES, ...SPECIAL_NAMES];

function RegisterCell({
  name,
  value,
  changed,
  onCommit,
}: {
  name: string;
  value: string;
  changed: boolean;
  onCommit: (name: string, value: string) => void;
}) {
  const [editing, setEditing] = useState(false);
  const [editValue, setEditValue] = useState(value);
  const inputRef = useRef<HTMLInputElement>(null);
  const flashRef = useRef<HTMLDivElement>(null);

  // Update editValue when value changes externally
  useEffect(() => {
    if (!editing) {
      setEditValue(value);
    }
  }, [value, editing]);

  // Flash animation on change
  useEffect(() => {
    if (changed && flashRef.current) {
      flashRef.current.style.background = 'var(--active)';
      const timeout = setTimeout(() => {
        if (flashRef.current) {
          flashRef.current.style.background = 'transparent';
        }
      }, 800);
      return () => clearTimeout(timeout);
    }
  }, [changed, value]);

  const handleStartEdit = () => {
    setEditing(true);
    setEditValue(value);
    setTimeout(() => inputRef.current?.select(), 0);
  };

  const handleCommit = () => {
    setEditing(false);
    const trimmed = editValue.trim();
    if (trimmed && trimmed !== value) {
      onCommit(name, trimmed);
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleCommit();
    } else if (e.key === 'Escape') {
      setEditing(false);
      setEditValue(value);
    }
  };

  return (
    <div
      ref={flashRef}
      className="flex items-center gap-2 h-7 px-2 rounded"
      style={{
        transition: 'background 0.4s ease',
      }}
    >
      <span
        className="font-mono text-[10px] uppercase shrink-0"
        style={{
          width: 48,
          color: 'var(--text-muted)',
          letterSpacing: '0.04em',
          fontWeight: 400,
        }}
      >
        {name}
      </span>
      {editing ? (
        <input
          ref={inputRef}
          type="text"
          value={editValue}
          onChange={(e) => setEditValue(e.target.value)}
          onBlur={handleCommit}
          onKeyDown={handleKeyDown}
          className="flex-1 h-5 px-1 rounded font-mono text-xs outline-none"
          style={{
            background: 'var(--bg)',
            border: '1px solid var(--text-muted)',
            color: 'var(--text)',
          }}
          autoFocus
        />
      ) : (
        <span
          className="flex-1 font-mono text-xs tabular-nums truncate cursor-pointer"
          style={{
            color: changed ? 'var(--text)' : 'var(--text-secondary)',
            fontWeight: changed ? 500 : 400,
          }}
          onClick={handleStartEdit}
          title="Click to edit"
        >
          {value}
        </span>
      )}
    </div>
  );
}

function Emulator({ onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { connected, health } = useConnection();
  const pid = health?.pid;
  const {
    created, registers, result, status, loading, error, changedRegs,
    create, destroy, mapModule, setRegister, run, runInstructions, reset,
  } = useEmulator();
  const { modules, refresh: refreshModules } = useModules();

  const [startAddress, setStartAddress] = useState('');
  const [endAddress, setEndAddress] = useState('');
  const [instrCount, setInstrCount] = useState(100);
  const [useEndAddress, setUseEndAddress] = useState(true);
  const [selectedModule, setSelectedModule] = useState('');
  const { menu, show: showContextMenu, close: closeContextMenu } = useContextMenu();

  // Fetch modules when connected
  useEffect(() => {
    if (connected && pid) {
      refreshModules(pid);
    }
  }, [connected, pid, refreshModules]);

  const handleCreate = useCallback(async () => {
    if (!pid) return;
    await create(pid);
  }, [pid, create]);

  const handleDestroy = useCallback(async () => {
    await destroy();
  }, [destroy]);

  const handleRun = useCallback(async () => {
    if (useEndAddress) {
      if (!startAddress.trim() || !endAddress.trim()) return;
      await run(startAddress.trim(), endAddress.trim());
    } else {
      if (!startAddress.trim()) return;
      await runInstructions(startAddress.trim(), instrCount);
    }
  }, [useEndAddress, startAddress, endAddress, instrCount, run, runInstructions]);

  const handleStep = useCallback(async () => {
    if (!startAddress.trim()) return;
    await runInstructions(startAddress.trim(), 1);
  }, [startAddress, runInstructions]);

  const handleReset = useCallback(async () => {
    await reset();
  }, [reset]);

  const handleMapModule = useCallback(async () => {
    if (!selectedModule) return;
    await mapModule(selectedModule);
  }, [selectedModule, mapModule]);

  const handleRegisterCommit = useCallback(async (name: string, value: string) => {
    await setRegister(name, value);
  }, [setRegister]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && created) {
      handleRun();
    }
  }, [created, handleRun]);

  // Status display color
  const getStatusColor = () => {
    switch (status) {
      case 'Ready': return 'var(--text-secondary)';
      case 'Running': return 'var(--text)';
      case 'Completed': return 'var(--text)';
      case 'Error': return 'var(--text-secondary)';
      default: return 'var(--text-muted)';
    }
  };

  return (
    <div className="h-full flex flex-col">
      {/* Header */}
      <motion.div
        className="shrink-0 px-6 pt-6 pb-4 space-y-4"
        initial={{ opacity: 0, y: -8 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.15, ease: 'easeOut' }}
      >
        {/* Title row */}
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <h1 className="text-lg tracking-tight" style={{ color: 'var(--text)', fontWeight: 500 }}>
              Emulator
            </h1>
            <span
              className="text-xs px-2 py-0.5 rounded-md font-mono"
              style={{
                color: getStatusColor(),
                background: 'var(--active)',
              }}
            >
              {status}
            </span>
          </div>
        </div>

        {/* Controls */}
        <div className="space-y-3">
          {/* Start address */}
          <div className="flex items-center gap-2">
            <span className="text-[10px] uppercase shrink-0" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400, width: 48 }}>
              Start
            </span>
            <input
              type="text"
              placeholder="0x7FF600001000"
              value={startAddress}
              onChange={(e) => setStartAddress(e.target.value)}
              onKeyDown={handleKeyDown}
              className="flex-1 h-8 px-3 rounded-lg text-xs font-mono outline-none"
              style={{
                background: 'var(--surface)',
                border: '1px solid var(--border)',
                color: 'var(--text)',
                transition: 'border-color 0.1s ease',
              }}
              onFocus={(e) => {
                e.currentTarget.style.borderColor = 'var(--text-muted)';
              }}
              onBlur={(e) => {
                e.currentTarget.style.borderColor = 'var(--border)';
              }}
            />
          </div>

          {/* End address / instruction count toggle */}
          <div className="flex items-center gap-2">
            <button
              onClick={() => setUseEndAddress(!useEndAddress)}
              className="text-[10px] uppercase shrink-0 cursor-pointer border-none outline-none"
              style={{
                background: 'transparent',
                color: 'var(--text-muted)',
                letterSpacing: '0.08em',
                fontWeight: 400,
                width: 48,
                textAlign: 'left',
              }}
              title={`Switch to ${useEndAddress ? 'instruction count' : 'end address'}`}
            >
              {useEndAddress ? 'End' : 'Count'}
            </button>
            {useEndAddress ? (
              <input
                type="text"
                placeholder="0x7FF600002000"
                value={endAddress}
                onChange={(e) => setEndAddress(e.target.value)}
                onKeyDown={handleKeyDown}
                className="flex-1 h-8 px-3 rounded-lg text-xs font-mono outline-none"
                style={{
                  background: 'var(--surface)',
                  border: '1px solid var(--border)',
                  color: 'var(--text)',
                  transition: 'border-color 0.1s ease',
                }}
                onFocus={(e) => {
                  e.currentTarget.style.borderColor = 'var(--text-muted)';
                }}
                onBlur={(e) => {
                  e.currentTarget.style.borderColor = 'var(--border)';
                }}
              />
            ) : (
              <input
                type="number"
                min={1}
                max={100000}
                value={instrCount}
                onChange={(e) => setInstrCount(Math.max(1, parseInt(e.target.value) || 1))}
                onKeyDown={handleKeyDown}
                className="flex-1 h-8 px-3 rounded-lg text-xs font-mono outline-none"
                style={{
                  background: 'var(--surface)',
                  border: '1px solid var(--border)',
                  color: 'var(--text)',
                  transition: 'border-color 0.1s ease',
                }}
                onFocus={(e) => {
                  e.currentTarget.style.borderColor = 'var(--text-muted)';
                }}
                onBlur={(e) => {
                  e.currentTarget.style.borderColor = 'var(--border)';
                }}
              />
            )}
          </div>

          {/* Buttons row */}
          <div className="flex items-center gap-2">
            {!created ? (
              <button
                onClick={handleCreate}
                disabled={loading || !pid}
                className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none disabled:opacity-40"
                style={{
                  fontWeight: 400,
                  background: 'transparent',
                  color: 'var(--text-secondary)',
                  border: '1px solid var(--border)',
                  transition: 'all 0.1s ease',
                }}
                onMouseEnter={(e) => {
                  e.currentTarget.style.background = 'var(--hover)';
                  e.currentTarget.style.color = 'var(--text)';
                }}
                onMouseLeave={(e) => {
                  e.currentTarget.style.background = 'transparent';
                  e.currentTarget.style.color = 'var(--text-secondary)';
                }}
              >
                Create
              </button>
            ) : (
              <>
                <button
                  onClick={handleRun}
                  disabled={loading || !startAddress.trim()}
                  className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none disabled:opacity-40"
                  style={{
                    fontWeight: 400,
                    background: 'transparent',
                    color: 'var(--text-secondary)',
                    border: '1px solid var(--border)',
                    transition: 'all 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }}
                >
                  Run
                </button>
                <button
                  onClick={handleStep}
                  disabled={loading || !startAddress.trim()}
                  className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none disabled:opacity-40"
                  style={{
                    fontWeight: 400,
                    background: 'transparent',
                    color: 'var(--text-secondary)',
                    border: '1px solid var(--border)',
                    transition: 'all 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }}
                >
                  Step
                </button>
                <button
                  onClick={handleReset}
                  disabled={loading}
                  className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none disabled:opacity-40"
                  style={{
                    fontWeight: 400,
                    background: 'transparent',
                    color: 'var(--text-secondary)',
                    border: '1px solid var(--border)',
                    transition: 'all 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }}
                >
                  Reset
                </button>

                {/* Separator */}
                <div className="w-px h-4" style={{ background: 'var(--border)' }} />

                {/* Map module */}
                <select
                  value={selectedModule}
                  onChange={(e) => setSelectedModule(e.target.value)}
                  className="h-7 px-2 rounded-md text-xs font-mono cursor-pointer outline-none"
                  style={{
                    background: 'var(--surface)',
                    color: 'var(--text)',
                    border: '1px solid var(--border)',
                    transition: 'border-color 0.1s ease',
                  }}
                  onFocus={(e) => {
                    e.currentTarget.style.borderColor = 'var(--text-muted)';
                  }}
                  onBlur={(e) => {
                    e.currentTarget.style.borderColor = 'var(--border)';
                  }}
                >
                  <option value="">Map Module...</option>
                  {modules.map((m) => (
                    <option key={m.base_address} value={m.name}>
                      {m.name}
                    </option>
                  ))}
                </select>
                <button
                  onClick={handleMapModule}
                  disabled={loading || !selectedModule}
                  className="px-2.5 h-7 rounded-md text-xs cursor-pointer border-none outline-none disabled:opacity-40"
                  style={{
                    fontWeight: 400,
                    background: 'transparent',
                    color: 'var(--text-secondary)',
                    border: '1px solid var(--border)',
                    transition: 'all 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }}
                >
                  Map
                </button>

                {/* Separator */}
                <div className="w-px h-4" style={{ background: 'var(--border)' }} />

                {/* Destroy */}
                <button
                  onClick={handleDestroy}
                  disabled={loading}
                  className="px-2.5 h-7 rounded-md text-xs cursor-pointer border-none outline-none disabled:opacity-40"
                  style={{
                    fontWeight: 400,
                    background: 'transparent',
                    color: 'var(--text-muted)',
                    border: '1px solid var(--border)',
                    transition: 'all 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = 'var(--text-muted)';
                  }}
                >
                  Destroy
                </button>
              </>
            )}
          </div>
        </div>
      </motion.div>

      {/* Error banner */}
      <AnimatePresence>
        {error && (
          <motion.div
            className="mx-6 mb-2 px-3 py-2 rounded-lg text-xs"
            style={{
              background: 'var(--surface)',
              border: '1px solid var(--border)',
              color: 'var(--text-secondary)',
            }}
            initial={{ opacity: 0, height: 0 }}
            animate={{ opacity: 1, height: 'auto' }}
            exit={{ opacity: 0, height: 0 }}
            transition={{ duration: 0.12 }}
          >
            {error}
          </motion.div>
        )}
      </AnimatePresence>

      {/* Main content */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {!connected || !pid ? (
          /* Empty state: not connected */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u25B6'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Connect to Orpheus to use the emulator</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Attach to a process first</p>
          </motion.div>
        ) : !created ? (
          /* Empty state: not created */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u25B6'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Create an emulator instance to start</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Set addresses and click Create</p>
          </motion.div>
        ) : (
          <div className="space-y-4">
            {/* Registers section */}
            <div
              className="rounded-lg overflow-hidden"
              style={{
                background: 'var(--surface)',
                border: '1px solid var(--border)',
              }}
            >
              {/* Registers header */}
              <div
                className="flex items-center px-3 h-7 text-[10px] font-mono uppercase select-none"
                style={{
                  color: 'var(--text-muted)',
                  borderBottom: '1px solid var(--border)',
                  letterSpacing: '0.08em',
                  fontWeight: 400,
                }}
              >
                <span>Registers</span>
              </div>

              {/* 2-column grid of registers */}
              <div className="grid grid-cols-2 gap-x-4 p-2">
                {ALL_REGISTERS.map((name) => {
                  const regValue = registers[name] || '0x0000000000000000';
                  return (
                    <div
                      key={name}
                      onContextMenu={(e) => showContextMenu(e, [
                        { label: 'View in Memory', action: () => onNavigate?.('memory', regValue) },
                        { label: 'View in Disassembly', action: () => onNavigate?.('disassembly', regValue) },
                        { label: 'Copy Value', action: () => copyToClipboard(regValue), separator: true },
                      ])}
                    >
                      <RegisterCell
                        name={name}
                        value={regValue}
                        changed={changedRegs.has(name)}
                        onCommit={handleRegisterCommit}
                      />
                    </div>
                  );
                })}
              </div>
            </div>

            {/* Output section */}
            {result && (
              <motion.div
                className="rounded-lg overflow-hidden"
                style={{
                  background: 'var(--surface)',
                  border: '1px solid var(--border)',
                }}
                initial={{ opacity: 0, y: 4 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ duration: 0.12, ease: 'easeOut' }}
              >
                {/* Output header */}
                <div
                  className="flex items-center px-3 h-7 text-[10px] font-mono uppercase select-none"
                  style={{
                    color: 'var(--text-muted)',
                    borderBottom: '1px solid var(--border)',
                    letterSpacing: '0.08em',
                    fontWeight: 400,
                  }}
                >
                  <span>Execution Result</span>
                </div>

                <div className="p-3 space-y-2">
                  {/* Status */}
                  <div className="flex items-center gap-2">
                    <span
                      className="text-[10px] uppercase"
                      style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
                    >
                      Status
                    </span>
                    <span
                      className="text-xs font-mono"
                      style={{ color: result.success ? 'var(--text)' : 'var(--text-secondary)' }}
                    >
                      {result.success ? 'Success' : 'Failed'}
                    </span>
                  </div>

                  {/* Instructions executed */}
                  <div className="flex items-center gap-2">
                    <span
                      className="text-[10px] uppercase"
                      style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
                    >
                      Executed
                    </span>
                    <span className="text-xs font-mono" style={{ color: 'var(--text-secondary)' }}>
                      {result.instructions_executed} instruction{result.instructions_executed !== 1 ? 's' : ''}
                    </span>
                  </div>

                  {/* Final RIP */}
                  <div className="flex items-center gap-2">
                    <span
                      className="text-[10px] uppercase"
                      style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
                    >
                      Final RIP
                    </span>
                    <span className="text-xs font-mono tabular-nums" style={{ color: 'var(--text-secondary)' }}>
                      {result.final_rip}
                    </span>
                  </div>

                  {/* Error */}
                  {result.error && (
                    <div className="flex items-start gap-2">
                      <span
                        className="text-[10px] uppercase shrink-0 pt-0.5"
                        style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
                      >
                        Error
                      </span>
                      <span className="text-xs font-mono" style={{ color: 'var(--text-secondary)' }}>
                        {result.error}
                      </span>
                    </div>
                  )}
                </div>
              </motion.div>
            )}
          </div>
        )}
      </div>

      {/* Context menu */}
      <AnimatePresence>
        {menu && (
          <ContextMenu
            x={menu.x}
            y={menu.y}
            items={menu.items}
            onClose={closeContextMenu}
          />
        )}
      </AnimatePresence>
    </div>
  );
}

export default Emulator;
