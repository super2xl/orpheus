import { useState, useEffect, useMemo, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useProcesses } from '../hooks/useProcesses';
import { useDma } from '../hooks/useDma';
import { useContextMenu } from '../hooks/useContextMenu';
import ContextMenu from '../components/ContextMenu';
import { copyToClipboard } from '../utils/clipboard';
import type { ProcessInfo } from '../api/types';

type SortField = 'pid' | 'name' | 'arch' | 'base';
type SortDir = 'asc' | 'desc';

function ProcessList({ onNavigate: _onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { processes, loading, error, refresh } = useProcesses();
  const { connected: dmaConnected } = useDma();
  const [search, setSearch] = useState('');
  const [selectedPid, setSelectedPid] = useState<number | null>(null);
  const [autoRefresh, setAutoRefresh] = useState(false);
  const [sortField, setSortField] = useState<SortField>('pid');
  const [sortDir, setSortDir] = useState<SortDir>('asc');
  const [hasLoaded, setHasLoaded] = useState(false);
  const { menu, show: showContextMenu, close: closeContextMenu } = useContextMenu();

  // Fetch when DMA connects
  useEffect(() => {
    if (dmaConnected) {
      refresh().then(() => setHasLoaded(true));
    }
  }, [dmaConnected]);

  // Auto-refresh (only when DMA connected)
  useEffect(() => {
    if (!autoRefresh || !dmaConnected) return;
    const interval = setInterval(refresh, 3000);
    return () => clearInterval(interval);
  }, [autoRefresh, dmaConnected, refresh]);

  // Filter
  const filtered = useMemo(() => {
    const q = search.toLowerCase().trim();
    if (!q) return processes;
    return processes.filter(
      (p) =>
        p.name.toLowerCase().includes(q) ||
        p.pid.toString().includes(q) ||
        p.base.toLowerCase().includes(q)
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
        case 'base':
          cmp = a.base.localeCompare(b.base);
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
      <span className="ml-1" style={{ color: 'var(--text)' }}>
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
        className="shrink-0 px-6 pt-6 pb-4 space-y-4"
        initial={{ opacity: 0, y: -8 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.15, ease: 'easeOut' }}
      >
        {/* Title row */}
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <h1 className="text-lg tracking-tight" style={{ color: 'var(--text)', fontWeight: 500 }}>
              Processes
            </h1>
            {processes.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {filtered.length}
                {search && ` / ${processes.length}`}
              </span>
            )}
          </div>
          <div className="flex items-center gap-2">
            {/* Auto-refresh toggle */}
            <button
              onClick={() => setAutoRefresh(!autoRefresh)}
              className="px-2.5 h-7 rounded-md text-xs cursor-pointer border-none outline-none"
              style={{
                fontWeight: 400,
                background: autoRefresh ? 'var(--active)' : 'transparent',
                color: autoRefresh ? 'var(--text)' : 'var(--text-secondary)',
                border: '1px solid var(--border)',
                transition: 'all 0.1s ease',
              }}
              onMouseEnter={(e) => {
                if (!autoRefresh) {
                  e.currentTarget.style.background = 'var(--hover)';
                  e.currentTarget.style.color = 'var(--text)';
                }
              }}
              onMouseLeave={(e) => {
                if (!autoRefresh) {
                  e.currentTarget.style.background = 'transparent';
                  e.currentTarget.style.color = 'var(--text-secondary)';
                }
              }}
            >
              {autoRefresh ? '\u25C9 Auto' : '\u25CB Auto'}
            </button>
            {/* Manual refresh */}
            <button
              onClick={refresh}
              disabled={loading || !dmaConnected}
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
              {'\u21BB'} Refresh
            </button>
          </div>
        </div>

        {/* Search bar */}
        <div className="relative">
          <span
            className="absolute left-3 top-1/2 -translate-y-1/2 text-sm pointer-events-none"
            style={{ color: 'var(--text-muted)' }}
          >
            {'\u2315'}
          </span>
          <input
            type="text"
            placeholder="Search processes..."
            value={search}
            onChange={(e) => setSearch(e.target.value)}
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
            {search && (
              <motion.button
                onClick={() => setSearch('')}
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

      {/* Table */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        <table className="w-full text-sm">
            {/* Column headers */}
            <thead className="sticky top-0 z-10">
              <tr style={{ background: 'var(--bg)' }}>
                {([
                  ['pid', 'PID', 'w-20'],
                  ['name', 'NAME', 'flex-1'],
                  ['arch', 'ARCH', 'w-20'],
                  ['base', 'BASE ADDRESS', 'w-40'],
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
                <th className="w-20 px-3 py-2.5" style={{ borderBottom: '1px solid var(--border)' }} />
              </tr>
            </thead>
            <tbody>
              {loading && !hasLoaded ? (
                /* Skeleton loading */
                skeletonRows.map((i) => (
                  <tr key={`skeleton-${i}`} className="h-9">
                    {[1, 2, 3, 4, 5].map((col) => (
                      <td key={col} className="px-3 py-1.5">
                        <motion.div
                          className="h-3.5 rounded"
                          style={{
                            width: col === 2 ? '60%' : col === 4 ? '70%' : '40%',
                            background: 'var(--skeleton)',
                          }}
                          animate={{ opacity: [0.3, 0.5, 0.3] }}
                          transition={{ duration: 2, repeat: Infinity, ease: 'easeInOut', delay: i * 0.04 }}
                        />
                      </td>
                    ))}
                  </tr>
                ))
              ) : sorted.length === 0 ? (
                /* No results */
                <tr>
                  <td colSpan={5} className="text-center py-12 text-sm" style={{ color: 'var(--text-muted)' }}>
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
                      onContextMenu={(e) => showContextMenu(e, [
                        { label: 'Attach', action: () => handleAttach(proc) },
                        { label: 'Copy PID', action: () => copyToClipboard(proc.pid.toString()), separator: true },
                        { label: 'Copy Name', action: () => copyToClipboard(proc.name) },
                      ])}
                      className="h-9 cursor-pointer group"
                      style={{
                        background: isSelected ? 'var(--active)' : 'transparent',
                        borderLeft: isSelected ? '2px solid var(--active-border)' : '2px solid transparent',
                        transition: 'background 0.1s ease',
                      }}
                      onMouseEnter={(e) => {
                        if (!isSelected) {
                          e.currentTarget.style.background = 'var(--hover)';
                        }
                      }}
                      onMouseLeave={(e) => {
                        if (!isSelected) {
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
                      {/* PID */}
                      <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-secondary)' }}>
                        {proc.pid}
                      </td>
                      {/* Name */}
                      <td className="px-3 py-1.5 truncate max-w-0" style={{ color: 'var(--text)' }}>
                        <span className="truncate">{proc.name}</span>
                      </td>
                      {/* Architecture */}
                      <td className="px-3 py-1.5">
                        <span
                          className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                          style={{
                            fontWeight: 400,
                            background: 'var(--active)',
                            color: 'var(--text-secondary)',
                          }}
                        >
                          {proc.is_64bit ? 'x64' : 'x86'}
                        </span>
                      </td>
                      {/* Base address */}
                      <td className="px-3 py-1.5 font-mono text-xs" style={{ color: 'var(--text-muted)' }}>
                        {proc.base}
                      </td>
                      {/* Attach button */}
                      <td className="px-3 py-1.5 text-right">
                        <button
                          onClick={(e) => {
                            e.stopPropagation();
                            handleAttach(proc);
                          }}
                          className="opacity-0 group-hover:opacity-100 px-2 py-0.5 rounded text-[10px] cursor-pointer outline-none"
                          style={{
                            fontWeight: 400,
                            background: 'transparent',
                            color: 'var(--text-secondary)',
                            border: '1px solid var(--border)',
                            transition: 'all 0.1s ease',
                          }}
                          onMouseEnter={(e) => {
                            e.currentTarget.style.background = 'var(--active)';
                            e.currentTarget.style.color = 'var(--text)';
                          }}
                          onMouseLeave={(e) => {
                            e.currentTarget.style.background = 'transparent';
                            e.currentTarget.style.color = 'var(--text-secondary)';
                          }}
                        >
                          Attach
                        </button>
                      </td>
                    </motion.tr>
                  );
                })
              )}
            </tbody>
          </table>
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

export default ProcessList;
