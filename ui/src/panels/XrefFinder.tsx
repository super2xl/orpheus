import { useState, useEffect, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useXrefs } from '../hooks/useXrefs';
import { useModules } from '../hooks/useModules';
import { useConnection } from '../hooks/useConnection';
import type { ModuleInfo } from '../api/types';

function XrefFinder({ onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { connected, health } = useConnection();
  const pid = health?.pid;
  const { results, loading, error, find, clear } = useXrefs();
  const { modules, refresh: refreshModules } = useModules();

  const [address, setAddress] = useState('');
  const [selectedModule, setSelectedModule] = useState('');
  const [hasSearched, setHasSearched] = useState(false);

  // Fetch modules when connected
  useEffect(() => {
    if (connected && pid) {
      refreshModules(pid);
    }
  }, [connected, pid, refreshModules]);

  const getSelectedModuleInfo = useCallback((): ModuleInfo | undefined => {
    if (!selectedModule) return undefined;
    return modules.find((m) => m.name === selectedModule);
  }, [selectedModule, modules]);

  const handleFind = useCallback(() => {
    const input = address.trim();
    if (!input || !pid) return;
    setHasSearched(true);
    const mod = getSelectedModuleInfo();
    find(pid, input, mod?.base_address, mod?.size);
  }, [address, pid, find, getSelectedModuleInfo]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleFind();
    }
  }, [handleFind]);

  const handleAddressClick = useCallback((addr: string) => {
    if (onNavigate) {
      onNavigate('disassembly', addr);
    }
  }, [onNavigate]);

  const getMnemonicStyle = (mnemonic: string): { color: string } => {
    const lower = mnemonic.toLowerCase();
    if (lower === 'call') return { color: 'var(--text)' };
    if (lower === 'lea') return { color: 'var(--text)' };
    if (lower.startsWith('j')) return { color: 'var(--text)' };
    if (lower.startsWith('mov')) return { color: 'var(--text)' };
    return { color: 'var(--text)' };
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
              Xrefs
            </h1>
            {hasSearched && !loading && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {results.length} result{results.length !== 1 ? 's' : ''}
              </span>
            )}
          </div>
        </div>

        {/* Address input */}
        <div className="relative">
          <input
            type="text"
            placeholder="0x7FF6A1B2C3D4"
            value={address}
            onChange={(e) => setAddress(e.target.value)}
            onKeyDown={handleKeyDown}
            className="w-full h-10 px-3 rounded-lg font-mono outline-none"
            style={{
              fontSize: '0.9375rem',
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
          <AnimatePresence>
            {address && (
              <motion.button
                onClick={() => { setAddress(''); clear(); setHasSearched(false); }}
                className="absolute right-2 top-1/2 -translate-y-1/2 text-xs w-5 h-5 rounded flex items-center justify-center cursor-pointer border-none outline-none"
                style={{
                  color: 'var(--text-muted)',
                  background: 'transparent',
                  transition: 'color 0.1s ease',
                }}
                onMouseEnter={(e) => {
                  e.currentTarget.style.color = 'var(--text)';
                }}
                onMouseLeave={(e) => {
                  e.currentTarget.style.color = 'var(--text-muted)';
                }}
                initial={{ opacity: 0, scale: 0.8 }}
                animate={{ opacity: 1, scale: 1 }}
                exit={{ opacity: 0, scale: 0.8 }}
                transition={{ duration: 0.1 }}
              >
                {'\u2715'}
              </motion.button>
            )}
          </AnimatePresence>
        </div>

        {/* Controls row */}
        <div className="flex items-center gap-3">
          {/* Module selector */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Scope
            </span>
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
              <option value="">All Modules</option>
              {modules.map((m) => (
                <option key={m.base_address} value={m.name}>
                  {m.name}
                </option>
              ))}
            </select>
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Find button */}
          <button
            onClick={handleFind}
            disabled={loading || !pid || !address.trim()}
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
            {loading ? 'Searching...' : 'Find Xrefs'}
          </button>
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

      {/* Results */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {!connected || !pid ? (
          /* Empty state: not connected */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u2192'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Connect to Orpheus to find xrefs</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Attach to a process first</p>
          </motion.div>
        ) : !hasSearched ? (
          /* Empty state: no search yet */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u2192'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter an address to find cross-references</p>
            <p className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>0x7FF6A1B2C3D4</p>
          </motion.div>
        ) : results.length === 0 && !loading ? (
          /* Empty state: no results */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u2192'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No cross-references found</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Try a different address or module scope</p>
          </motion.div>
        ) : results.length > 0 ? (
          /* Results table */
          <table className="w-full text-sm">
            <thead className="sticky top-0 z-10">
              <tr style={{ background: 'var(--bg)' }}>
                {([
                  ['ADDRESS', 'w-40'],
                  ['INSTRUCTION', ''],
                  ['TYPE', 'w-20'],
                  ['MODULE+OFFSET', 'w-48'],
                ] as [string, string][]).map(([label, width]) => (
                  <th
                    key={label}
                    className={`text-left text-[10px] uppercase px-3 py-2.5 select-none ${width}`}
                    style={{
                      fontWeight: 400,
                      letterSpacing: '0.08em',
                      color: 'var(--text-muted)',
                      borderBottom: '1px solid var(--border)',
                    }}
                  >
                    {label}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {results.map((xref, index) => (
                <motion.tr
                  key={`${xref.address}-${index}`}
                  className="h-9 cursor-pointer group"
                  style={{
                    background: 'transparent',
                    transition: 'background 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = 'var(--hover)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent';
                  }}
                  onClick={() => handleAddressClick(xref.address)}
                  initial={{ opacity: 0, x: -4 }}
                  animate={{ opacity: 1, x: 0 }}
                  transition={{
                    duration: 0.1,
                    ease: 'easeOut',
                    delay: Math.min(index * 0.01, 0.2),
                  }}
                  layout
                >
                  {/* Address */}
                  <td
                    className="px-3 py-1.5 font-mono text-xs tabular-nums"
                    style={{
                      color: 'var(--text-secondary)',
                      textDecoration: 'underline',
                      textDecorationColor: 'var(--border)',
                      textUnderlineOffset: '2px',
                    }}
                  >
                    {xref.address}
                  </td>
                  {/* Instruction */}
                  <td className="px-3 py-1.5 font-mono text-xs truncate max-w-0" style={{ color: 'var(--text)' }}>
                    <span className="truncate">{xref.instruction}</span>
                  </td>
                  {/* Type badge */}
                  <td className="px-3 py-1.5">
                    <span
                      className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                      style={{
                        fontWeight: 400,
                        background: 'var(--active)',
                        ...getMnemonicStyle(xref.mnemonic),
                      }}
                    >
                      {xref.mnemonic}
                    </span>
                  </td>
                  {/* Module+Offset */}
                  <td className="px-3 py-1.5 font-mono text-xs" style={{ color: 'var(--text-muted)' }}>
                    {xref.module_name && xref.module_offset
                      ? `${xref.module_name}+${xref.module_offset}`
                      : xref.module_name || '\u2014'}
                  </td>
                </motion.tr>
              ))}
            </tbody>
          </table>
        ) : null}
      </div>
    </div>
  );
}

export default XrefFinder;
