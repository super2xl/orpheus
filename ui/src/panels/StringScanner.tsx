import { useState, useEffect, useCallback, useMemo } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useStringScan } from '../hooks/useStringScan';
import { useModules } from '../hooks/useModules';
import { useProcess } from '../hooks/useProcess';
import { useDma } from '../hooks/useDma';
import { useContextMenu } from '../hooks/useContextMenu';
import ContextMenu from '../components/ContextMenu';
import { copyToClipboard } from '../utils/clipboard';
import { useToast } from '../hooks/useToast';

function StringScanner({ onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { process: attachedProcess } = useProcess();
  const { connected: dmaConnected } = useDma();
  const { toast } = useToast();
  const pid = attachedProcess?.pid;
  const { result, loading, error, task, scanAsync, cancel } = useStringScan();
  const { modules, refresh: refreshModules } = useModules();

  const [searchText, setSearchText] = useState('');
  const [selectedModule, setSelectedModule] = useState('');
  const [minLength, setMinLength] = useState(4);
  const [scanAscii, setScanAscii] = useState(true);
  const [scanUtf16, setScanUtf16] = useState(true);
  const [filter, setFilter] = useState('');
  const [hasScanned, setHasScanned] = useState(false);
  const { menu, show: showContextMenu, close: closeContextMenu } = useContextMenu();

  // Fetch modules when DMA connected and attached
  useEffect(() => {
    if (dmaConnected && pid) {
      refreshModules(pid);
    }
  }, [dmaConnected, pid, refreshModules]);

  const handleScan = useCallback(() => {
    if (!pid) return;
    setHasScanned(true);

    // Determine scan scope from selected module
    const mod = modules.find((m) => m.name === selectedModule);
    const base = mod ? mod.base : '0x0';
    const size = mod ? mod.size : 0;

    scanAsync({
      pid,
      base,
      size,
      min_length: minLength,
      scan_ascii: scanAscii,
      scan_utf16: scanUtf16,
      ...(searchText.trim() && { contains: searchText.trim() }),
    });
  }, [pid, selectedModule, modules, minLength, scanAscii, scanUtf16, searchText, scanAsync]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleScan();
    }
  }, [handleScan]);

  const progressPercent = task?.progress ?? 0;
  const statusMessage = task?.status_message ?? '';
  const matches = result?.strings ?? [];

  // Client-side filter on found strings
  const filtered = useMemo(() => {
    const q = filter.toLowerCase().trim();
    if (!q) return matches;
    return matches.filter(
      (m) =>
        m.value.toLowerCase().includes(q) ||
        m.address.toLowerCase().includes(q)
    );
  }, [matches, filter]);

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
              Strings
            </h1>
            {result && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {filtered.length}
                {filter && ` / ${matches.length}`}
                {' '}string{filtered.length !== 1 ? 's' : ''}
              </span>
            )}
          </div>
        </div>

        {/* Search input */}
        <div className="relative">
          <input
            type="text"
            placeholder="Search text (leave empty to scan all strings)"
            value={searchText}
            onChange={(e) => setSearchText(e.target.value)}
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
            {searchText && (
              <motion.button
                onClick={() => setSearchText('')}
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
              <option value="">All Modules</option>
              {modules.map((m) => (
                <option key={m.base} value={m.name}>
                  {m.name}
                </option>
              ))}
            </select>
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Min length */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Min Length
            </span>
            <input
              type="number"
              value={minLength}
              onChange={(e) => setMinLength(Math.max(1, parseInt(e.target.value) || 1))}
              className="h-7 w-14 px-2 rounded-md text-xs font-mono outline-none"
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

          {/* Encoding checkboxes */}
          <div className="flex items-center gap-3">
            <label
              className="flex items-center gap-1.5 cursor-pointer text-xs select-none"
              style={{ color: scanAscii ? 'var(--text)' : 'var(--text-muted)', transition: 'color 0.1s ease' }}
            >
              <input
                type="checkbox"
                checked={scanAscii}
                onChange={(e) => setScanAscii(e.target.checked)}
                className="cursor-pointer accent-current"
              />
              ASCII
            </label>
            <label
              className="flex items-center gap-1.5 cursor-pointer text-xs select-none"
              style={{ color: scanUtf16 ? 'var(--text)' : 'var(--text-muted)', transition: 'color 0.1s ease' }}
            >
              <input
                type="checkbox"
                checked={scanUtf16}
                onChange={(e) => setScanUtf16(e.target.checked)}
                className="cursor-pointer accent-current"
              />
              UTF-16
            </label>
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Scan / Cancel buttons */}
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
              disabled={!pid || (!scanAscii && !scanUtf16) || !dmaConnected}
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
              Scan
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
                  animate={{ width: `${Math.max(progressPercent, 2)}%` }}
                  transition={{ duration: 0.3, ease: 'easeOut' }}
                />
              </div>
              <div className="flex items-center justify-between">
                <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                  {statusMessage || 'Scanning strings...'}
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
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>T</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Scan for readable strings in memory</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Select a module and click Scan</p>
          </motion.div>
        ) : result && matches.length === 0 && !loading ? (
          /* Empty state: no results */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>T</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No strings found</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Try a different module or lower the minimum length</p>
          </motion.div>
        ) : matches.length > 0 ? (
          <>
            {/* Filter input */}
            <div className="relative mb-3">
              <span
                className="absolute left-3 top-1/2 -translate-y-1/2 text-sm pointer-events-none"
                style={{ color: 'var(--text-muted)' }}
              >
                {'\u2315'}
              </span>
              <input
                type="text"
                placeholder="Filter results..."
                value={filter}
                onChange={(e) => setFilter(e.target.value)}
                className="w-full h-8 pl-8 pr-3 rounded-lg text-xs outline-none"
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

            {/* Results table */}
            <table className="w-full text-sm">
              <thead className="sticky top-0 z-10">
                <tr style={{ background: 'var(--bg)' }}>
                  {([
                    ['ADDRESS', 'w-44'],
                    ['STRING', ''],
                    ['TYPE', 'w-24'],
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
                {filtered.map((match, index) => (
                  <motion.tr
                    key={`${match.address}-${index}`}
                    className="h-9 cursor-pointer group"
                    style={{
                      background: 'transparent',
                      transition: 'background 0.1s ease',
                    }}
                    onContextMenu={(e) => showContextMenu(e, [
                      { label: 'View in Memory', action: () => onNavigate?.('memory', match.address) },
                      { label: 'View in Disassembly', action: () => onNavigate?.('disassembly', match.address) },
                      { label: 'Copy Address', action: () => { copyToClipboard(match.address); toast('Address copied to clipboard'); }, separator: true },
                      { label: 'Copy String', action: () => { copyToClipboard(match.value); toast('String copied to clipboard'); } },
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
                    {/* Address */}
                    <td
                      className="px-3 py-1.5 font-mono text-xs tabular-nums"
                      style={{ color: 'var(--text-secondary)' }}
                    >
                      {match.address}
                    </td>
                    {/* String */}
                    <td className="px-3 py-1.5 font-mono text-xs truncate max-w-0" style={{ color: 'var(--syn-string)' }}>
                      <span className="truncate">{match.value}</span>
                    </td>
                    {/* Type */}
                    <td className="px-3 py-1.5">
                      <span
                        className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                        style={{
                          fontWeight: 400,
                          background: 'var(--active)',
                          color: 'var(--text-secondary)',
                        }}
                      >
                        {match.type === 'UTF16_LE' ? 'UTF-16' : match.type}
                      </span>
                    </td>
                  </motion.tr>
                ))}
              </tbody>
            </table>
          </>
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

export default StringScanner;
