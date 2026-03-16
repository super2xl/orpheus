import { useState, useEffect, useMemo, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useModules } from '../hooks/useModules';
import { useDma } from '../hooks/useDma';
import { useProcess } from '../hooks/useProcess';
import { useContextMenu } from '../hooks/useContextMenu';
import ContextMenu from '../components/ContextMenu';
import { copyToClipboard } from '../utils/clipboard';
import { useToast } from '../hooks/useToast';

type SortField = 'name' | 'base' | 'size' | 'entry';
type SortDir = 'asc' | 'desc';

function formatSize(bytes: number): string {
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
  if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return bytes + ' B';
}

function ModuleBrowser({ onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { modules, loading, error, refresh } = useModules();
  const { connected: dmaConnected } = useDma();
  const { process: attachedProcess } = useProcess();
  const { toast } = useToast();
  const [search, setSearch] = useState('');
  const [selectedIndex, setSelectedIndex] = useState<number | null>(null);
  const [sortField, setSortField] = useState<SortField>('name');
  const [sortDir, setSortDir] = useState<SortDir>('asc');
  const [hasLoaded, setHasLoaded] = useState(false);
  const [copiedAddress, setCopiedAddress] = useState<string | null>(null);
  const { menu, show: showContextMenu, close: closeContextMenu } = useContextMenu();

  const pid = attachedProcess?.pid;

  // Fetch when DMA connects and process attached
  useEffect(() => {
    if (dmaConnected && pid) {
      refresh(pid).then(() => setHasLoaded(true));
    }
  }, [dmaConnected, pid, refresh]);

  // Reset loaded state when PID changes
  useEffect(() => {
    setHasLoaded(false);
    setSelectedIndex(null);
  }, [pid]);

  // Filter
  const filtered = useMemo(() => {
    const q = search.toLowerCase().trim();
    if (!q) return modules;
    return modules.filter(
      (m) =>
        m.name.toLowerCase().includes(q) ||
        (m.path || '').toLowerCase().includes(q) ||
        m.base.toLowerCase().includes(q)
    );
  }, [modules, search]);

  // Sort
  const sorted = useMemo(() => {
    const list = [...filtered];
    list.sort((a, b) => {
      let cmp = 0;
      switch (sortField) {
        case 'name':
          cmp = a.name.localeCompare(b.name);
          break;
        case 'base':
          cmp = a.base.localeCompare(b.base);
          break;
        case 'size':
          cmp = a.size - b.size;
          break;
        case 'entry':
          cmp = a.entry.localeCompare(b.entry);
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

  const handleRowClick = useCallback((index: number) => {
    setSelectedIndex((prev) => (prev === index ? null : index));
  }, []);

  const handleCopyAddress = useCallback((address: string) => {
    navigator.clipboard.writeText(address);
    setCopiedAddress(address);
    setTimeout(() => setCopiedAddress(null), 1500);
  }, []);

  const handleRefresh = useCallback(() => {
    if (pid) refresh(pid);
  }, [pid, refresh]);

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
              Modules
            </h1>
            {modules.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {filtered.length}
                {search && ` / ${modules.length}`}
              </span>
            )}
          </div>
          <div className="flex items-center gap-2">
            {/* Manual refresh */}
            <button
              onClick={handleRefresh}
              disabled={loading || !pid || !dmaConnected}
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
            placeholder="Search modules..."
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
                  ['name', 'NAME', ''],
                  ['base', 'BASE ADDRESS', 'w-44'],
                  ['size', 'SIZE', 'w-28'],
                  ['entry', 'ENTRY POINT', 'w-44'],
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
                <th className="w-28 px-3 py-2.5" style={{ borderBottom: '1px solid var(--border)' }} />
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
                            width: col === 1 ? '60%' : col === 3 ? '40%' : '70%',
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
                    {search ? 'No modules match your search' : 'No modules found'}
                  </td>
                </tr>
              ) : (
                /* Module rows */
                sorted.map((mod, index) => {
                  const isSelected = selectedIndex === index;
                  return (
                    <motion.tr
                      key={`${mod.base}-${mod.name}`}
                      onClick={() => handleRowClick(index)}
                      onContextMenu={(e) => showContextMenu(e, [
                        { label: 'View in Memory', action: () => onNavigate?.('memory', mod.base) },
                        { label: 'View in Disassembly', action: () => onNavigate?.('disassembly', mod.entry) },
                        { label: 'Copy Base Address', action: () => { copyToClipboard(mod.base); toast('Address copied to clipboard'); }, separator: true },
                        { label: 'Copy Name', action: () => { copyToClipboard(mod.name); toast('Name copied to clipboard'); } },
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
                      {/* Name */}
                      <td className="px-3 py-1.5 truncate max-w-0" style={{ color: 'var(--text)' }}>
                        <span className="truncate" title={mod.path}>{mod.name}</span>
                      </td>
                      {/* Base Address */}
                      <td className="px-3 py-1.5 font-mono text-xs" style={{ color: 'var(--text-muted)' }}>
                        {mod.base}
                      </td>
                      {/* Size */}
                      <td className="px-3 py-1.5 font-mono text-xs" style={{ color: 'var(--text-secondary)' }}>
                        {formatSize(mod.size)}
                      </td>
                      {/* Entry Point */}
                      <td className="px-3 py-1.5 font-mono text-xs" style={{ color: 'var(--text-muted)' }}>
                        {mod.entry}
                      </td>
                      {/* Action buttons */}
                      <td className="px-3 py-1.5 text-right">
                        <div className="opacity-0 group-hover:opacity-100 flex items-center justify-end gap-1" style={{ transition: 'opacity 0.1s ease' }}>
                          <button
                            onClick={(e) => {
                              e.stopPropagation();
                              handleCopyAddress(mod.base);
                            }}
                            className="px-2 py-0.5 rounded text-[10px] cursor-pointer outline-none"
                            style={{
                              fontWeight: 400,
                              background: 'transparent',
                              color: copiedAddress === mod.base ? 'var(--text)' : 'var(--text-secondary)',
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
                            {copiedAddress === mod.base ? 'Copied' : 'Copy'}
                          </button>
                        </div>
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

export default ModuleBrowser;
