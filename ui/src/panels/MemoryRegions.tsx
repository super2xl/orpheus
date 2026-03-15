import { useState, useEffect, useMemo, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useMemoryRegions } from '../hooks/useMemoryRegions';
import { useConnection } from '../hooks/useConnection';

type SortField = 'base_address' | 'size' | 'protection';
type SortDir = 'asc' | 'desc';
type ProtectionFilter = 'all' | 'execute' | 'write' | 'read';
type TypeFilter = 'all' | 'MEM_PRIVATE' | 'MEM_IMAGE' | 'MEM_MAPPED';

interface MemoryRegionsProps {
  onNavigate: (panel: string, address?: string) => void;
}

function formatSize(bytes: number): string {
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
  if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return bytes + ' B';
}

function formatTotalSize(bytes: number): string {
  if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + ' GB';
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
  if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return bytes + ' B';
}

function MemoryRegions({ onNavigate }: MemoryRegionsProps) {
  const { health } = useConnection();
  const pid = health?.pid;
  const { regions, loading, error, refresh } = useMemoryRegions();

  const [hasLoaded, setHasLoaded] = useState(false);
  const [search, setSearch] = useState('');
  const [protFilter, setProtFilter] = useState<ProtectionFilter>('all');
  const [typeFilter, setTypeFilter] = useState<TypeFilter>('all');
  const [sortField, setSortField] = useState<SortField>('base_address');
  const [sortDir, setSortDir] = useState<SortDir>('asc');

  useEffect(() => {
    if (pid) {
      refresh(pid).then(() => setHasLoaded(true));
    }
  }, [pid, refresh]);

  const handleRefresh = useCallback(() => {
    if (pid) refresh(pid);
  }, [pid, refresh]);

  const filtered = useMemo(() => {
    let list = regions;

    // Protection filter
    if (protFilter === 'execute') {
      list = list.filter((r) => r.protection.includes('X'));
    } else if (protFilter === 'write') {
      list = list.filter((r) => r.protection.includes('W'));
    } else if (protFilter === 'read') {
      list = list.filter((r) => r.protection.includes('R'));
    }

    // Type filter
    if (typeFilter !== 'all') {
      list = list.filter((r) => r.type === typeFilter);
    }

    // Search
    const q = search.toLowerCase().trim();
    if (q) {
      list = list.filter(
        (r) =>
          r.base_address.toLowerCase().includes(q) ||
          r.info.toLowerCase().includes(q) ||
          r.protection.toLowerCase().includes(q)
      );
    }

    return list;
  }, [regions, protFilter, typeFilter, search]);

  const sorted = useMemo(() => {
    const list = [...filtered];
    list.sort((a, b) => {
      let cmp = 0;
      switch (sortField) {
        case 'base_address':
          cmp = a.base_address.localeCompare(b.base_address);
          break;
        case 'size':
          cmp = a.size - b.size;
          break;
        case 'protection':
          cmp = a.protection.localeCompare(b.protection);
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

  const handleAddressClick = useCallback((address: string) => {
    onNavigate('memory', address);
  }, [onNavigate]);

  // Summary stats
  const stats = useMemo(() => {
    const totalSize = regions.reduce((acc, r) => acc + r.size, 0);
    const execCount = regions.filter((r) => r.protection.includes('X')).length;
    const writeCount = regions.filter((r) => r.protection.includes('W')).length;
    return { totalSize, execCount, writeCount };
  }, [regions]);

  const sortIcon = (field: SortField) => {
    if (sortField !== field) return null;
    return (
      <span className="ml-1" style={{ color: 'var(--text)' }}>
        {sortDir === 'asc' ? '\u25B4' : '\u25BE'}
      </span>
    );
  };

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
              Memory Regions
            </h1>
            {regions.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {filtered.length}
                {(search || protFilter !== 'all' || typeFilter !== 'all') && ` / ${regions.length}`}
              </span>
            )}
          </div>
          <div className="flex items-center gap-2">
            <button
              onClick={handleRefresh}
              disabled={loading}
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

        {/* Summary bar */}
        {regions.length > 0 && (
          <div className="flex items-center gap-4">
            <div className="flex items-center gap-1.5">
              <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
                Total
              </span>
              <span className="text-xs font-mono" style={{ color: 'var(--text-secondary)' }}>
                {formatTotalSize(stats.totalSize)}
              </span>
            </div>
            <div className="w-px h-4" style={{ background: 'var(--border)' }} />
            <div className="flex items-center gap-1.5">
              <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
                Executable
              </span>
              <span className="text-xs font-mono" style={{ color: 'var(--text-secondary)' }}>
                {stats.execCount}
              </span>
            </div>
            <div className="w-px h-4" style={{ background: 'var(--border)' }} />
            <div className="flex items-center gap-1.5">
              <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
                Writable
              </span>
              <span className="text-xs font-mono" style={{ color: 'var(--text-secondary)' }}>
                {stats.writeCount}
              </span>
            </div>
          </div>
        )}

        {/* Filter row */}
        <div className="flex items-center gap-3">
          {/* Protection filter */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Protection
            </span>
            <select
              value={protFilter}
              onChange={(e) => setProtFilter(e.target.value as ProtectionFilter)}
              className="h-7 px-2 rounded-md text-xs cursor-pointer outline-none"
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
              <option value="all">All</option>
              <option value="execute">Execute</option>
              <option value="write">Write</option>
              <option value="read">Read</option>
            </select>
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Type filter */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Type
            </span>
            <select
              value={typeFilter}
              onChange={(e) => setTypeFilter(e.target.value as TypeFilter)}
              className="h-7 px-2 rounded-md text-xs cursor-pointer outline-none"
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
              <option value="all">All</option>
              <option value="MEM_PRIVATE">Private</option>
              <option value="MEM_IMAGE">Image</option>
              <option value="MEM_MAPPED">Mapped</option>
            </select>
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Search */}
          <div className="relative flex-1 max-w-xs">
            <span
              className="absolute left-3 top-1/2 -translate-y-1/2 text-sm pointer-events-none"
              style={{ color: 'var(--text-muted)' }}
            >
              {'\u2315'}
            </span>
            <input
              type="text"
              placeholder="Search address / info..."
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              className="w-full h-7 pl-8 pr-3 rounded-md text-xs outline-none"
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
            <thead className="sticky top-0 z-10">
              <tr style={{ background: 'var(--bg)' }}>
                {([
                  ['base_address', 'BASE ADDRESS', 'w-44'],
                  ['size', 'SIZE', 'w-24'],
                  ['protection', 'PROTECTION', 'w-28'],
                  [null, 'TYPE', 'w-28'],
                  [null, 'INFO', ''],
                ] as [SortField | null, string, string][]).map(([field, label, width]) => (
                  <th
                    key={label}
                    onClick={field ? () => handleSort(field) : undefined}
                    className={`text-left text-[10px] uppercase px-3 py-2.5 select-none ${width} ${field ? 'cursor-pointer' : ''}`}
                    style={{
                      fontWeight: 400,
                      letterSpacing: '0.08em',
                      color: 'var(--text-muted)',
                      borderBottom: '1px solid var(--border)',
                      transition: 'color 0.1s ease',
                    }}
                    onMouseEnter={(e) => {
                      if (field) e.currentTarget.style.color = 'var(--text)';
                    }}
                    onMouseLeave={(e) => {
                      if (field) e.currentTarget.style.color = 'var(--text-muted)';
                    }}
                  >
                    {label}
                    {field && sortIcon(field)}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {loading && !hasLoaded ? (
                skeletonRows.map((i) => (
                  <tr key={`skeleton-${i}`} className="h-9">
                    {[1, 2, 3, 4, 5].map((col) => (
                      <td key={col} className="px-3 py-1.5">
                        <motion.div
                          className="h-3.5 rounded"
                          style={{
                            width: col === 1 ? '70%' : col === 5 ? '50%' : '40%',
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
                <tr>
                  <td colSpan={5} className="text-center py-12 text-sm" style={{ color: 'var(--text-muted)' }}>
                    {search || protFilter !== 'all' || typeFilter !== 'all'
                      ? 'No regions match your filters'
                      : 'No memory regions found'}
                  </td>
                </tr>
              ) : (
                sorted.map((region, index) => (
                  <motion.tr
                    key={region.base_address}
                    className="h-9 group"
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
                    initial={{ opacity: 0, x: -4 }}
                    animate={{ opacity: 1, x: 0 }}
                    transition={{
                      duration: 0.1,
                      ease: 'easeOut',
                      delay: Math.min(index * 0.005, 0.15),
                    }}
                    layout
                  >
                    {/* Base Address */}
                    <td className="px-3 py-1.5">
                      <button
                        onClick={() => handleAddressClick(region.base_address)}
                        className="font-mono text-xs tabular-nums cursor-pointer border-none outline-none bg-transparent p-0"
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
                        {region.base_address}
                      </button>
                    </td>
                    {/* Size */}
                    <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-secondary)' }}>
                      {formatSize(region.size)}
                    </td>
                    {/* Protection */}
                    <td className="px-3 py-1.5 font-mono text-xs">
                      <ProtectionDisplay protection={region.protection} />
                    </td>
                    {/* Type */}
                    <td className="px-3 py-1.5">
                      <span
                        className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                        style={{
                          fontWeight: 400,
                          background: 'var(--active)',
                          color: 'var(--text-muted)',
                        }}
                      >
                        {region.type}
                      </span>
                    </td>
                    {/* Info */}
                    <td className="px-3 py-1.5 text-xs truncate max-w-0" style={{ color: 'var(--text-muted)' }}>
                      <span className="truncate">{region.info || '\u2014'}</span>
                    </td>
                  </motion.tr>
                ))
              )}
            </tbody>
          </table>
      </div>
    </div>
  );
}

/** Renders a protection string like "RWX" with enabled flags in --text and dashes in --text-muted */
function ProtectionDisplay({ protection }: { protection: string }) {
  // Normalize to a "RWX" style display
  const hasR = protection.includes('R');
  const hasW = protection.includes('W');
  const hasX = protection.includes('X');

  return (
    <span className="tabular-nums">
      <span style={{ color: hasR ? 'var(--text)' : 'var(--text-muted)' }}>
        {hasR ? 'R' : '-'}
      </span>
      <span style={{ color: hasW ? 'var(--text)' : 'var(--text-muted)', fontWeight: hasW ? 500 : 400 }}>
        {hasW ? 'W' : '-'}
      </span>
      <span style={{ color: hasX ? 'var(--text)' : 'var(--text-muted)' }}>
        {hasX ? 'X' : '-'}
      </span>
    </span>
  );
}

export default MemoryRegions;
