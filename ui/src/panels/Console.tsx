import { useState, useMemo, useRef, useEffect, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useLogger, clearLog } from '../hooks/useLogger';
import type { LogLevel } from '../hooks/useLogger';

const LEVEL_FILTERS: Array<LogLevel | 'ALL'> = ['ALL', 'TRC', 'DBG', 'INF', 'WRN', 'ERR'];

function formatTimestamp(ts: number): string {
  const d = new Date(ts);
  const h = d.getHours().toString().padStart(2, '0');
  const m = d.getMinutes().toString().padStart(2, '0');
  const s = d.getSeconds().toString().padStart(2, '0');
  const ms = d.getMilliseconds().toString().padStart(3, '0');
  return `${h}:${m}:${s}.${ms}`;
}

function levelStyle(level: LogLevel): { fontWeight: number; opacity: number } {
  switch (level) {
    case 'TRC': return { fontWeight: 400, opacity: 0.5 };
    case 'DBG': return { fontWeight: 400, opacity: 0.6 };
    case 'INF': return { fontWeight: 400, opacity: 1.0 };
    case 'WRN': return { fontWeight: 500, opacity: 1.0 };
    case 'ERR': return { fontWeight: 600, opacity: 1.0 };
  }
}

function Console() {
  const { entries } = useLogger();
  const [levelFilter, setLevelFilter] = useState<LogLevel | 'ALL'>('ALL');
  const [search, setSearch] = useState('');
  const [autoScroll, setAutoScroll] = useState(true);
  const scrollRef = useRef<HTMLDivElement>(null);
  const prevCountRef = useRef(0);

  const filtered = useMemo(() => {
    let list = entries;
    if (levelFilter !== 'ALL') {
      list = list.filter((e) => e.level === levelFilter);
    }
    if (search.trim()) {
      const q = search.toLowerCase().trim();
      list = list.filter(
        (e) =>
          e.message.toLowerCase().includes(q) ||
          e.module.toLowerCase().includes(q)
      );
    }
    return list;
  }, [entries, levelFilter, search]);

  // Auto-scroll when new entries arrive
  useEffect(() => {
    if (autoScroll && scrollRef.current && filtered.length > prevCountRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
    prevCountRef.current = filtered.length;
  }, [filtered.length, autoScroll]);

  const handleClear = useCallback(() => {
    clearLog();
  }, []);

  return (
    <div className="h-full flex flex-col">
      {/* Header */}
      <motion.div
        className="shrink-0 px-6 pt-6 pb-4 space-y-3"
        initial={{ opacity: 0, y: -8 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.15, ease: 'easeOut' }}
      >
        {/* Title row */}
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <h1 className="text-lg tracking-tight" style={{ color: 'var(--text)', fontWeight: 500 }}>
              Console
            </h1>
            <span
              className="text-xs px-2 py-0.5 rounded-md font-mono"
              style={{ color: 'var(--text-secondary)', background: 'var(--active)' }}
            >
              {filtered.length}
              {(levelFilter !== 'ALL' || search) && ` / ${entries.length}`}
            </span>
          </div>
          <div className="flex items-center gap-2">
            {/* Auto-scroll toggle */}
            <button
              onClick={() => setAutoScroll(!autoScroll)}
              className="px-2.5 h-7 rounded-md text-xs cursor-pointer border-none outline-none"
              style={{
                fontWeight: 400,
                background: autoScroll ? 'var(--active)' : 'transparent',
                color: autoScroll ? 'var(--text)' : 'var(--text-secondary)',
                border: '1px solid var(--border)',
                transition: 'all 0.1s ease',
              }}
              onMouseEnter={(e) => {
                if (!autoScroll) {
                  e.currentTarget.style.background = 'var(--hover)';
                  e.currentTarget.style.color = 'var(--text)';
                }
              }}
              onMouseLeave={(e) => {
                if (!autoScroll) {
                  e.currentTarget.style.background = 'transparent';
                  e.currentTarget.style.color = 'var(--text-secondary)';
                }
              }}
            >
              {autoScroll ? '\u25C9 Auto-scroll' : '\u25CB Auto-scroll'}
            </button>
            {/* Clear */}
            <button
              onClick={handleClear}
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
              Clear
            </button>
          </div>
        </div>

        {/* Level filter pills */}
        <div className="flex items-center gap-1">
          {LEVEL_FILTERS.map((level) => {
            const active = levelFilter === level;
            return (
              <button
                key={level}
                onClick={() => setLevelFilter(level)}
                className="px-2.5 h-6 rounded-md text-[11px] font-mono cursor-pointer border-none outline-none"
                style={{
                  fontWeight: 400,
                  background: active ? 'var(--active)' : 'transparent',
                  color: active ? 'var(--text)' : 'var(--text-muted)',
                  transition: 'all 0.1s ease',
                }}
                onMouseEnter={(e) => {
                  if (!active) {
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }
                }}
                onMouseLeave={(e) => {
                  if (!active) {
                    e.currentTarget.style.color = 'var(--text-muted)';
                  }
                }}
              >
                {level}
              </button>
            );
          })}
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
            placeholder="Filter messages..."
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
                onMouseEnter={(e) => { e.currentTarget.style.color = 'var(--text)'; }}
                onMouseLeave={(e) => { e.currentTarget.style.color = 'var(--text-muted)'; }}
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

      {/* Log entries */}
      <div ref={scrollRef} className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {filtered.length === 0 ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u25B8'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No log entries</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Events will appear here as they occur</p>
          </motion.div>
        ) : (
          <div className="space-y-0">
            {filtered.map((entry) => {
              const style = levelStyle(entry.level);
              return (
                <div
                  key={entry.id}
                  className="flex items-baseline gap-3 py-0.5 font-mono text-xs leading-5"
                  style={{ opacity: style.opacity }}
                >
                  {/* Timestamp */}
                  <span className="shrink-0 tabular-nums" style={{ color: 'var(--text-muted)', fontSize: '11px' }}>
                    {formatTimestamp(entry.timestamp)}
                  </span>
                  {/* Level badge */}
                  <span
                    className="shrink-0 w-7 text-center"
                    style={{
                      color: entry.level === 'ERR' ? 'var(--text)' : entry.level === 'WRN' ? 'var(--text-secondary)' : 'var(--text-muted)',
                      fontWeight: style.fontWeight,
                      fontSize: '10px',
                    }}
                  >
                    {entry.level}
                  </span>
                  {/* Module */}
                  <span className="shrink-0" style={{ color: 'var(--text-secondary)', fontSize: '11px' }}>
                    {entry.module}
                  </span>
                  {/* Message */}
                  <span
                    className="min-w-0 break-all"
                    style={{
                      color: 'var(--text)',
                      fontWeight: style.fontWeight,
                    }}
                  >
                    {entry.message}
                  </span>
                </div>
              );
            })}
          </div>
        )}
      </div>
    </div>
  );
}

export default Console;
