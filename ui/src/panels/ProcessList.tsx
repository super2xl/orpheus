import { useState, useEffect, useMemo, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useProcesses } from '../hooks/useProcesses';
import { useConnection } from '../hooks/useConnection';
import type { ProcessInfo } from '../api/types';

type SortField = 'pid' | 'name' | 'arch' | 'base_address';
type SortDir = 'asc' | 'desc';

function ProcessList() {
  const { processes, loading, error, refresh } = useProcesses();
  const { connected } = useConnection();
  const [search, setSearch] = useState('');
  const [selectedPid, setSelectedPid] = useState<number | null>(null);
  const [autoRefresh, setAutoRefresh] = useState(false);
  const [sortField, setSortField] = useState<SortField>('pid');
  const [sortDir, setSortDir] = useState<SortDir>('asc');
  const [hasLoaded, setHasLoaded] = useState(false);

  // Initial fetch
  useEffect(() => {
    if (connected) {
      refresh().then(() => setHasLoaded(true));
    }
  }, [connected, refresh]);

  // Auto-refresh
  useEffect(() => {
    if (!autoRefresh || !connected) return;
    const interval = setInterval(refresh, 3000);
    return () => clearInterval(interval);
  }, [autoRefresh, connected, refresh]);

  // Filter
  const filtered = useMemo(() => {
    const q = search.toLowerCase().trim();
    if (!q) return processes;
    return processes.filter(
      (p) =>
        p.name.toLowerCase().includes(q) ||
        p.pid.toString().includes(q) ||
        p.base_address.toLowerCase().includes(q)
    );
  }, [processes, search]);

  // Sort
  const sorted = useMemo(() => {
    const list = [...filtered];
    list.sort((a, b) => {
      let cmp = 0;
      switch (sortField) {
        case 'pid':
          cmp = a.pid - b.pid;
          break;
        case 'name':
          cmp = a.name.localeCompare(b.name);
          break;
        case 'arch':
          cmp = (a.is_64bit ? 'x64' : 'x86').localeCompare(b.is_64bit ? 'x64' : 'x86');
          break;
        case 'base_address':
          cmp = a.base_address.localeCompare(b.base_address);
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

  const handleRowClick = useCallback((pid: number) => {
    setSelectedPid((prev) => (prev === pid ? null : pid));
  }, []);

  const handleAttach = useCallback(
    (_p: ProcessInfo) => {
      // Future: call attach endpoint
      setSelectedPid(_p.pid);
    },
    []
  );

  const sortIcon = (field: SortField) => {
    if (sortField !== field) return null;
    return (
      <span className="ml-1 text-cyan-400">
        {sortDir === 'asc' ? '\u25B4' : '\u25BE'}
      </span>
    );
  };

  // Skeleton rows for loading state
  const skeletonRows = Array.from({ length: 12 }, (_, i) => i);

  return (
    <div className="h-full flex flex-col">
      {/* Header */}
      <motion.div
        className="shrink-0 px-5 pt-5 pb-3 space-y-3"
        initial={{ opacity: 0, y: -10 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ type: 'spring', stiffness: 300, damping: 30 }}
      >
        {/* Title row */}
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <h1 className="text-lg font-semibold tracking-tight text-slate-100">
              Processes
            </h1>
            {processes.length > 0 && (
              <motion.span
                className="text-xs text-slate-500 bg-slate-800/60 px-2 py-0.5 rounded-md font-mono"
                initial={{ opacity: 0, scale: 0.8 }}
                animate={{ opacity: 1, scale: 1 }}
                transition={{ type: 'spring', stiffness: 400, damping: 25 }}
              >
                {filtered.length}
                {search && ` / ${processes.length}`}
              </motion.span>
            )}
          </div>
          <div className="flex items-center gap-2">
            {/* Auto-refresh toggle */}
            <motion.button
              onClick={() => setAutoRefresh(!autoRefresh)}
              className={`
                px-2.5 h-7 rounded-md text-xs font-medium transition-colors duration-150 cursor-pointer
                ${autoRefresh
                  ? 'bg-cyan-500/15 text-cyan-400 border border-cyan-500/30'
                  : 'bg-slate-800/60 text-slate-400 border border-slate-700/40 hover:bg-slate-800 hover:text-slate-300'
                }
              `}
              whileHover={{ scale: 1.03 }}
              whileTap={{ scale: 0.97 }}
            >
              {autoRefresh ? '\u25C9 Auto' : '\u25CB Auto'}
            </motion.button>
            {/* Manual refresh */}
            <motion.button
              onClick={refresh}
              disabled={loading}
              className="px-2.5 h-7 rounded-md text-xs font-medium bg-slate-800/60 text-slate-400 border border-slate-700/40 hover:bg-slate-800 hover:text-slate-300 transition-colors duration-150 disabled:opacity-40 cursor-pointer"
              whileHover={{ scale: 1.03 }}
              whileTap={{ scale: 0.97 }}
            >
              <motion.span
                animate={loading ? { rotate: 360 } : { rotate: 0 }}
                transition={loading ? { duration: 1, repeat: Infinity, ease: 'linear' } : { duration: 0 }}
                className="inline-block"
              >
                {'\u21BB'}
              </motion.span>
              {' Refresh'}
            </motion.button>
          </div>
        </div>

        {/* Search bar */}
        <div className="relative">
          <span className="absolute left-3 top-1/2 -translate-y-1/2 text-slate-500 text-sm pointer-events-none">
            {'\u2315'}
          </span>
          <input
            type="text"
            placeholder="Search processes..."
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            className="w-full h-9 pl-8 pr-3 rounded-lg bg-slate-800/50 border border-slate-700/40 text-sm text-slate-200 placeholder:text-slate-600 outline-none focus:border-cyan-500/40 focus:ring-1 focus:ring-cyan-500/20 transition-all duration-200"
          />
          <AnimatePresence>
            {search && (
              <motion.button
                onClick={() => setSearch('')}
                className="absolute right-2 top-1/2 -translate-y-1/2 text-slate-500 hover:text-slate-300 text-xs w-5 h-5 rounded flex items-center justify-center hover:bg-slate-700/50 transition-colors cursor-pointer"
                initial={{ opacity: 0, scale: 0.5 }}
                animate={{ opacity: 1, scale: 1 }}
                exit={{ opacity: 0, scale: 0.5 }}
                transition={{ type: 'spring', stiffness: 400, damping: 25 }}
              >
                {'\u2715'}
              </motion.button>
            )}
          </AnimatePresence>
        </div>
      </motion.div>

      {/* Error banner */}
      <AnimatePresence>
        {error && (
          <motion.div
            className="mx-5 mb-2 px-3 py-2 rounded-lg bg-red-500/10 border border-red-500/20 text-red-400 text-xs"
            initial={{ opacity: 0, height: 0 }}
            animate={{ opacity: 1, height: 'auto' }}
            exit={{ opacity: 0, height: 0 }}
            transition={{ type: 'spring', stiffness: 300, damping: 25 }}
          >
            {error}
          </motion.div>
        )}
      </AnimatePresence>

      {/* Table */}
      <div className="flex-1 min-h-0 overflow-auto px-5 pb-3">
        {!connected && !hasLoaded ? (
          /* Empty state: not connected */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.15 }}
          >
            <div className="text-3xl text-slate-700">{'\u25A3'}</div>
            <p className="text-sm text-slate-500">Start Orpheus to view processes</p>
            <p className="text-xs text-slate-600">Connect to the MCP server first</p>
          </motion.div>
        ) : (
          <table className="w-full text-sm">
            {/* Column headers */}
            <thead className="sticky top-0 z-10">
              <tr className="bg-slate-950/90 backdrop-blur-sm">
                {([
                  ['pid', 'PID', 'w-20'],
                  ['name', 'NAME', 'flex-1'],
                  ['arch', 'ARCH', 'w-20'],
                  ['base_address', 'BASE ADDRESS', 'w-40'],
                ] as [SortField, string, string][]).map(([field, label, width]) => (
                  <th
                    key={field}
                    onClick={() => handleSort(field)}
                    className={`
                      text-left text-[10px] font-semibold uppercase tracking-wider text-slate-500
                      px-3 py-2 border-b border-slate-800/40 cursor-pointer
                      hover:text-slate-300 transition-colors duration-150 select-none
                      ${width}
                    `}
                  >
                    {label}
                    {sortIcon(field)}
                  </th>
                ))}
                <th className="w-20 px-3 py-2 border-b border-slate-800/40" />
              </tr>
            </thead>
            <tbody>
              {loading && !hasLoaded ? (
                /* Skeleton loading */
                skeletonRows.map((i) => (
                  <tr key={`skeleton-${i}`} className="h-8">
                    {[1, 2, 3, 4, 5].map((col) => (
                      <td key={col} className="px-3 py-1.5">
                        <motion.div
                          className="h-3.5 rounded bg-slate-800/60"
                          style={{ width: col === 2 ? '60%' : col === 4 ? '70%' : '40%' }}
                          animate={{ opacity: [0.3, 0.6, 0.3] }}
                          transition={{ duration: 1.5, repeat: Infinity, ease: 'easeInOut', delay: i * 0.05 }}
                        />
                      </td>
                    ))}
                  </tr>
                ))
              ) : sorted.length === 0 ? (
                /* No results */
                <tr>
                  <td colSpan={5} className="text-center py-12 text-slate-600 text-sm">
                    {search ? 'No processes match your search' : 'No processes found'}
                  </td>
                </tr>
              ) : (
                /* Process rows */
                sorted.map((proc, index) => {
                  const isSelected = selectedPid === proc.pid;
                  return (
                    <motion.tr
                      key={proc.pid}
                      onClick={() => handleRowClick(proc.pid)}
                      onDoubleClick={() => handleAttach(proc)}
                      className={`
                        h-8 cursor-pointer transition-colors duration-100 group
                        ${isSelected
                          ? 'bg-cyan-500/8'
                          : index % 2 === 0
                            ? 'bg-transparent'
                            : 'bg-slate-900/30'
                        }
                        ${!isSelected && 'hover:bg-slate-800/40'}
                      `}
                      initial={{ opacity: 0, x: -6 }}
                      animate={{ opacity: 1, x: 0 }}
                      transition={{
                        type: 'spring',
                        stiffness: 400,
                        damping: 30,
                        delay: Math.min(index * 0.015, 0.3),
                      }}
                      layout
                    >
                      {/* PID */}
                      <td className="px-3 py-1.5 font-mono text-xs text-slate-400 tabular-nums">
                        {proc.pid}
                      </td>
                      {/* Name */}
                      <td className="px-3 py-1.5 text-slate-200 truncate max-w-0">
                        <div className="flex items-center gap-2">
                          {isSelected && (
                            <motion.div
                              className="w-1 h-1 rounded-full bg-cyan-400 shrink-0"
                              layoutId="selected-dot"
                              transition={{ type: 'spring', stiffness: 400, damping: 25 }}
                            />
                          )}
                          <span className="truncate">{proc.name}</span>
                        </div>
                      </td>
                      {/* Architecture */}
                      <td className="px-3 py-1.5">
                        <span
                          className={`
                            text-[10px] font-mono font-medium px-1.5 py-0.5 rounded
                            ${proc.is_64bit
                              ? 'bg-cyan-500/10 text-cyan-400'
                              : 'bg-amber-500/10 text-amber-400'
                            }
                          `}
                        >
                          {proc.is_64bit ? 'x64' : 'x86'}
                        </span>
                      </td>
                      {/* Base address */}
                      <td className="px-3 py-1.5 font-mono text-xs text-slate-500">
                        {proc.base_address}
                      </td>
                      {/* Attach button */}
                      <td className="px-3 py-1.5 text-right">
                        <motion.button
                          onClick={(e) => {
                            e.stopPropagation();
                            handleAttach(proc);
                          }}
                          className="opacity-0 group-hover:opacity-100 px-2 py-0.5 rounded text-[10px] font-medium bg-cyan-500/15 text-cyan-400 border border-cyan-500/25 hover:bg-cyan-500/25 transition-all duration-150 cursor-pointer"
                          whileHover={{ scale: 1.05 }}
                          whileTap={{ scale: 0.95 }}
                        >
                          Attach
                        </motion.button>
                      </td>
                    </motion.tr>
                  );
                })
              )}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

export default ProcessList;
