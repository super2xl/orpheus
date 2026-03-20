import { useState, useCallback, useMemo } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useProcess } from '../hooks/useProcess';
import { useDma } from '../hooks/useDma';
import { useSnapshots } from '../hooks/useSnapshots';

// ─── Helpers ────────────────────────────────────────────────────────────────

function formatSize(bytes: number): string {
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
  if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return bytes + ' B';
}

function formatTimestamp(ts: number): string {
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function toHex(n: number): string {
  return '0x' + n.toString(16).toUpperCase().padStart(2, '0');
}

function normalizeHexInput(raw: string): string {
  const trimmed = raw.trim();
  if (trimmed.startsWith('0x') || trimmed.startsWith('0X')) return trimmed;
  return '0x' + trimmed;
}

// ─── Sub-components ──────────────────────────────────────────────────────────

function SectionHeader({ children }: { children: React.ReactNode }) {
  return (
    <div
      className="flex items-center gap-2 px-6 pt-5 pb-3"
      style={{ borderBottom: '1px solid var(--border)' }}
    >
      <h2
        className="text-xs font-medium uppercase tracking-widest"
        style={{ color: 'var(--text-muted)', letterSpacing: '0.1em' }}
      >
        {children}
      </h2>
    </div>
  );
}

interface InputProps extends React.InputHTMLAttributes<HTMLInputElement> {
  label?: string;
  monospace?: boolean;
  prefix?: string;
}

function FieldInput({ label, monospace, prefix, style, className, ...rest }: InputProps) {
  return (
    <div className="flex flex-col gap-1 min-w-0">
      {label && (
        <span
          className="text-[10px] uppercase"
          style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
        >
          {label}
        </span>
      )}
      <div className="relative">
        {prefix && (
          <span
            className="absolute left-3 top-1/2 -translate-y-1/2 text-xs font-mono pointer-events-none select-none"
            style={{ color: 'var(--text-muted)' }}
          >
            {prefix}
          </span>
        )}
        <input
          className={`w-full h-8 rounded-lg text-xs outline-none ${prefix ? 'pl-7' : 'pl-3'} pr-3 ${monospace ? 'font-mono' : ''} ${className ?? ''}`}
          style={{
            background: 'var(--surface)',
            border: '1px solid var(--border)',
            color: 'var(--text)',
            transition: 'border-color 0.1s ease',
            ...style,
          }}
          onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
          onBlur={(e) => { e.currentTarget.style.borderColor = 'var(--border)'; }}
          {...rest}
        />
      </div>
    </div>
  );
}

function ActionButton({
  children,
  onClick,
  disabled,
  variant = 'default',
}: {
  children: React.ReactNode;
  onClick?: () => void;
  disabled?: boolean;
  variant?: 'default' | 'primary' | 'danger';
}) {
  const baseStyle: React.CSSProperties = {
    transition: 'all 0.1s ease',
    fontWeight: variant === 'primary' ? 500 : 400,
    border: '1px solid var(--border)',
  };

  const variantStyle: React.CSSProperties =
    variant === 'primary'
      ? { background: 'var(--text)', color: 'var(--bg)', borderColor: 'var(--text)' }
      : variant === 'danger'
      ? { background: 'transparent', color: 'var(--text-muted)', borderColor: 'var(--border)' }
      : { background: 'var(--surface)', color: 'var(--text)', borderColor: 'var(--border)' };

  return (
    <button
      onClick={onClick}
      disabled={disabled}
      className="px-3 h-8 rounded-lg text-xs cursor-pointer border-none outline-none disabled:opacity-40 disabled:cursor-not-allowed shrink-0"
      style={{ ...baseStyle, ...variantStyle }}
      onMouseEnter={(e) => {
        if (!disabled) {
          if (variant === 'primary') {
            e.currentTarget.style.opacity = '0.85';
          } else if (variant === 'danger') {
            e.currentTarget.style.color = '#ef4444';
            e.currentTarget.style.borderColor = '#ef4444';
          } else {
            e.currentTarget.style.background = 'var(--hover)';
            e.currentTarget.style.borderColor = 'var(--text-muted)';
          }
        }
      }}
      onMouseLeave={(e) => {
        if (!disabled) {
          if (variant === 'primary') {
            e.currentTarget.style.opacity = '1';
          } else if (variant === 'danger') {
            e.currentTarget.style.color = 'var(--text-muted)';
            e.currentTarget.style.borderColor = 'var(--border)';
          } else {
            e.currentTarget.style.background = 'var(--surface)';
            e.currentTarget.style.borderColor = 'var(--border)';
          }
        }
      }}
    >
      {children}
    </button>
  );
}

// ─── Main Panel ──────────────────────────────────────────────────────────────

function MemoryDiff() {
  const { process: attachedProcess } = useProcess();
  const { connected: dmaConnected } = useDma();
  const pid = attachedProcess?.pid ?? null;

  const {
    snapshots,
    loadingSnapshots,
    snapshotError,
    takeSnapshot,
    deleteSnapshot,
    diff,
    diffLoading,
    diffError,
    runDiff,
    clearDiff,
    takingSnapshot,
    deletingSnapshot,
  } = useSnapshots();

  // ── Snapshot form state ──────────────────────────────
  const [snapAddress, setSnapAddress] = useState('');
  const [snapSize, setSnapSize] = useState('256');
  const [snapName, setSnapName] = useState('');
  const [formError, setFormError] = useState<string | null>(null);

  const handleTakeSnapshot = useCallback(async () => {
    setFormError(null);
    if (!pid) { setFormError('No process attached'); return; }
    if (!snapAddress.trim()) { setFormError('Address required'); return; }
    if (!snapName.trim()) { setFormError('Name required'); return; }
    const sizeNum = parseInt(snapSize, 10);
    if (isNaN(sizeNum) || sizeNum <= 0) { setFormError('Invalid size'); return; }

    try {
      await takeSnapshot(pid, normalizeHexInput(snapAddress), sizeNum, snapName.trim());
      setSnapName('');
      setSnapAddress('');
    } catch {
      // error surfaced from hook
    }
  }, [pid, snapAddress, snapSize, snapName, takeSnapshot]);

  // ── Diff form state ──────────────────────────────────
  const [diffName1, setDiffName1] = useState('');
  const [diffName2, setDiffName2] = useState('__current__');

  const canDiff = pid !== null && diffName1 !== '' && dmaConnected;

  const handleRunDiff = useCallback(async () => {
    if (!pid || !diffName1) return;
    const n2 = diffName2 === '__current__' ? undefined : diffName2;
    await runDiff(pid, diffName1, n2);
  }, [pid, diffName1, diffName2, runDiff]);

  // ── diff result pagination ───────────────────────────
  const PAGE_SIZE = 200;
  const [diffPage, setDiffPage] = useState(0);

  const totalPages = Math.ceil(diff.length / PAGE_SIZE);
  const pagedDiff = useMemo(
    () => diff.slice(diffPage * PAGE_SIZE, (diffPage + 1) * PAGE_SIZE),
    [diff, diffPage],
  );

  // Reset page when new diff arrives
  const prevDiffLen = useMemo(() => diff.length, [diff]);
  if (prevDiffLen !== diff.length) setDiffPage(0);

  const disabled = !pid || !dmaConnected;

  return (
    <div className="h-full flex flex-col overflow-hidden">
      {/* ── Page header ── */}
      <motion.div
        className="shrink-0 px-6 pt-6 pb-4 flex items-center justify-between"
        initial={{ opacity: 0, y: -8 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.15, ease: 'easeOut' }}
      >
        <div className="flex items-center gap-3">
          <h1
            className="text-lg tracking-tight"
            style={{ color: 'var(--text)', fontWeight: 500 }}
          >
            Memory Diff
          </h1>
          {snapshots.length > 0 && (
            <span
              className="text-xs px-2 py-0.5 rounded-md font-mono"
              style={{ color: 'var(--text-secondary)', background: 'var(--active)' }}
            >
              {snapshots.length} snapshot{snapshots.length !== 1 ? 's' : ''}
            </span>
          )}
        </div>

        {/* Guard badge */}
        {disabled && (
          <span
            className="text-[11px] px-2.5 py-1 rounded-md"
            style={{
              color: 'var(--text-muted)',
              background: 'var(--surface)',
              border: '1px solid var(--border)',
            }}
          >
            {!dmaConnected ? 'DMA not connected' : 'No process attached'}
          </span>
        )}
      </motion.div>

      {/* ── Scrollable body: two sections ── */}
      <div className="flex-1 min-h-0 overflow-y-auto pb-6">

        {/* ════════════════════════════════════════
            SECTION 1 — Snapshots Manager
        ════════════════════════════════════════ */}
        <SectionHeader>Snapshots</SectionHeader>

        {/* Take snapshot form */}
        <div className="px-6 pt-4 pb-4">
          <div className="flex items-end gap-2 flex-wrap">
            <div className="flex-1 min-w-[160px]">
              <FieldInput
                label="Address"
                monospace
                prefix="0x"
                placeholder="7FF600001000"
                value={snapAddress}
                onChange={(e) => setSnapAddress(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && handleTakeSnapshot()}
                disabled={disabled}
              />
            </div>
            <div className="w-24">
              <FieldInput
                label="Size"
                monospace
                placeholder="256"
                value={snapSize}
                onChange={(e) => setSnapSize(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && handleTakeSnapshot()}
                disabled={disabled}
              />
            </div>
            <div className="flex-1 min-w-[120px]">
              <FieldInput
                label="Name"
                placeholder="baseline"
                value={snapName}
                onChange={(e) => setSnapName(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && handleTakeSnapshot()}
                disabled={disabled}
              />
            </div>
            <div className="pb-[1px]">
              <ActionButton
                variant="primary"
                onClick={handleTakeSnapshot}
                disabled={disabled || takingSnapshot || !snapAddress.trim() || !snapName.trim()}
              >
                {takingSnapshot ? 'Saving…' : 'Take Snapshot'}
              </ActionButton>
            </div>
          </div>

          {/* Form / API error */}
          <AnimatePresence>
            {(formError || snapshotError) && (
              <motion.div
                className="mt-2 px-3 py-2 rounded-lg text-xs"
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
                {formError || snapshotError}
              </motion.div>
            )}
          </AnimatePresence>
        </div>

        {/* Snapshot list */}
        <div className="px-6">
          {loadingSnapshots && snapshots.length === 0 ? (
            <motion.p
              className="text-xs py-4 text-center"
              style={{ color: 'var(--text-muted)' }}
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
            >
              Loading snapshots…
            </motion.p>
          ) : snapshots.length === 0 ? (
            <motion.div
              className="py-8 flex flex-col items-center gap-2"
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              transition={{ delay: 0.05 }}
            >
              <span className="text-2xl" style={{ color: 'var(--text-muted)' }}>⬡</span>
              <p className="text-xs" style={{ color: 'var(--text-muted)' }}>
                No snapshots yet
              </p>
            </motion.div>
          ) : (
            <div
              className="rounded-lg overflow-hidden"
              style={{ border: '1px solid var(--border)', background: 'var(--surface)' }}
            >
              {/* Table header */}
              <div
                className="grid text-[10px] font-medium uppercase px-3 h-7 items-center"
                style={{
                  color: 'var(--text-muted)',
                  letterSpacing: '0.08em',
                  borderBottom: '1px solid var(--border)',
                  gridTemplateColumns: '1fr 11rem 6rem 6rem 5rem',
                }}
              >
                <span>Name</span>
                <span>Address</span>
                <span>Size</span>
                <span>Time</span>
                <span />
              </div>

              <AnimatePresence initial={false}>
                {snapshots.map((snap, i) => (
                  <motion.div
                    key={snap.name}
                    className="grid items-center px-3 font-mono text-xs"
                    style={{
                      height: 32,
                      gridTemplateColumns: '1fr 11rem 6rem 6rem 5rem',
                      borderTop: i > 0 ? '1px solid var(--border)' : undefined,
                      background: i % 2 === 1 ? 'var(--hover)' : 'transparent',
                    }}
                    initial={{ opacity: 0, x: -8 }}
                    animate={{ opacity: 1, x: 0 }}
                    exit={{ opacity: 0, x: 8 }}
                    transition={{ duration: 0.12, ease: 'easeOut' }}
                  >
                    <span
                      className="truncate pr-2"
                      style={{ color: 'var(--text)', fontFamily: 'inherit', fontWeight: 500 }}
                    >
                      {snap.name}
                    </span>
                    <span style={{ color: 'var(--text-secondary)' }} className="tabular-nums">
                      {snap.address}
                    </span>
                    <span style={{ color: 'var(--text-secondary)' }} className="tabular-nums">
                      {formatSize(snap.size)}
                    </span>
                    <span style={{ color: 'var(--text-muted)' }} className="tabular-nums">
                      {formatTimestamp(snap.timestamp)}
                    </span>
                    <div className="flex justify-end">
                      <button
                        onClick={() => deleteSnapshot(snap.name)}
                        disabled={deletingSnapshot === snap.name}
                        className="text-[11px] px-2 py-0.5 rounded cursor-pointer border-none outline-none disabled:opacity-40"
                        style={{
                          color: 'var(--text-muted)',
                          background: 'transparent',
                          fontFamily: 'inherit',
                          transition: 'color 0.1s ease',
                        }}
                        onMouseEnter={(e) => { e.currentTarget.style.color = '#ef4444'; }}
                        onMouseLeave={(e) => { e.currentTarget.style.color = 'var(--text-muted)'; }}
                        title="Delete snapshot"
                      >
                        {deletingSnapshot === snap.name ? '…' : '✕'}
                      </button>
                    </div>
                  </motion.div>
                ))}
              </AnimatePresence>
            </div>
          )}
        </div>

        {/* ════════════════════════════════════════
            SECTION 2 — Diff Viewer
        ════════════════════════════════════════ */}
        <SectionHeader>Diff Viewer</SectionHeader>

        {/* Diff controls */}
        <div className="px-6 pt-4 pb-4">
          <div className="flex items-end gap-2 flex-wrap">
            {/* Snapshot A */}
            <div className="flex flex-col gap-1 min-w-[150px] flex-1">
              <span
                className="text-[10px] uppercase"
                style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
              >
                Snapshot A
              </span>
              <select
                value={diffName1}
                onChange={(e) => { setDiffName1(e.target.value); clearDiff(); }}
                className="h-8 px-3 rounded-lg text-xs outline-none cursor-pointer"
                style={{
                  background: 'var(--surface)',
                  border: '1px solid var(--border)',
                  color: diffName1 ? 'var(--text)' : 'var(--text-muted)',
                  transition: 'border-color 0.1s ease',
                  appearance: 'none',
                  WebkitAppearance: 'none',
                }}
                onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
                onBlur={(e) => { e.currentTarget.style.borderColor = 'var(--border)'; }}
                disabled={disabled}
              >
                <option value="">Select snapshot…</option>
                {snapshots.map((s) => (
                  <option key={s.name} value={s.name}>{s.name}</option>
                ))}
              </select>
            </div>

            {/* VS divider */}
            <span
              className="text-xs pb-1.5 select-none"
              style={{ color: 'var(--text-muted)' }}
            >
              vs
            </span>

            {/* Snapshot B */}
            <div className="flex flex-col gap-1 min-w-[150px] flex-1">
              <span
                className="text-[10px] uppercase"
                style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
              >
                Snapshot B
              </span>
              <select
                value={diffName2}
                onChange={(e) => { setDiffName2(e.target.value); clearDiff(); }}
                className="h-8 px-3 rounded-lg text-xs outline-none cursor-pointer"
                style={{
                  background: 'var(--surface)',
                  border: '1px solid var(--border)',
                  color: 'var(--text)',
                  transition: 'border-color 0.1s ease',
                  appearance: 'none',
                  WebkitAppearance: 'none',
                }}
                onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
                onBlur={(e) => { e.currentTarget.style.borderColor = 'var(--border)'; }}
                disabled={disabled}
              >
                <option value="__current__">Current Memory</option>
                {snapshots.map((s) => (
                  <option key={s.name} value={s.name}>{s.name}</option>
                ))}
              </select>
            </div>

            <div className="pb-[1px]">
              <ActionButton
                variant="primary"
                onClick={handleRunDiff}
                disabled={!canDiff || diffLoading}
              >
                {diffLoading ? 'Comparing…' : 'Compare'}
              </ActionButton>
            </div>

            {diff.length > 0 && (
              <div className="pb-[1px]">
                <ActionButton onClick={clearDiff}>Clear</ActionButton>
              </div>
            )}
          </div>

          {/* Diff error */}
          <AnimatePresence>
            {diffError && (
              <motion.div
                className="mt-2 px-3 py-2 rounded-lg text-xs"
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
                {diffError}
              </motion.div>
            )}
          </AnimatePresence>
        </div>

        {/* Diff results */}
        <div className="px-6">
          <AnimatePresence mode="wait">
            {diffLoading ? (
              <motion.div
                key="loading"
                className="py-10 flex flex-col items-center gap-2"
                initial={{ opacity: 0 }}
                animate={{ opacity: 1 }}
                exit={{ opacity: 0 }}
                transition={{ duration: 0.1 }}
              >
                <motion.div
                  animate={{ rotate: 360 }}
                  transition={{ duration: 1, repeat: Infinity, ease: 'linear' }}
                  className="w-4 h-4 rounded-full border-t-2"
                  style={{ borderColor: 'var(--text-muted)' }}
                />
                <p className="text-xs" style={{ color: 'var(--text-muted)' }}>
                  Comparing memory…
                </p>
              </motion.div>
            ) : diff.length > 0 ? (
              <motion.div
                key="results"
                initial={{ opacity: 0, y: 4 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0 }}
                transition={{ duration: 0.15 }}
              >
                {/* Summary bar */}
                <div className="flex items-center justify-between mb-3">
                  <div className="flex items-center gap-3">
                    <span
                      className="text-xs font-medium"
                      style={{ color: 'var(--text-secondary)' }}
                    >
                      {diff.length.toLocaleString()} change{diff.length !== 1 ? 's' : ''}
                    </span>
                    <div className="flex items-center gap-1.5">
                      <span
                        className="inline-block w-2 h-2 rounded-sm"
                        style={{ background: 'rgba(239,68,68,0.35)' }}
                      />
                      <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                        old
                      </span>
                    </div>
                    <div className="flex items-center gap-1.5">
                      <span
                        className="inline-block w-2 h-2 rounded-sm"
                        style={{ background: 'rgba(34,197,94,0.35)' }}
                      />
                      <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                        new
                      </span>
                    </div>
                  </div>

                  {/* Pagination */}
                  {totalPages > 1 && (
                    <div className="flex items-center gap-2">
                      <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                        Page {diffPage + 1} / {totalPages}
                      </span>
                      <button
                        onClick={() => setDiffPage((p) => Math.max(0, p - 1))}
                        disabled={diffPage === 0}
                        className="w-6 h-6 rounded text-xs cursor-pointer border-none outline-none disabled:opacity-30"
                        style={{
                          background: 'var(--surface)',
                          color: 'var(--text-secondary)',
                          border: '1px solid var(--border)',
                        }}
                      >
                        ‹
                      </button>
                      <button
                        onClick={() => setDiffPage((p) => Math.min(totalPages - 1, p + 1))}
                        disabled={diffPage >= totalPages - 1}
                        className="w-6 h-6 rounded text-xs cursor-pointer border-none outline-none disabled:opacity-30"
                        style={{
                          background: 'var(--surface)',
                          color: 'var(--text-secondary)',
                          border: '1px solid var(--border)',
                        }}
                      >
                        ›
                      </button>
                    </div>
                  )}
                </div>

                {/* Table */}
                <div
                  className="rounded-lg overflow-hidden"
                  style={{ border: '1px solid var(--border)', background: 'var(--surface)' }}
                >
                  {/* Table header */}
                  <div
                    className="grid text-[10px] font-medium uppercase px-3 h-7 items-center"
                    style={{
                      color: 'var(--text-muted)',
                      letterSpacing: '0.08em',
                      borderBottom: '1px solid var(--border)',
                      gridTemplateColumns: '1fr 1fr 1fr',
                    }}
                  >
                    <span>Offset</span>
                    <span>Old Value</span>
                    <span>New Value</span>
                  </div>

                  {pagedDiff.map((entry, i) => (
                    <motion.div
                      key={`${entry.offset}-${i}`}
                      className="grid items-center px-3 font-mono text-xs tabular-nums"
                      style={{
                        height: 28,
                        gridTemplateColumns: '1fr 1fr 1fr',
                        borderTop: i > 0 ? '1px solid var(--border)' : undefined,
                      }}
                      initial={{ opacity: 0 }}
                      animate={{ opacity: 1 }}
                      transition={{ duration: 0.06, delay: Math.min(i * 0.004, 0.12) }}
                    >
                      <span style={{ color: 'var(--text-secondary)' }}>
                        +{entry.offset.toString(16).toUpperCase().padStart(4, '0')}
                      </span>
                      <span
                        className="rounded px-1.5 py-0.5 w-fit"
                        style={{
                          color: '#ef4444',
                          background: 'rgba(239,68,68,0.12)',
                        }}
                      >
                        {toHex(entry.old_value)}
                      </span>
                      <span
                        className="rounded px-1.5 py-0.5 w-fit"
                        style={{
                          color: '#22c55e',
                          background: 'rgba(34,197,94,0.12)',
                        }}
                      >
                        {toHex(entry.new_value)}
                      </span>
                    </motion.div>
                  ))}
                </div>
              </motion.div>
            ) : diffName1 ? (
              <motion.div
                key="empty-selected"
                className="py-8 flex flex-col items-center gap-2"
                initial={{ opacity: 0 }}
                animate={{ opacity: 1 }}
                exit={{ opacity: 0 }}
                transition={{ duration: 0.1 }}
              >
                <span className="text-2xl" style={{ color: 'var(--text-muted)' }}>∅</span>
                <p className="text-xs" style={{ color: 'var(--text-muted)' }}>
                  Hit Compare to run the diff
                </p>
              </motion.div>
            ) : (
              <motion.div
                key="idle"
                className="py-8 flex flex-col items-center gap-2"
                initial={{ opacity: 0 }}
                animate={{ opacity: 1 }}
                exit={{ opacity: 0 }}
                transition={{ duration: 0.1, delay: 0.05 }}
              >
                <span className="text-2xl" style={{ color: 'var(--text-muted)' }}>⊕</span>
                <p className="text-xs" style={{ color: 'var(--text-muted)' }}>
                  Select a snapshot to compare
                </p>
              </motion.div>
            )}
          </AnimatePresence>
        </div>
      </div>
    </div>
  );
}

export default MemoryDiff;
