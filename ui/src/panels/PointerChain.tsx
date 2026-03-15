import { useState, useMemo, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { usePointerChain } from '../hooks/usePointerChain';
import { useConnection } from '../hooks/useConnection';

interface PointerChainProps {
  onNavigate: (panel: string, address?: string) => void;
}

function PointerChain({ onNavigate }: PointerChainProps) {
  const { connected, health } = useConnection();
  const pid = health?.pid;
  const { chain, loading, error, resolve } = usePointerChain();

  const [baseAddress, setBaseAddress] = useState('');
  const [offsetsInput, setOffsetsInput] = useState('');

  const handleResolve = useCallback(() => {
    if (!pid || !baseAddress.trim()) return;
    const base = baseAddress.trim();

    // Parse comma-separated hex offsets
    const offsets = offsetsInput
      .split(',')
      .map((s) => s.trim())
      .filter((s) => s.length > 0)
      .map((s) => {
        if (s.startsWith('0x') || s.startsWith('0X')) {
          return parseInt(s, 16);
        }
        return parseInt(s, 16);
      })
      .filter((n) => !isNaN(n));

    resolve(pid, base, offsets);
  }, [pid, baseAddress, offsetsInput, resolve]);

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.key === 'Enter') {
        handleResolve();
      }
    },
    [handleResolve]
  );

  const handleAddressClick = useCallback(
    (address: string) => {
      onNavigate('memory', address);
    },
    [onNavigate]
  );

  // Interpret final value bytes
  const interpretations = useMemo(() => {
    if (!chain || !chain.final_value) return null;

    try {
      const value = BigInt(chain.final_value);
      const buffer = new ArrayBuffer(8);
      const view = new DataView(buffer);

      // Write as little-endian 64-bit
      view.setBigUint64(0, value, true);

      return {
        int32: view.getInt32(0, true),
        uint32: view.getUint32(0, true),
        int64: view.getBigInt64(0, true).toString(),
        float32: view.getFloat32(0, true),
        float64: view.getFloat64(0, true),
      };
    } catch {
      return null;
    }
  }, [chain]);

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
              Pointer Chain
            </h1>
            {chain && chain.steps.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {chain.steps.length} steps
              </span>
            )}
          </div>
        </div>

        {/* Input section */}
        <div className="space-y-2">
          {/* Base address */}
          <div className="flex items-center gap-2">
            <span className="text-[10px] uppercase shrink-0 w-14" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Base
            </span>
            <input
              type="text"
              placeholder="client.dll+0x17B8E8 or 0x7FF600001000"
              value={baseAddress}
              onChange={(e) => setBaseAddress(e.target.value)}
              onKeyDown={handleKeyDown}
              className="flex-1 h-9 px-3 rounded-lg text-sm font-mono outline-none"
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
          {/* Offsets */}
          <div className="flex items-center gap-2">
            <span className="text-[10px] uppercase shrink-0 w-14" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Offsets
            </span>
            <input
              type="text"
              placeholder="0x10, 0x20, 0x8"
              value={offsetsInput}
              onChange={(e) => setOffsetsInput(e.target.value)}
              onKeyDown={handleKeyDown}
              className="flex-1 h-9 px-3 rounded-lg text-sm font-mono outline-none"
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
            <button
              onClick={handleResolve}
              disabled={loading || !pid || !baseAddress.trim()}
              className="px-4 h-9 rounded-lg text-sm cursor-pointer border-none outline-none disabled:opacity-40"
              style={{
                fontWeight: 500,
                background: 'var(--surface)',
                color: 'var(--text)',
                border: '1px solid var(--border)',
                transition: 'all 0.1s ease',
              }}
              onMouseEnter={(e) => {
                e.currentTarget.style.background = 'var(--hover)';
                e.currentTarget.style.borderColor = 'var(--text-muted)';
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.background = 'var(--surface)';
                e.currentTarget.style.borderColor = 'var(--border)';
              }}
            >
              Resolve
            </button>
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

      {/* Chain visualization */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {!connected || !pid ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u2192'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Attach to a process to resolve pointers</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Select a process from the Processes panel</p>
          </motion.div>
        ) : !chain ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u2192'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter a base address and offsets to resolve</p>
            <p className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>
              Example: client.dll+0x17B8E8 with offsets 0x10, 0x20, 0x8
            </p>
          </motion.div>
        ) : (
          <div className="space-y-6">
            {/* Chain steps */}
            <div
              className="rounded-lg overflow-hidden"
              style={{
                background: 'var(--surface)',
                border: '1px solid var(--border)',
              }}
            >
              {chain.steps.map((step, index) => (
                <motion.div
                  key={index}
                  className="flex items-center px-4 font-mono text-xs"
                  style={{
                    height: 36,
                    borderBottom: index < chain.steps.length - 1 ? '1px solid var(--border)' : 'none',
                    background: index % 2 === 1 ? 'var(--hover)' : 'transparent',
                  }}
                  initial={{ opacity: 0, x: -4 }}
                  animate={{ opacity: 1, x: 0 }}
                  transition={{
                    duration: 0.1,
                    ease: 'easeOut',
                    delay: index * 0.04,
                  }}
                >
                  {/* Label */}
                  <span
                    className="shrink-0 tabular-nums"
                    style={{ width: 80, color: 'var(--text-muted)' }}
                  >
                    {index === 0 ? 'Base:' : `+0x${step.offset.toString(16).toUpperCase()}:`}
                  </span>
                  {/* Address being read */}
                  <span className="shrink-0" style={{ width: 8, color: 'var(--text-muted)' }}>[</span>
                  <button
                    onClick={() => handleAddressClick(step.address)}
                    className="shrink-0 tabular-nums cursor-pointer border-none outline-none bg-transparent p-0 font-mono text-xs"
                    style={{
                      color: 'var(--text-secondary)',
                      transition: 'color 0.1s ease',
                    }}
                    onMouseEnter={(e) => {
                      e.currentTarget.style.color = 'var(--text)';
                    }}
                    onMouseLeave={(e) => {
                      e.currentTarget.style.color = 'var(--text-secondary)';
                    }}
                  >
                    {step.address}
                  </button>
                  <span className="shrink-0" style={{ width: 8, color: 'var(--text-muted)' }}>]</span>
                  {/* Arrow */}
                  <span className="shrink-0 mx-3" style={{ color: 'var(--text-muted)' }}>{'\u2192'}</span>
                  {/* Value */}
                  {step.success ? (
                    <span
                      className="tabular-nums"
                      style={{
                        color: index === chain.steps.length - 1 ? 'var(--text)' : 'var(--text-secondary)',
                        fontWeight: index === chain.steps.length - 1 ? 500 : 400,
                      }}
                    >
                      {step.value}
                      {index === chain.steps.length - 1 && (
                        <span className="ml-3" style={{ color: 'var(--text-muted)', fontWeight: 400 }}>
                          (Final)
                        </span>
                      )}
                    </span>
                  ) : (
                    <span style={{ color: 'var(--text-muted)' }}>Failed to read</span>
                  )}
                </motion.div>
              ))}

              {/* Loading overlay */}
              <AnimatePresence>
                {loading && (
                  <motion.div
                    className="flex items-center justify-center py-4"
                    initial={{ opacity: 0 }}
                    animate={{ opacity: 1 }}
                    exit={{ opacity: 0 }}
                    transition={{ duration: 0.1 }}
                  >
                    <span className="text-xs" style={{ color: 'var(--text-muted)' }}>Resolving chain...</span>
                  </motion.div>
                )}
              </AnimatePresence>
            </div>

            {/* Final value interpretation */}
            {interpretations && (
              <motion.div
                className="rounded-lg p-4"
                style={{
                  background: 'var(--surface)',
                  border: '1px solid var(--border)',
                }}
                initial={{ opacity: 0, y: 4 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ duration: 0.15, delay: 0.1 }}
              >
                <span
                  className="text-[10px] uppercase block mb-3"
                  style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
                >
                  Value Interpretation
                </span>
                <div className="grid grid-cols-2 gap-x-6 gap-y-1.5">
                  {([
                    ['Int32', interpretations.int32.toString()],
                    ['UInt32', interpretations.uint32.toString()],
                    ['Int64', interpretations.int64],
                    ['Float', formatFloat(interpretations.float32)],
                    ['Double', formatFloat(interpretations.float64)],
                    ['Hex', chain.final_value],
                  ] as [string, string][]).map(([label, value]) => (
                    <div key={label} className="flex items-center gap-3">
                      <span
                        className="text-xs shrink-0"
                        style={{ color: 'var(--text-secondary)', width: 48 }}
                      >
                        {label}
                      </span>
                      <span
                        className="text-xs font-mono tabular-nums"
                        style={{ color: 'var(--text)', userSelect: 'text' }}
                      >
                        {value}
                      </span>
                    </div>
                  ))}
                </div>
              </motion.div>
            )}
          </div>
        )}
      </div>
    </div>
  );
}

function formatFloat(value: number): string {
  if (!isFinite(value)) return value.toString();
  // Show enough precision but not excessive
  const str = value.toPrecision(7);
  // Remove trailing zeros after decimal
  return str.includes('.') ? str.replace(/\.?0+$/, '') : str;
}

export default PointerChain;
