import { useState, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useVTableReader } from '../hooks/useVTableReader';
import { useConnection } from '../hooks/useConnection';
import { useDma } from '../hooks/useDma';

interface VTableReaderProps {
  onNavigate: (panel: string, address?: string) => void;
}

function VTableReader({ onNavigate }: VTableReaderProps) {
  const { health } = useConnection();
  const { connected: dmaConnected } = useDma();
  const pid = health?.pid;
  const { vtable, loading, error, read } = useVTableReader();

  const [address, setAddress] = useState('');
  const [entryCount, setEntryCount] = useState(20);

  const handleRead = useCallback(() => {
    if (!pid || !address.trim()) return;
    const addr = address.trim();
    read(pid, addr, Math.min(Math.max(entryCount, 1), 100));
  }, [pid, address, entryCount, read]);

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.key === 'Enter') {
        handleRead();
      }
    },
    [handleRead]
  );

  const handleAddressClick = useCallback(
    (addr: string) => {
      onNavigate('disassembly', addr);
    },
    [onNavigate]
  );

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
              VTable Reader
            </h1>
            {vtable && vtable.entries.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {vtable.entries.length} entries
              </span>
            )}
          </div>
        </div>

        {/* Input section */}
        <div className="flex items-center gap-2">
          {/* VTable address */}
          <div className="relative flex-1">
            <span
              className="absolute left-3 top-1/2 -translate-y-1/2 text-xs font-mono pointer-events-none"
              style={{ color: 'var(--text-muted)' }}
            >
              {'0x'}
            </span>
            <input
              type="text"
              placeholder="VTable address"
              value={address}
              onChange={(e) => setAddress(e.target.value)}
              onKeyDown={handleKeyDown}
              className="w-full h-9 pl-8 pr-3 rounded-lg text-sm font-mono outline-none"
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

          {/* Entry count */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Count
            </span>
            <input
              type="number"
              min={1}
              max={100}
              value={entryCount}
              onChange={(e) => setEntryCount(parseInt(e.target.value) || 20)}
              onKeyDown={handleKeyDown}
              className="w-16 h-9 px-2 rounded-lg text-sm font-mono outline-none text-center"
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

          {/* Read button */}
          <button
            onClick={handleRead}
            disabled={loading || !pid || !address.trim() || !dmaConnected}
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
            Read
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

      {/* Content */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {!vtable ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u25A4'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter a vtable address to read entries</p>
            <p className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>Function pointers + RTTI class info</p>
          </motion.div>
        ) : (
          <div className="space-y-4">
            {/* RTTI class info */}
            {vtable.class_name && (
              <motion.div
                className="rounded-lg p-4 space-y-2"
                style={{
                  background: 'var(--surface)',
                  border: '1px solid var(--border)',
                }}
                initial={{ opacity: 0, y: 4 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ duration: 0.15 }}
              >
                <div className="space-y-1">
                  <span
                    className="text-[10px] uppercase"
                    style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
                  >
                    Class
                  </span>
                  <p className="font-mono text-sm" style={{ color: 'var(--text)', userSelect: 'text' }}>
                    {vtable.class_name}
                  </p>
                </div>
                {vtable.base_classes && vtable.base_classes.length > 0 && (
                  <div className="space-y-1">
                    <span
                      className="text-[10px] uppercase"
                      style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
                    >
                      Base Classes
                    </span>
                    <div className="flex items-center gap-2 flex-wrap">
                      {vtable.base_classes.map((base) => (
                        <span
                          key={base}
                          className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                          style={{
                            background: 'var(--active)',
                            color: 'var(--text-secondary)',
                          }}
                        >
                          {base}
                        </span>
                      ))}
                    </div>
                  </div>
                )}
              </motion.div>
            )}

            {/* VTable entries table */}
            <table className="w-full text-sm">
              <thead className="sticky top-0 z-10">
                <tr style={{ background: 'var(--bg)' }}>
                  {([
                    ['INDEX', 'w-16'],
                    ['ADDRESS', 'w-44'],
                    ['FUNCTION', ''],
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
                {vtable.entries.length === 0 ? (
                  <tr>
                    <td colSpan={3} className="text-center py-12 text-sm" style={{ color: 'var(--text-muted)' }}>
                      No vtable entries found
                    </td>
                  </tr>
                ) : (
                  vtable.entries.map((entry, index) => (
                    <motion.tr
                      key={entry.index}
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
                        delay: Math.min(index * 0.01, 0.2),
                      }}
                      layout
                    >
                      {/* Index */}
                      <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-muted)' }}>
                        #{entry.index}
                      </td>
                      {/* Address */}
                      <td className="px-3 py-1.5">
                        <button
                          onClick={() => handleAddressClick(entry.address)}
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
                          {entry.address}
                        </button>
                      </td>
                      {/* Function name */}
                      <td className="px-3 py-1.5 font-mono text-xs truncate max-w-0" style={{ color: 'var(--text)' }}>
                        <span className="truncate" style={{ userSelect: 'text' }}>
                          {entry.function_name || `sub_${entry.address.replace(/^0x0*/i, '')}`}
                        </span>
                      </td>
                    </motion.tr>
                  ))
                )}
              </tbody>
            </table>

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
                  <span className="text-xs" style={{ color: 'var(--text-muted)' }}>Reading vtable...</span>
                </motion.div>
              )}
            </AnimatePresence>
          </div>
        )}
      </div>
    </div>
  );
}

export default VTableReader;
