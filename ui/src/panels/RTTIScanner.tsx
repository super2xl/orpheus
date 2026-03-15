import { useState, useEffect, useMemo, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useRTTI } from '../hooks/useRTTI';
import { useModules } from '../hooks/useModules';
import { useConnection } from '../hooks/useConnection';
import { useDma } from '../hooks/useDma';
import { useContextMenu } from '../hooks/useContextMenu';
import ContextMenu from '../components/ContextMenu';
import { copyToClipboard } from '../utils/clipboard';
import type { RTTIClassInfo } from '../api/types';

function RTTIScanner({ onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { health } = useConnection();
  const { connected: dmaConnected } = useDma();
  const pid = health?.pid;
  const { results, scanTime, loading, error, progress, statusMessage, scan, cancel, parseVTable } = useRTTI();
  const { modules, refresh: refreshModules } = useModules();

  const [selectedModule, setSelectedModule] = useState('');
  const [hasScanned, setHasScanned] = useState(false);
  const [filter, setFilter] = useState('');
  const [expandedClass, setExpandedClass] = useState<string | null>(null);
  const [vtableEntries, setVtableEntries] = useState<string[]>([]);
  const [vtableLoading, setVtableLoading] = useState(false);
  const { menu, show: showContextMenu, close: closeContextMenu } = useContextMenu();

  // Fetch modules when DMA connected and attached
  useEffect(() => {
    if (dmaConnected && pid) {
      refreshModules(pid);
    }
  }, [dmaConnected, pid, refreshModules]);

  const selectedModuleInfo = useMemo(() => {
    return modules.find((m) => m.name === selectedModule);
  }, [modules, selectedModule]);

  const handleScan = useCallback(() => {
    if (!pid || !selectedModuleInfo) return;
    setHasScanned(true);
    setExpandedClass(null);
    setVtableEntries([]);
    scan(pid, selectedModuleInfo.base_address, selectedModuleInfo.size);
  }, [pid, selectedModuleInfo, scan]);

  const filtered = useMemo(() => {
    const q = filter.toLowerCase().trim();
    if (!q) return results;
    return results.filter(
      (c) =>
        c.demangled_name.toLowerCase().includes(q) ||
        c.mangled_name.toLowerCase().includes(q) ||
        c.vtable_address.toLowerCase().includes(q)
    );
  }, [results, filter]);

  const handleClassClick = useCallback(async (cls: RTTIClassInfo) => {
    if (expandedClass === cls.vtable_address) {
      setExpandedClass(null);
      setVtableEntries([]);
      return;
    }
    setExpandedClass(cls.vtable_address);
    setVtableEntries([]);
    if (pid) {
      setVtableLoading(true);
      const entries = await parseVTable(pid, cls.vtable_address);
      setVtableEntries(entries);
      setVtableLoading(false);
    }
  }, [expandedClass, pid, parseVTable]);

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
              RTTI
            </h1>
            {results.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {filtered.length}
                {filter && ` / ${results.length}`}
                {scanTime != null && ` \u00B7 ${scanTime}ms`}
              </span>
            )}
          </div>
        </div>

        {/* Controls row */}
        <div className="flex items-center gap-3">
          {/* Module selector */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Module
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
              <option value="">Select module...</option>
              {modules.map((m) => (
                <option key={m.base_address} value={m.name}>
                  {m.name}
                </option>
              ))}
            </select>
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Scan / Cancel button */}
          {loading ? (
            <button
              onClick={cancel}
              className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none"
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
              Cancel
            </button>
          ) : (
            <button
              onClick={handleScan}
              disabled={!pid || !selectedModule || !dmaConnected}
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
              Scan Module
            </button>
          )}
        </div>

        {/* Progress bar */}
        <AnimatePresence>
          {loading && (
            <motion.div
              className="space-y-1"
              initial={{ opacity: 0, height: 0 }}
              animate={{ opacity: 1, height: 'auto' }}
              exit={{ opacity: 0, height: 0 }}
              transition={{ duration: 0.12 }}
            >
              <div
                className="h-1 rounded-full overflow-hidden"
                style={{ background: 'var(--border)' }}
              >
                <motion.div
                  className="h-full rounded-full"
                  style={{ background: 'var(--text)' }}
                  initial={{ width: '0%' }}
                  animate={{ width: `${Math.max(progress, 2)}%` }}
                  transition={{ duration: 0.3, ease: 'easeOut' }}
                />
              </div>
              <div className="flex items-center justify-between">
                <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                  {statusMessage || 'Scanning...'}
                </span>
                <span className="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                  {progress}%
                </span>
              </div>
            </motion.div>
          )}
        </AnimatePresence>

        {/* Filter input */}
        {results.length > 0 && (
          <div className="relative">
            <span
              className="absolute left-3 top-1/2 -translate-y-1/2 text-sm pointer-events-none"
              style={{ color: 'var(--text-muted)' }}
            >
              {'\u2315'}
            </span>
            <input
              type="text"
              placeholder="Filter by class name..."
              value={filter}
              onChange={(e) => setFilter(e.target.value)}
              className="w-full h-9 pl-8 pr-3 rounded-lg text-sm outline-none"
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
            <AnimatePresence>
              {filter && (
                <motion.button
                  onClick={() => setFilter('')}
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
        )}
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
        {!hasScanned ? (
          /* Empty state: no scan yet */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u25C8'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Select a module to scan for RTTI vtables</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Discovers MSVC class hierarchies</p>
          </motion.div>
        ) : results.length === 0 && !loading ? (
          /* Empty state: no results */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u25C8'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No RTTI classes found</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Module may not contain MSVC RTTI data</p>
          </motion.div>
        ) : filtered.length > 0 ? (
          /* Results table */
          <table className="w-full text-sm">
            <thead className="sticky top-0 z-10">
              <tr style={{ background: 'var(--bg)' }}>
                {([
                  ['VTABLE ADDRESS', 'w-40'],
                  ['CLASS NAME', ''],
                  ['BASE CLASSES', 'w-52'],
                  ['METHODS', 'w-20'],
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
              {filtered.map((cls, index) => {
                const isExpanded = expandedClass === cls.vtable_address;
                return (
                  <motion.tr
                    key={cls.vtable_address}
                    onClick={() => handleClassClick(cls)}
                    onContextMenu={(e) => showContextMenu(e, [
                      { label: 'View VTable in Memory', action: () => onNavigate?.('memory', cls.vtable_address) },
                      { label: 'View in Disassembly', action: () => onNavigate?.('disassembly', cls.vtable_address) },
                      { label: 'Copy VTable Address', action: () => copyToClipboard(cls.vtable_address), separator: true },
                      { label: 'Copy Class Name', action: () => copyToClipboard(cls.demangled_name) },
                    ])}
                    className="cursor-pointer group"
                    style={{
                      background: isExpanded ? 'var(--active)' : 'transparent',
                      transition: 'background 0.1s ease',
                    }}
                    onMouseEnter={(e) => {
                      if (!isExpanded) {
                        e.currentTarget.style.background = 'var(--hover)';
                      }
                    }}
                    onMouseLeave={(e) => {
                      if (!isExpanded) {
                        e.currentTarget.style.background = 'transparent';
                      }
                    }}
                    initial={{ opacity: 0, x: -4 }}
                    animate={{ opacity: 1, x: 0 }}
                    transition={{
                      duration: 0.1,
                      ease: 'easeOut',
                      delay: Math.min(index * 0.01, 0.2),
                    }}
                    layout
                  >
                    {/* VTable Address */}
                    <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-secondary)' }}>
                      {cls.vtable_address}
                    </td>
                    {/* Class Name */}
                    <td className="px-3 py-1.5 font-mono text-xs truncate max-w-0" style={{ color: 'var(--text)' }}>
                      <span className="truncate">{cls.demangled_name}</span>
                    </td>
                    {/* Base Classes */}
                    <td className="px-3 py-1.5 font-mono text-xs truncate max-w-0" style={{ color: 'var(--text-muted)' }}>
                      <span className="truncate">
                        {cls.base_classes.length > 0 ? cls.base_classes.join(', ') : '\u2014'}
                      </span>
                    </td>
                    {/* Methods */}
                    <td className="px-3 py-1.5">
                      <span
                        className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                        style={{
                          fontWeight: 400,
                          background: 'var(--active)',
                          color: 'var(--text-secondary)',
                        }}
                      >
                        {cls.method_count}
                      </span>
                    </td>
                  </motion.tr>
                );
              })}
            </tbody>
          </table>
        ) : filter && results.length > 0 ? (
          /* No filter matches */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u25C8'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No classes match your filter</p>
          </motion.div>
        ) : null}

        {/* Expanded class detail */}
        <AnimatePresence>
          {expandedClass && (
            <motion.div
              className="mt-2 mb-4 rounded-lg p-4 space-y-3"
              style={{
                background: 'var(--surface)',
                border: '1px solid var(--border)',
              }}
              initial={{ opacity: 0, height: 0 }}
              animate={{ opacity: 1, height: 'auto' }}
              exit={{ opacity: 0, height: 0 }}
              transition={{ duration: 0.15 }}
            >
              {(() => {
                const cls = results.find((c) => c.vtable_address === expandedClass);
                if (!cls) return null;
                return (
                  <>
                    {/* Mangled name */}
                    <div className="space-y-1">
                      <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
                        Mangled Name
                      </span>
                      <p className="font-mono text-xs" style={{ color: 'var(--text-secondary)', userSelect: 'text' }}>
                        {cls.mangled_name}
                      </p>
                    </div>

                    {/* Inheritance flags */}
                    <div className="flex items-center gap-2">
                      {cls.is_multiple_inheritance && (
                        <span
                          className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                          style={{
                            background: 'var(--active)',
                            color: 'var(--text-secondary)',
                          }}
                        >
                          Multiple
                        </span>
                      )}
                      {cls.has_virtual_base && (
                        <span
                          className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                          style={{
                            background: 'var(--active)',
                            color: 'var(--text-secondary)',
                          }}
                        >
                          Virtual
                        </span>
                      )}
                      {!cls.is_multiple_inheritance && !cls.has_virtual_base && (
                        <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                          Single inheritance
                        </span>
                      )}
                    </div>

                    {/* VTable entries */}
                    <div className="space-y-1">
                      <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
                        VTable Entries ({cls.method_count})
                      </span>
                      {vtableLoading ? (
                        <div className="space-y-1">
                          {Array.from({ length: 3 }, (_, i) => (
                            <motion.div
                              key={i}
                              className="h-4 rounded"
                              style={{ width: '50%', background: 'var(--skeleton)' }}
                              animate={{ opacity: [0.3, 0.5, 0.3] }}
                              transition={{ duration: 2, repeat: Infinity, ease: 'easeInOut', delay: i * 0.1 }}
                            />
                          ))}
                        </div>
                      ) : vtableEntries.length > 0 ? (
                        <div className="space-y-0.5 max-h-40 overflow-auto">
                          {vtableEntries.map((entry, i) => (
                            <div key={i} className="flex items-center gap-2">
                              <span className="font-mono text-[10px] tabular-nums w-6 text-right" style={{ color: 'var(--text-muted)' }}>
                                {i}
                              </span>
                              <span className="font-mono text-xs tabular-nums" style={{ color: 'var(--text-secondary)', userSelect: 'text' }}>
                                {entry}
                              </span>
                            </div>
                          ))}
                        </div>
                      ) : (
                        <p className="text-xs" style={{ color: 'var(--text-muted)' }}>
                          No entries available
                        </p>
                      )}
                    </div>
                  </>
                );
              })()}
            </motion.div>
          )}
        </AnimatePresence>
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

export default RTTIScanner;
