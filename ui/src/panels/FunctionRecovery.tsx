import { useState, useEffect, useMemo, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useFunctionRecovery } from '../hooks/useFunctionRecovery';
import { useModules } from '../hooks/useModules';
import { useProcess } from '../hooks/useProcess';
import { useDma } from '../hooks/useDma';
import { useContextMenu } from '../hooks/useContextMenu';
import ContextMenu from '../components/ContextMenu';
import { copyToClipboard } from '../utils/clipboard';
import { orpheus } from '../api/client';

type SortField = 'entry_address' | 'name' | 'size' | 'source' | 'confidence';
type SortDir = 'asc' | 'desc';

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function FunctionRecovery({ onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { process: attachedProcess } = useProcess();
  const { connected: dmaConnected } = useDma();
  const pid = attachedProcess?.pid;
  const { functions, scanTime, stats, loading, error, recover } = useFunctionRecovery();
  const { modules, refresh: refreshModules } = useModules();

  const [selectedModule, setSelectedModule] = useState('');
  const [hasScanned, setHasScanned] = useState(false);
  const [filter, setFilter] = useState('');
  const [sortField, setSortField] = useState<SortField>('entry_address');
  const [sortDir, setSortDir] = useState<SortDir>('asc');
  const [maxFunctions, setMaxFunctions] = useState(10000);
  const { menu, show: showContextMenu, close: closeContextMenu } = useContextMenu();

  // Technique checkboxes
  const [prologues, setPrologues] = useState(true);
  const [callTargets, setCallTargets] = useState(true);
  const [exceptionData, setExceptionData] = useState(true);
  const [rtti, setRtti] = useState(true);
  const [exports, setExports] = useState(true);

  // Fetch modules when DMA connected and attached
  useEffect(() => {
    if (dmaConnected && pid) {
      refreshModules(pid);
    }
  }, [dmaConnected, pid, refreshModules]);

  const selectedModuleInfo = useMemo(() => {
    return modules.find((m) => m.name === selectedModule);
  }, [modules, selectedModule]);

  const handleRecover = useCallback(() => {
    if (!pid || !selectedModuleInfo) return;
    setHasScanned(true);
    recover(pid, selectedModuleInfo.base, selectedModuleInfo.size, {
      prologues,
      call_targets: callTargets,
      exception_data: exceptionData,
      rtti,
      exports,
      max_functions: maxFunctions,
    });
  }, [pid, selectedModuleInfo, recover, prologues, callTargets, exceptionData, rtti, exports, maxFunctions]);

  // Filter
  const filtered = useMemo(() => {
    const q = filter.toLowerCase().trim();
    if (!q) return functions;
    return functions.filter(
      (f) =>
        f.name.toLowerCase().includes(q) ||
        f.entry_address.toLowerCase().includes(q)
    );
  }, [functions, filter]);

  // Sort
  const sorted = useMemo(() => {
    const list = [...filtered];
    list.sort((a, b) => {
      let cmp = 0;
      switch (sortField) {
        case 'entry_address':
          cmp = a.entry_address.localeCompare(b.entry_address);
          break;
        case 'name':
          cmp = a.name.localeCompare(b.name);
          break;
        case 'size':
          cmp = a.size - b.size;
          break;
        case 'source':
          cmp = a.source.localeCompare(b.source);
          break;
        case 'confidence':
          cmp = a.confidence - b.confidence;
          break;
      }
      return sortDir === 'asc' ? cmp : -cmp;
    });
    return list;
  }, [filtered, sortField, sortDir]);

  const handleSort = useCallback((field: SortField) => {
    setSortField((prev) => {
      if (prev === field) {
        setSortDir((d) => (d === 'asc' ? 'desc' : 'asc'));
        return prev;
      }
      setSortDir('asc');
      return field;
    });
  }, []);

  const sortIcon = (field: SortField) => {
    if (sortField !== field) return null;
    return (
      <span className="ml-1" style={{ color: 'var(--text)' }}>
        {sortDir === 'asc' ? '\u25B4' : '\u25BE'}
      </span>
    );
  };

  const sourceLabel = (source: string): string => {
    switch (source) {
      case 'CallTarget': return 'Call';
      case 'ExceptionData': return 'PData';
      default: return source;
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
              Functions
            </h1>
            {functions.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {filtered.length}
                {filter && ` / ${functions.length}`}
                {scanTime != null && ` \u00B7 ${(scanTime / 1000).toFixed(1)}s`}
              </span>
            )}
          </div>
        </div>

        {/* Controls row */}
        <div className="flex items-center gap-3 flex-wrap">
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
                <option key={m.base} value={m.name}>
                  {m.name}
                </option>
              ))}
            </select>
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Technique checkboxes */}
          {([
            ['Prologues', prologues, setPrologues],
            ['Call Targets', callTargets, setCallTargets],
            ['.pdata', exceptionData, setExceptionData],
            ['RTTI', rtti, setRtti],
            ['Exports', exports, setExports],
          ] as [string, boolean, (v: boolean) => void][]).map(([label, checked, setter]) => (
            <label
              key={label}
              className="flex items-center gap-1 text-xs cursor-pointer select-none"
              style={{ color: checked ? 'var(--text)' : 'var(--text-muted)' }}
            >
              <input
                type="checkbox"
                checked={checked}
                onChange={(e) => setter(e.target.checked)}
                className="cursor-pointer"
                style={{ accentColor: 'var(--text)' }}
              />
              {label}
            </label>
          ))}

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Max functions */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Max
            </span>
            <input
              type="number"
              value={maxFunctions}
              onChange={(e) => setMaxFunctions(Math.max(1, parseInt(e.target.value) || 1))}
              className="h-7 w-20 px-2 rounded-md text-xs font-mono outline-none"
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
              min={1}
            />
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Recover button */}
          <button
            onClick={handleRecover}
            disabled={loading || !pid || !selectedModule || !dmaConnected}
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
            {loading ? 'Recovering...' : 'Recover'}
          </button>
        </div>

        {/* Progress bar — shown while loading */}
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
                  animate={{ width: ['0%', '60%', '80%', '90%'] }}
                  transition={{ duration: 30, ease: 'easeOut' }}
                />
              </div>
              <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                Recovering functions...
              </span>
            </motion.div>
          )}
        </AnimatePresence>

        {/* Stats bar */}
        <AnimatePresence>
          {stats && !loading && (
            <motion.div
              className="flex items-center gap-2 flex-wrap"
              initial={{ opacity: 0, height: 0 }}
              animate={{ opacity: 1, height: 'auto' }}
              exit={{ opacity: 0, height: 0 }}
              transition={{ duration: 0.12 }}
            >
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                Found {functions.length} functions
                {scanTime != null && ` in ${(scanTime / 1000).toFixed(1)}s`}
              </span>
              <span style={{ color: 'var(--text-muted)' }}>{'\u00B7'}</span>
              {Object.entries(stats).map(([source, count]) => (
                <span
                  key={source}
                  className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                  style={{
                    background: 'var(--active)',
                    color: 'var(--text-muted)',
                  }}
                >
                  {sourceLabel(source)} {count}
                </span>
              ))}
            </motion.div>
          )}
        </AnimatePresence>

        {/* Filter input */}
        {functions.length > 0 && (
          <div className="relative">
            <span
              className="absolute left-3 top-1/2 -translate-y-1/2 text-sm pointer-events-none"
              style={{ color: 'var(--text-muted)' }}
            >
              {'\u2315'}
            </span>
            <input
              type="text"
              placeholder="Filter by name or address..."
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
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u03BB'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Select a module to discover functions</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Prologues, calls, .pdata, RTTI, and exports</p>
          </motion.div>
        ) : functions.length === 0 && !loading ? (
          /* Empty state: no results */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u03BB'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No functions found</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Try enabling more techniques or a different module</p>
          </motion.div>
        ) : sorted.length > 0 ? (
          /* Results table */
          <table className="w-full text-sm">
            <thead className="sticky top-0 z-10">
              <tr style={{ background: 'var(--bg)' }}>
                {([
                  ['entry_address', 'ADDRESS', 'w-40'],
                  ['name', 'NAME', ''],
                  ['size', 'SIZE', 'w-24'],
                  ['source', 'SOURCE', 'w-24'],
                  ['confidence', 'CONFIDENCE', 'w-28'],
                ] as [SortField, string, string][]).map(([field, label, width]) => (
                  <th
                    key={field}
                    onClick={() => handleSort(field)}
                    className={`text-left text-[10px] uppercase px-3 py-2.5 cursor-pointer select-none ${width}`}
                    style={{
                      fontWeight: 400,
                      letterSpacing: '0.08em',
                      color: 'var(--text-muted)',
                      borderBottom: '1px solid var(--border)',
                      transition: 'color 0.1s ease',
                    }}
                    onMouseEnter={(e) => {
                      e.currentTarget.style.color = 'var(--text)';
                    }}
                    onMouseLeave={(e) => {
                      e.currentTarget.style.color = 'var(--text-muted)';
                    }}
                  >
                    {label}
                    {sortIcon(field)}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {sorted.map((fn, index) => (
                <motion.tr
                  key={fn.entry_address}
                  className="h-9 cursor-pointer group"
                  style={{
                    background: 'transparent',
                    transition: 'background 0.1s ease',
                  }}
                  onContextMenu={(e) => showContextMenu(e, [
                    { label: 'View in Disassembly', action: () => onNavigate?.('disassembly', fn.entry_address) },
                    { label: 'Decompile', action: () => onNavigate?.('decompiler', fn.entry_address) },
                    { label: 'Build CFG', action: () => onNavigate?.('cfg', fn.entry_address) },
                    { label: 'Add Bookmark', action: () => {
                      orpheus.request('tools/bookmarks/add', {
                        address: fn.entry_address,
                        label: fn.name,
                        notes: '',
                        category: 'Functions',
                        module: '',
                      }).catch(() => {});
                    }},
                    { label: 'Copy Address', action: () => copyToClipboard(fn.entry_address), separator: true },
                    { label: 'Copy Name', action: () => copyToClipboard(fn.name) },
                  ])}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = 'var(--hover)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent';
                  }}
                  initial={{ opacity: 0, x: -4 }}
                  animate={{ opacity: 1, x: 0 }}
                  transition={{
                    duration: 0.1,
                    ease: 'easeOut',
                    delay: Math.min(index * 0.005, 0.2),
                  }}
                  layout
                >
                  {/* Address */}
                  <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-secondary)' }}>
                    {fn.entry_address}
                  </td>
                  {/* Name */}
                  <td className="px-3 py-1.5 font-mono text-xs truncate max-w-0" style={{ color: 'var(--text)' }}>
                    <span className="truncate">{fn.name}</span>
                  </td>
                  {/* Size */}
                  <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-muted)' }}>
                    {formatSize(fn.size)}
                  </td>
                  {/* Source */}
                  <td className="px-3 py-1.5">
                    <span
                      className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                      style={{
                        fontWeight: 400,
                        background: 'var(--active)',
                        color: 'var(--text-muted)',
                      }}
                    >
                      {sourceLabel(fn.source)}
                    </span>
                  </td>
                  {/* Confidence */}
                  <td className="px-3 py-1.5">
                    <div className="flex items-center gap-2">
                      <div
                        className="flex-1 h-1 rounded-full overflow-hidden"
                        style={{ background: 'var(--border)' }}
                      >
                        <div
                          className="h-full rounded-full"
                          style={{
                            width: `${fn.confidence * 100}%`,
                            background: 'var(--text-muted)',
                          }}
                        />
                      </div>
                      <span className="font-mono text-[10px] tabular-nums w-8 text-right" style={{ color: 'var(--text-muted)' }}>
                        {Math.round(fn.confidence * 100)}%
                      </span>
                    </div>
                  </td>
                </motion.tr>
              ))}
            </tbody>
          </table>
        ) : filter && functions.length > 0 ? (
          /* No filter matches */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u03BB'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No functions match your filter</p>
          </motion.div>
        ) : null}
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

export default FunctionRecovery;
