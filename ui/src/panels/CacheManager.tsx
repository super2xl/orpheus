import { useState, useEffect } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useCacheManager } from '../hooks/useCacheManager';
import type { CacheEntry } from '../api/types';

function formatDate(iso: string): string {
  try {
    const d = new Date(iso);
    return d.toLocaleDateString(undefined, { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
  } catch {
    return iso;
  }
}

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

interface CacheGroupProps {
  label: string;
  entries: CacheEntry[];
  onClearAll: () => void;
  onDelete: (filepath: string) => void;
  delay: number;
}

function CacheGroup({ label, entries, onClearAll, onDelete, delay }: CacheGroupProps) {
  const [expanded, setExpanded] = useState(true);

  return (
    <motion.section
      className="rounded-lg overflow-hidden"
      style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}
      initial={{ opacity: 0, y: 8 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.15, delay }}
    >
      {/* Group header */}
      <div
        className="flex items-center justify-between px-4 py-3 cursor-pointer"
        onClick={() => setExpanded(!expanded)}
        style={{ transition: 'background 0.1s ease' }}
        onMouseEnter={(e) => { e.currentTarget.style.background = 'var(--hover)'; }}
        onMouseLeave={(e) => { e.currentTarget.style.background = 'transparent'; }}
      >
        <div className="flex items-center gap-2.5">
          <span
            className="text-xs"
            style={{ color: 'var(--text-muted)', transition: 'transform 0.15s ease', display: 'inline-block', transform: expanded ? 'rotate(90deg)' : 'rotate(0deg)' }}
          >
            {'\u25B8'}
          </span>
          <h3 className="text-xs" style={{ color: 'var(--text)', fontWeight: 500 }}>
            {label}
          </h3>
          <span
            className="text-[10px] px-1.5 py-0.5 rounded font-mono"
            style={{ color: 'var(--text-muted)', background: 'var(--bg)' }}
          >
            {entries.length}
          </span>
        </div>
        {entries.length > 0 && (
          <button
            onClick={(e) => {
              e.stopPropagation();
              onClearAll();
            }}
            className="px-2 h-6 rounded-md text-[10px] cursor-pointer border-none outline-none"
            style={{
              fontWeight: 400,
              background: 'transparent',
              color: 'var(--text-muted)',
              border: '1px solid var(--border)',
              transition: 'all 0.1s ease',
            }}
            onMouseEnter={(e) => {
              e.currentTarget.style.background = 'var(--hover)';
              e.currentTarget.style.color = 'var(--text)';
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.background = 'transparent';
              e.currentTarget.style.color = 'var(--text-muted)';
            }}
          >
            Clear All
          </button>
        )}
      </div>

      {/* Entries */}
      <AnimatePresence>
        {expanded && entries.length > 0 && (
          <motion.div
            initial={{ height: 0, opacity: 0 }}
            animate={{ height: 'auto', opacity: 1 }}
            exit={{ height: 0, opacity: 0 }}
            transition={{ duration: 0.15 }}
            style={{ overflow: 'hidden' }}
          >
            <div style={{ borderTop: '1px solid var(--border)' }}>
              {entries.map((entry) => (
                <div
                  key={entry.filepath}
                  className="flex items-center justify-between px-4 py-2.5 group"
                  style={{
                    borderBottom: '1px solid var(--border-subtle)',
                    transition: 'background 0.1s ease',
                  }}
                  onMouseEnter={(e) => { e.currentTarget.style.background = 'var(--hover)'; }}
                  onMouseLeave={(e) => { e.currentTarget.style.background = 'transparent'; }}
                >
                  <div className="min-w-0 flex-1 space-y-0.5">
                    <div className="text-xs truncate" style={{ color: 'var(--text)' }}>
                      {entry.name}
                    </div>
                    <div className="flex items-center gap-3 text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                      <span>{entry.item_count} items</span>
                      <span>{formatSize(entry.size)}</span>
                      <span>{formatDate(entry.cached_at)}</span>
                    </div>
                  </div>
                  <button
                    onClick={() => onDelete(entry.filepath)}
                    className="opacity-0 group-hover:opacity-100 px-2 py-0.5 rounded text-[10px] cursor-pointer border-none outline-none shrink-0 ml-3"
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
                    Delete
                  </button>
                </div>
              ))}
            </div>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Empty state inside group */}
      {expanded && entries.length === 0 && (
        <div className="px-4 py-4 text-xs" style={{ color: 'var(--text-muted)', borderTop: '1px solid var(--border)' }}>
          No cached entries
        </div>
      )}
    </motion.section>
  );
}

function CacheManager() {
  const {
    stats,
    rttiEntries,
    schemaEntries,
    refresh,
    refreshStats,
    clearAll,
    clearRtti,
    clearSchema,
    deleteEntry,
  } = useCacheManager();
  const [hasLoaded, setHasLoaded] = useState(false);

  // Initial fetch + auto-refresh stats every 5s
  useEffect(() => {
    refresh().then(() => setHasLoaded(true));
    const interval = setInterval(refreshStats, 5000);
    return () => clearInterval(interval);
  }, [refresh, refreshStats]);

  const totalEntries = rttiEntries.length + schemaEntries.length;

  return (
    <div className="h-full flex flex-col overflow-auto">
      {/* Header */}
      <motion.div
        className="shrink-0 px-6 pt-6 pb-4"
        initial={{ opacity: 0, y: -8 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.15, ease: 'easeOut' }}
      >
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <h1 className="text-lg tracking-tight" style={{ color: 'var(--text)', fontWeight: 500 }}>
              Cache
            </h1>
            {hasLoaded && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{ color: 'var(--text-secondary)', background: 'var(--active)' }}
              >
                {totalEntries} entries
              </span>
            )}
          </div>
          {totalEntries > 0 && (
            <button
              onClick={clearAll}
              className="px-2.5 h-7 rounded-md text-xs cursor-pointer border-none outline-none"
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
              Clear All
            </button>
          )}
        </div>
      </motion.div>

      {/* Content */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-6 space-y-4">
        <>
            {/* Cache stats */}
            {stats && (
              <motion.section
                className="rounded-lg p-5 space-y-4"
                style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}
                initial={{ opacity: 0, y: 8 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ duration: 0.15, delay: 0.03 }}
              >
                <h2 className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
                  Statistics
                </h2>

                {/* Hit rate large display */}
                <div className="flex items-baseline gap-1.5">
                  <span className="text-3xl font-mono" style={{ color: 'var(--text)', fontWeight: 500 }}>
                    {(stats.hit_rate * 100).toFixed(1)}
                  </span>
                  <span className="text-sm" style={{ color: 'var(--text-muted)' }}>% hit rate</span>
                </div>

                {/* Stats grid */}
                <div className="grid grid-cols-2 gap-3">
                  {([
                    ['Hits', stats.hits.toLocaleString()],
                    ['Misses', stats.misses.toLocaleString()],
                    ['Evictions', stats.evictions.toLocaleString()],
                    ['Pages Cached', `${stats.current_pages.toLocaleString()}${stats.max_pages ? ` / ${stats.max_pages.toLocaleString()}` : ''}`],
                  ] as [string, string][]).map(([label, value]) => (
                    <div key={label} className="space-y-0.5">
                      <div className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
                        {label}
                      </div>
                      <div className="text-sm font-mono" style={{ color: 'var(--text)' }}>
                        {value}
                      </div>
                    </div>
                  ))}
                </div>
              </motion.section>
            )}

            {/* Cache entry groups */}
            <CacheGroup
              label="RTTI"
              entries={rttiEntries}
              onClearAll={clearRtti}
              onDelete={deleteEntry}
              delay={0.06}
            />
            <CacheGroup
              label="Schema"
              entries={schemaEntries}
              onClearAll={clearSchema}
              onDelete={deleteEntry}
              delay={0.09}
            />
          </>
      </div>
    </div>
  );
}

export default CacheManager;
