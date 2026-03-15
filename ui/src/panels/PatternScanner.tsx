import { useState, useEffect, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { usePatternScan } from '../hooks/usePatternScan';
import { useModules } from '../hooks/useModules';
import { useConnection } from '../hooks/useConnection';
import { useDma } from '../hooks/useDma';
import { useContextMenu } from '../hooks/useContextMenu';
import ContextMenu from '../components/ContextMenu';
import { copyToClipboard } from '../utils/clipboard';

function PatternScanner({ onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { health } = useConnection();
  const { connected: dmaConnected } = useDma();
  const pid = health?.pid;
  const { result, loading, error, task, scanAsync } = usePatternScan();
  const { modules, refresh: refreshModules } = useModules();

  const [pattern, setPattern] = useState('');
  const [selectedModule, setSelectedModule] = useState('');
  const [maxResults, setMaxResults] = useState(50);
  const [hasScanned, setHasScanned] = useState(false);
  const { menu, show: showContextMenu, close: closeContextMenu } = useContextMenu();

  // Fetch modules when DMA connected and attached
  useEffect(() => {
    if (dmaConnected && pid) {
      refreshModules(pid);
    }
  }, [dmaConnected, pid, refreshModules]);

  const handleScan = useCallback(() => {
    const input = pattern.trim();
    if (!input || !pid) return;
    setHasScanned(true);
    scanAsync(pid, input, selectedModule || undefined);
  }, [pattern, pid, selectedModule, scanAsync]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleScan();
    }
  }, [handleScan]);

  const progressPercent = task?.progress ?? 0;
  const statusMessage = task?.status_message ?? '';
  const matches = result?.matches ?? [];
  const scanTime = result?.scan_time_ms;

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
              Scanner
            </h1>
            {result && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {matches.length} match{matches.length !== 1 ? 'es' : ''}
                {scanTime != null && ` \u00B7 ${scanTime}ms`}
              </span>
            )}
          </div>
        </div>

        {/* Pattern input */}
        <div className="relative">
          <input
            type="text"
            placeholder="48 8B 05 ?? ?? ?? ??"
            value={pattern}
            onChange={(e) => setPattern(e.target.value)}
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
            {pattern && (
              <motion.button
                onClick={() => setPattern('')}
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

          {/* Max results */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Max
            </span>
            <input
              type="number"
              value={maxResults}
              onChange={(e) => setMaxResults(Math.max(1, parseInt(e.target.value) || 1))}
              className="h-7 w-16 px-2 rounded-md text-xs font-mono outline-none"
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

          {/* Scan button */}
          <button
            onClick={handleScan}
            disabled={loading || !pid || !pattern.trim() || !dmaConnected}
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
            {loading ? 'Scanning...' : 'Scan'}
          </button>
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
                  animate={{ width: `${Math.max(progressPercent, 2)}%` }}
                  transition={{ duration: 0.3, ease: 'easeOut' }}
                />
              </div>
              <div className="flex items-center justify-between">
                <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                  {statusMessage || 'Scanning...'}
                </span>
                <span className="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                  {progressPercent}%
                </span>
              </div>
            </motion.div>
          )}
        </AnimatePresence>
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
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u29BF'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter a byte pattern to scan</p>
            <p className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>48 8B 05 ?? ?? ?? ??</p>
          </motion.div>
        ) : result && matches.length === 0 && !loading ? (
          /* Empty state: no results */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u29BF'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No matches found</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Try a different pattern or module scope</p>
          </motion.div>
        ) : matches.length > 0 ? (
          /* Results table */
          <table className="w-full text-sm">
            <thead className="sticky top-0 z-10">
              <tr style={{ background: 'var(--bg)' }}>
                {([
                  ['#', 'w-12'],
                  ['ADDRESS', 'w-44'],
                  ['MODULE+OFFSET', 'w-52'],
                  ['CONTEXT', ''],
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
              {matches.slice(0, maxResults).map((match, index) => (
                <motion.tr
                  key={match.address}
                  className="h-9 cursor-pointer group"
                  style={{
                    background: 'transparent',
                    transition: 'background 0.1s ease',
                  }}
                  onContextMenu={(e) => showContextMenu(e, [
                    { label: 'View in Memory', action: () => onNavigate?.('memory', match.address) },
                    { label: 'View in Disassembly', action: () => onNavigate?.('disassembly', match.address) },
                    { label: 'Copy Address', action: () => copyToClipboard(match.address), separator: true },
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
                    delay: Math.min(index * 0.01, 0.2),
                  }}
                  layout
                >
                  {/* Index */}
                  <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-muted)' }}>
                    {index + 1}
                  </td>
                  {/* Address */}
                  <td
                    className="px-3 py-1.5 font-mono text-xs tabular-nums"
                    style={{ color: 'var(--text-secondary)' }}
                  >
                    {match.address}
                  </td>
                  {/* Module+Offset */}
                  <td className="px-3 py-1.5 font-mono text-xs" style={{ color: 'var(--text-secondary)' }}>
                    {match.module_name || '\u2014'}
                  </td>
                  {/* Context */}
                  <td className="px-3 py-1.5 font-mono truncate max-w-0" style={{ color: 'var(--text-muted)', fontSize: '0.65rem' }}>
                    <span className="truncate">{match.context || ''}</span>
                  </td>
                </motion.tr>
              ))}
            </tbody>
          </table>
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

export default PatternScanner;
