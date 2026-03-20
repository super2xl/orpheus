import { useState, useEffect, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useProcess } from '../hooks/useProcess';
import { useDma } from '../hooks/useDma';
import { useCS2 } from '../hooks/useCS2';
import type { CS2Player, CS2Field, CS2InspectResult } from '../hooks/useCS2';

// ── Shared button style helpers ───────────────────────────────────────────────

function actionBtnStyle(disabled?: boolean) {
  return {
    background: 'transparent',
    color: disabled ? 'var(--text-muted)' : 'var(--text-secondary)',
    border: '1px solid var(--border)',
    transition: 'all 0.1s ease',
    opacity: disabled ? 0.4 : 1,
  } as React.CSSProperties;
}

// ── Tab bar ───────────────────────────────────────────────────────────────────

type TabId = 'overview' | 'players' | 'inspector';

const TABS: { id: TabId; label: string }[] = [
  { id: 'overview', label: 'Overview' },
  { id: 'players', label: 'Players' },
  { id: 'inspector', label: 'Entity Inspector' },
];

// ── Team badge ────────────────────────────────────────────────────────────────

function TeamBadge({ team }: { team: number }) {
  // team 2 = T, team 3 = CT
  const label = team === 3 ? 'CT' : team === 2 ? 'T' : '—';
  const color = team === 3 ? '#60a5fa' : team === 2 ? '#f59e0b' : 'var(--text-muted)';
  return (
    <span
      className="text-[10px] font-mono px-1.5 py-0.5 rounded"
      style={{ background: 'var(--active)', color }}
    >
      {label}
    </span>
  );
}

// ── Health bar ────────────────────────────────────────────────────────────────

function HealthBar({ value }: { value: number }) {
  const pct = Math.max(0, Math.min(100, value));
  const color = pct > 60 ? '#22c55e' : pct > 30 ? '#f59e0b' : '#ef4444';
  return (
    <div className="flex items-center gap-2">
      <div
        className="flex-1 h-1.5 rounded-full overflow-hidden"
        style={{ background: 'var(--border)', maxWidth: 60 }}
      >
        <div
          className="h-full rounded-full"
          style={{ width: `${pct}%`, background: color, transition: 'width 0.3s ease' }}
        />
      </div>
      <span className="font-mono text-xs tabular-nums" style={{ color: 'var(--text-secondary)', minWidth: 28, textAlign: 'right' }}>
        {value}
      </span>
    </div>
  );
}

// ── Field tree node ───────────────────────────────────────────────────────────

function FieldRow({
  field,
  pid,
  address,
  onReadField,
}: {
  field: CS2Field;
  pid: number;
  address: string;
  onReadField: (className: string, fieldName: string) => Promise<void>;
}) {
  const [expanded, setExpanded] = useState(false);
  const hasChildren =
    field.value !== null &&
    field.value !== undefined &&
    typeof field.value === 'object' &&
    Object.keys(field.value as object).length > 0;
  const className = (field as any).class_name as string | undefined;

  const displayValue = (() => {
    if (field.value === null || field.value === undefined) return '—';
    if (typeof field.value === 'object') return '{...}';
    return String(field.value);
  })();

  return (
    <div>
      <div
        className="flex items-center gap-2 px-2 py-1 rounded group"
        style={{ transition: 'background 0.08s' }}
        onMouseEnter={(e) => { (e.currentTarget as HTMLDivElement).style.background = 'var(--hover)'; }}
        onMouseLeave={(e) => { (e.currentTarget as HTMLDivElement).style.background = 'transparent'; }}
      >
        {/* expand toggle */}
        <button
          className="w-3 h-3 flex items-center justify-center shrink-0 border-none outline-none cursor-pointer"
          style={{ background: 'transparent', color: 'var(--text-muted)', visibility: hasChildren ? 'visible' : 'hidden' }}
          onClick={() => setExpanded((v) => !v)}
        >
          <motion.span
            className="text-[8px] leading-none"
            animate={{ rotate: expanded ? 90 : 0 }}
            transition={{ duration: 0.1 }}
          >
            {'\u25B8'}
          </motion.span>
        </button>

        {/* offset */}
        {field.offset !== undefined && (
          <span className="font-mono text-[10px] tabular-nums w-14 shrink-0" style={{ color: 'var(--text-muted)' }}>
            +0x{(field.offset as number).toString(16).toUpperCase().padStart(4, '0')}
          </span>
        )}

        {/* field name */}
        <span className="font-mono text-xs truncate" style={{ color: 'var(--text)', minWidth: 120 }}>
          {field.name}
        </span>

        {/* type */}
        <span
          className="font-mono text-[10px] px-1 py-0.5 rounded shrink-0"
          style={{ color: 'var(--text-muted)', background: 'var(--active)' }}
        >
          {field.type}
        </span>

        {/* value */}
        <span className="font-mono text-xs tabular-nums flex-1 text-right truncate" style={{ color: '#86efac', userSelect: 'text' }}>
          {displayValue}
        </span>

        {/* read field button */}
        {className && (
          <button
            className="text-[10px] px-1.5 py-0.5 rounded border-none outline-none cursor-pointer opacity-0 group-hover:opacity-100 shrink-0"
            style={{ background: 'var(--active)', color: 'var(--text-muted)', transition: 'opacity 0.1s, color 0.1s' }}
            onClick={() => onReadField(className, field.name)}
            onMouseEnter={(e) => { e.currentTarget.style.color = 'var(--text)'; }}
            onMouseLeave={(e) => { e.currentTarget.style.color = 'var(--text-muted)'; }}
            title="Re-read this field"
          >
            Read
          </button>
        )}
      </div>

      {/* nested fields */}
      <AnimatePresence>
        {expanded && hasChildren && (
          <motion.div
            className="pl-6"
            initial={{ height: 0, opacity: 0 }}
            animate={{ height: 'auto', opacity: 1 }}
            exit={{ height: 0, opacity: 0 }}
            transition={{ duration: 0.12 }}
          >
            {Object.entries(field.value as unknown as Record<string, unknown>).map(([k, v]) => (
              <FieldRow
                key={k}
                field={{ name: k, type: typeof v, value: v as any }}
                pid={pid}
                address={address}
                onReadField={onReadField}
              />
            ))}
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  );
}

// ── InspectView ───────────────────────────────────────────────────────────────

function InspectView({
  pid,
  result,
  loading,
  error,
  onReadField,
}: {
  pid: number;
  result: CS2InspectResult | null;
  loading: boolean;
  error: string | null;
  onReadField: (className: string, fieldName: string) => Promise<void>;
}) {
  if (loading) {
    return (
      <div className="space-y-1.5 p-4">
        {Array.from({ length: 6 }, (_, i) => (
          <motion.div
            key={i}
            className="h-5 rounded"
            style={{ width: `${60 + Math.random() * 30}%`, background: 'var(--skeleton)' }}
            animate={{ opacity: [0.3, 0.5, 0.3] }}
            transition={{ duration: 2, repeat: Infinity, ease: 'easeInOut', delay: i * 0.08 }}
          />
        ))}
      </div>
    );
  }

  if (error) {
    return (
      <div className="mx-4 mt-2 px-3 py-2 rounded-lg text-xs" style={{ background: 'var(--surface)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}>
        {error}
      </div>
    );
  }

  if (!result) return null;

  const fields: CS2Field[] = Array.isArray(result.fields)
    ? result.fields
    : Object.entries(result).filter(([k]) => k !== 'address' && k !== 'class_name' && k !== 'type_name').map(([k, v]) => ({
        name: k,
        type: typeof v,
        value: v as any,
      }));

  return (
    <div className="rounded-lg overflow-hidden" style={{ border: '1px solid var(--border)' }}>
      {/* header */}
      <div className="px-3 py-2 flex items-center gap-3" style={{ background: 'var(--surface)', borderBottom: '1px solid var(--border)' }}>
        <span className="font-mono text-xs font-medium" style={{ color: 'var(--text)' }}>
          {result.class_name || result.type_name || 'Object'}
        </span>
        <span className="font-mono text-[10px]" style={{ color: 'var(--text-muted)' }}>
          {result.address}
        </span>
        <span
          className="text-[10px] px-1.5 py-0.5 rounded font-mono"
          style={{ background: 'var(--active)', color: 'var(--text-secondary)' }}
        >
          {fields.length} fields
        </span>
      </div>
      {/* fields */}
      <div className="max-h-96 overflow-auto py-1" style={{ background: 'var(--bg)' }}>
        {fields.length === 0 ? (
          <p className="text-xs px-4 py-3" style={{ color: 'var(--text-muted)' }}>No fields returned</p>
        ) : (
          fields.map((f, i) => (
            <FieldRow
              key={f.name + i}
              field={f}
              pid={pid}
              address={result.address}
              onReadField={onReadField}
            />
          ))
        )}
      </div>
    </div>
  );
}

// ── Overview Tab ──────────────────────────────────────────────────────────────

function OverviewTab({ pid }: { pid: number }) {
  const { init, initLoading, initError, initialized, initResult, gameState, gameStateLoading, startGameStatePolling, stopGameStatePolling } = useCS2();

  useEffect(() => {
    if (!initialized) return;
    const stop = startGameStatePolling(pid);
    return stop;
  }, [initialized, pid, startGameStatePolling]);

  useEffect(() => {
    return () => stopGameStatePolling();
  }, [stopGameStatePolling]);

  const handleInit = useCallback(() => {
    init(pid);
  }, [pid, init]);

  const stateEntries = gameState
    ? Object.entries(gameState).filter(([, v]) => v !== null && v !== undefined && v !== '')
    : [];

  return (
    <div className="p-6 space-y-6">
      {/* Init card */}
      <div className="rounded-lg p-4 space-y-4" style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}>
        <div className="flex items-center justify-between">
          <div className="space-y-0.5">
            <p className="text-sm font-medium" style={{ color: 'var(--text)' }}>CS2 Initialization</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>
              Loads schema system and RTTI data from the game process
            </p>
          </div>
          <div className="flex items-center gap-2">
            {/* Status dot */}
            <div
              className="w-2 h-2 rounded-full"
              style={{ background: initialized ? 'var(--dot-connected)' : 'var(--dot-disconnected)' }}
            />
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              {initialized ? 'Initialized' : 'Not initialized'}
            </span>
          </div>
        </div>

        <div className="flex items-center gap-3">
          <button
            onClick={handleInit}
            disabled={initLoading}
            className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none disabled:opacity-40"
            style={actionBtnStyle(initLoading)}
            onMouseEnter={(e) => { if (!initLoading) { e.currentTarget.style.background = 'var(--hover)'; e.currentTarget.style.color = 'var(--text)'; } }}
            onMouseLeave={(e) => { e.currentTarget.style.background = 'transparent'; e.currentTarget.style.color = 'var(--text-secondary)'; }}
          >
            {initLoading ? 'Initializing...' : initialized ? 'Re-initialize' : 'Initialize CS2'}
          </button>

          {initResult && (
            <div className="flex items-center gap-2">
              <span
                className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                style={{ background: 'var(--active)', color: 'var(--text-secondary)' }}
              >
                {initResult.class_count ?? '—'} classes
              </span>
              <span
                className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                style={{ background: 'var(--active)', color: 'var(--text-secondary)' }}
              >
                {initResult.schema_count ?? '—'} schema
              </span>
            </div>
          )}
        </div>

        <AnimatePresence>
          {initError && (
            <motion.p
              className="text-xs"
              style={{ color: 'var(--text-secondary)' }}
              initial={{ opacity: 0, height: 0 }}
              animate={{ opacity: 1, height: 'auto' }}
              exit={{ opacity: 0, height: 0 }}
            >
              {initError}
            </motion.p>
          )}
        </AnimatePresence>
      </div>

      {/* Game state card */}
      <AnimatePresence>
        {initialized && (
          <motion.div
            className="rounded-lg overflow-hidden"
            style={{ border: '1px solid var(--border)' }}
            initial={{ opacity: 0, y: 6 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0 }}
            transition={{ duration: 0.15 }}
          >
            <div
              className="px-4 py-2.5 flex items-center justify-between"
              style={{ background: 'var(--surface)', borderBottom: '1px solid var(--border)' }}
            >
              <span className="text-xs font-medium" style={{ color: 'var(--text)' }}>Game State</span>
              {gameStateLoading && (
                <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>refreshing…</span>
              )}
            </div>
            <div className="p-4" style={{ background: 'var(--bg)' }}>
              {stateEntries.length === 0 ? (
                <p className="text-xs" style={{ color: 'var(--text-muted)' }}>
                  {gameStateLoading ? 'Loading...' : 'No game state data'}
                </p>
              ) : (
                <div className="grid grid-cols-2 gap-x-8 gap-y-2">
                  {stateEntries.map(([key, value]) => (
                    <div key={key} className="flex items-baseline gap-2 min-w-0">
                      <span
                        className="text-[10px] uppercase shrink-0"
                        style={{ color: 'var(--text-muted)', letterSpacing: '0.07em', fontWeight: 400, minWidth: 72 }}
                      >
                        {key.replace(/_/g, ' ')}
                      </span>
                      <span className="font-mono text-xs truncate" style={{ color: 'var(--text-secondary)', userSelect: 'text' }}>
                        {String(value)}
                      </span>
                    </div>
                  ))}
                </div>
              )}
            </div>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Not-initialized placeholder */}
      {!initialized && !initLoading && (
        <motion.div
          className="flex flex-col items-center justify-center gap-3 py-16"
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          transition={{ delay: 0.1 }}
        >
          <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'⊞'}</div>
          <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>CS2 not initialized</p>
          <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Click Initialize CS2 to load schema and class data</p>
        </motion.div>
      )}
    </div>
  );
}

// ── Players Tab ───────────────────────────────────────────────────────────────

function PlayersTab({ pid, onInspect }: { pid: number; onInspect: (address: string) => void }) {
  const {
    players,
    playersLoading,
    playersError,
    startPlayerPolling,
    stopPlayerPolling,
    inspect,
    inspectResult,
    inspectLoading,
    inspectError,
  } = useCS2();
  const [selectedAddress, setSelectedAddress] = useState<string | null>(null);

  useEffect(() => {
    const stop = startPlayerPolling(pid);
    return () => {
      stop();
      stopPlayerPolling();
    };
  }, [pid, startPlayerPolling, stopPlayerPolling]);

  const handlePlayerClick = useCallback(async (player: CS2Player) => {
    const addr = player.address || player.controller_address || player.pawn_address;
    if (!addr) return;
    if (selectedAddress === addr) {
      setSelectedAddress(null);
      return;
    }
    setSelectedAddress(addr);
    await inspect(pid, addr);
  }, [selectedAddress, pid, inspect]);

  return (
    <div className="h-full flex flex-col">
      {/* Error banner */}
      <AnimatePresence>
        {playersError && (
          <motion.div
            className="mx-6 mt-4 px-3 py-2 rounded-lg text-xs"
            style={{ background: 'var(--surface)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}
            initial={{ opacity: 0, height: 0 }}
            animate={{ opacity: 1, height: 'auto' }}
            exit={{ opacity: 0, height: 0 }}
          >
            {playersError}
          </motion.div>
        )}
      </AnimatePresence>

      {/* Players table */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pt-4 pb-2">
        {players.length === 0 ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'◈'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>
              {playersLoading ? 'Loading players...' : 'No players found'}
            </p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>
              Make sure CS2 is initialized and a match is active
            </p>
          </motion.div>
        ) : (
          <>
            <table className="w-full text-sm">
              <thead className="sticky top-0 z-10">
                <tr style={{ background: 'var(--bg)' }}>
                  {(['TEAM', 'NAME', 'HP', 'ARMOR', 'K', 'D', '$', 'STEAMID'] as const).map((label) => (
                    <th
                      key={label}
                      className="text-left text-[10px] uppercase px-3 py-2.5 select-none"
                      style={{ fontWeight: 400, letterSpacing: '0.08em', color: 'var(--text-muted)', borderBottom: '1px solid var(--border)' }}
                    >
                      {label}
                    </th>
                  ))}
                </tr>
              </thead>
              <tbody>
                {players.map((player, idx) => {
                  const addr = player.address || player.controller_address || player.pawn_address;
                  const isSelected = addr ? selectedAddress === addr : false;
                  const isLocal = player.is_local;
                  return (
                    <motion.tr
                      key={player.index ?? idx}
                      onClick={() => handlePlayerClick(player)}
                      className="cursor-pointer"
                      style={{
                        background: isSelected ? 'var(--active)' : 'transparent',
                        transition: 'background 0.1s ease',
                        outline: isLocal ? '1px solid var(--border)' : 'none',
                      }}
                      onMouseEnter={(e) => { if (!isSelected) e.currentTarget.style.background = 'var(--hover)'; }}
                      onMouseLeave={(e) => { if (!isSelected) e.currentTarget.style.background = 'transparent'; }}
                      initial={{ opacity: 0, x: -4 }}
                      animate={{ opacity: 1, x: 0 }}
                      transition={{ duration: 0.1, delay: Math.min(idx * 0.02, 0.2) }}
                    >
                      <td className="px-3 py-1.5"><TeamBadge team={player.team} /></td>
                      <td className="px-3 py-1.5 font-mono text-xs max-w-[160px] truncate">
                        <span style={{ color: isLocal ? '#86efac' : 'var(--text)' }}>
                          {player.name || '—'}
                          {isLocal && <span className="ml-1.5 text-[10px]" style={{ color: '#86efac' }}>(you)</span>}
                        </span>
                      </td>
                      <td className="px-3 py-1.5 min-w-[100px]">
                        <HealthBar value={player.health ?? 0} />
                      </td>
                      <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-secondary)' }}>
                        {player.armor ?? '—'}
                      </td>
                      <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-secondary)' }}>
                        {player.kills ?? '—'}
                      </td>
                      <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-secondary)' }}>
                        {player.deaths ?? '—'}
                      </td>
                      <td className="px-3 py-1.5 font-mono text-xs tabular-nums" style={{ color: 'var(--text-secondary)' }}>
                        {player.money !== undefined ? `$${player.money}` : '—'}
                      </td>
                      <td className="px-3 py-1.5 font-mono text-[10px] tabular-nums" style={{ color: 'var(--text-muted)' }}>
                        {player.steam_id64 || player.steam_id || '—'}
                      </td>
                    </motion.tr>
                  );
                })}
              </tbody>
            </table>

            {/* Player inspect panel */}
            <AnimatePresence>
              {selectedAddress && (
                <motion.div
                  className="mt-4"
                  initial={{ opacity: 0, height: 0 }}
                  animate={{ opacity: 1, height: 'auto' }}
                  exit={{ opacity: 0, height: 0 }}
                  transition={{ duration: 0.15 }}
                >
                  <div className="flex items-center justify-between mb-2">
                    <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em' }}>
                      Entity Detail
                    </span>
                    <button
                      className="text-xs cursor-pointer border-none outline-none px-2 py-1 rounded"
                      style={{ background: 'transparent', color: 'var(--text-muted)', transition: 'color 0.1s' }}
                      onClick={() => onInspect(selectedAddress)}
                      onMouseEnter={(e) => { e.currentTarget.style.color = 'var(--text)'; }}
                      onMouseLeave={(e) => { e.currentTarget.style.color = 'var(--text-muted)'; }}
                    >
                      Open in Inspector →
                    </button>
                  </div>
                  <InspectView
                    pid={pid}
                    result={inspectResult}
                    loading={inspectLoading}
                    error={inspectError}
                    onReadField={async (className, fieldName) => {
                      // read-field updates are shown inline via the field's value
                      void className; void fieldName;
                    }}
                  />
                </motion.div>
              )}
            </AnimatePresence>
          </>
        )}
      </div>

      {/* Footer: refresh indicator */}
      <div className="px-6 py-2 shrink-0 flex items-center gap-2" style={{ borderTop: '1px solid var(--border)' }}>
        <div
          className="w-1.5 h-1.5 rounded-full"
          style={{ background: playersLoading ? '#f59e0b' : '#22c55e' }}
        />
        <span className="text-[11px]" style={{ color: 'var(--text-muted)' }}>
          {players.length} player{players.length !== 1 ? 's' : ''} · auto-refresh 3s
        </span>
      </div>
    </div>
  );
}

// ── Entity Inspector Tab ──────────────────────────────────────────────────────

function InspectorTab({ pid, pendingAddress }: { pid: number; pendingAddress?: string }) {
  const { inspect, inspectResult, inspectLoading, inspectError, readField } = useCS2();
  const [address, setAddress] = useState(pendingAddress ?? '');
  const [thisType, setThisType] = useState('');
  const [readFieldResult, setReadFieldResult] = useState<string | null>(null);

  useEffect(() => {
    if (pendingAddress) setAddress(pendingAddress);
  }, [pendingAddress]);

  const handleInspect = useCallback(() => {
    if (!address.trim()) return;
    inspect(pid, address.trim(), thisType.trim() || undefined);
  }, [pid, address, thisType, inspect]);

  const handleReadField = useCallback(async (className: string, fieldName: string) => {
    if (!inspectResult?.address) return;
    const res = await readField(pid, inspectResult.address, className, fieldName);
    if (res) {
      setReadFieldResult(`${fieldName} = ${JSON.stringify(res.value)}`);
      setTimeout(() => setReadFieldResult(null), 4000);
    }
  }, [pid, inspectResult, readField]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') handleInspect();
  }, [handleInspect]);

  return (
    <div className="p-6 space-y-4">
      {/* Controls */}
      <div className="flex items-center gap-2">
        <div className="flex-1 relative">
          <input
            type="text"
            placeholder="0x... entity address"
            value={address}
            onChange={(e) => setAddress(e.target.value)}
            onKeyDown={handleKeyDown}
            className="w-full h-8 px-3 rounded-lg text-xs font-mono outline-none"
            style={{
              background: 'var(--surface)',
              border: '1px solid var(--border)',
              color: 'var(--text)',
              transition: 'border-color 0.1s ease',
            }}
            onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
            onBlur={(e) => { e.currentTarget.style.borderColor = 'var(--border)'; }}
          />
        </div>
        <div className="w-40">
          <input
            type="text"
            placeholder="class hint (opt)"
            value={thisType}
            onChange={(e) => setThisType(e.target.value)}
            onKeyDown={handleKeyDown}
            className="w-full h-8 px-3 rounded-lg text-xs font-mono outline-none"
            style={{
              background: 'var(--surface)',
              border: '1px solid var(--border)',
              color: 'var(--text)',
              transition: 'border-color 0.1s ease',
            }}
            onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
            onBlur={(e) => { e.currentTarget.style.borderColor = 'var(--border)'; }}
          />
        </div>
        <button
          onClick={handleInspect}
          disabled={!address.trim() || inspectLoading}
          className="px-3 h-8 rounded-lg text-xs cursor-pointer border-none outline-none disabled:opacity-40"
          style={actionBtnStyle(!address.trim() || inspectLoading)}
          onMouseEnter={(e) => { if (address.trim() && !inspectLoading) { e.currentTarget.style.background = 'var(--hover)'; e.currentTarget.style.color = 'var(--text)'; } }}
          onMouseLeave={(e) => { e.currentTarget.style.background = 'transparent'; e.currentTarget.style.color = 'var(--text-secondary)'; }}
        >
          {inspectLoading ? 'Inspecting...' : 'Inspect'}
        </button>
      </div>

      {/* Read field flash */}
      <AnimatePresence>
        {readFieldResult && (
          <motion.div
            className="px-3 py-1.5 rounded text-xs font-mono"
            style={{ background: 'var(--active)', color: '#86efac' }}
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
          >
            {readFieldResult}
          </motion.div>
        )}
      </AnimatePresence>

      {/* Results */}
      {!inspectResult && !inspectLoading && !inspectError && (
        <motion.div
          className="flex flex-col items-center justify-center gap-3 py-16"
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          transition={{ delay: 0.1 }}
        >
          <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'◈'}</div>
          <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter an address to inspect</p>
          <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Dumps all schema fields from an entity object</p>
        </motion.div>
      )}

      <InspectView
        pid={pid}
        result={inspectResult}
        loading={inspectLoading}
        error={inspectError}
        onReadField={handleReadField}
      />
    </div>
  );
}

// ── Main Panel ────────────────────────────────────────────────────────────────

function CS2Inspector({ onNavigate: _onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { process: attachedProcess } = useProcess();
  const { connected: dmaConnected } = useDma();
  const pid = attachedProcess?.pid;

  const [activeTab, setActiveTab] = useState<TabId>('overview');
  const [inspectorAddress, setInspectorAddress] = useState<string | undefined>(undefined);

  const handleOpenInInspector = useCallback((address: string) => {
    setInspectorAddress(address);
    setActiveTab('inspector');
  }, []);

  const notReady = !dmaConnected || !pid;

  return (
    <div className="h-full flex flex-col">
      {/* Panel header */}
      <motion.div
        className="shrink-0 px-6 pt-6 pb-0 space-y-4"
        initial={{ opacity: 0, y: -8 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.15, ease: 'easeOut' }}
      >
        <div className="flex items-center justify-between">
          <div>
            <h1 className="text-lg tracking-tight" style={{ color: 'var(--text)', fontWeight: 500 }}>
              CS2 Inspector
            </h1>
            <p className="text-xs mt-0.5" style={{ color: 'var(--text-muted)' }}>
              Counter-Strike 2 memory introspection — players, entities, game state
            </p>
          </div>
          {/* Status pill */}
          <div className="flex items-center gap-2 px-2.5 py-1 rounded-md" style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}>
            <div
              className="w-1.5 h-1.5 rounded-full"
              style={{ background: notReady ? 'var(--dot-disconnected)' : 'var(--dot-connected)' }}
            />
            <span className="text-[11px]" style={{ color: 'var(--text-muted)' }}>
              {!dmaConnected ? 'DMA not connected' : !pid ? 'No process' : `PID ${pid}`}
            </span>
          </div>
        </div>

        {/* Tab bar */}
        <div className="flex items-center gap-0.5 -mb-px">
          {TABS.map((tab) => {
            const isActive = activeTab === tab.id;
            return (
              <button
                key={tab.id}
                onClick={() => setActiveTab(tab.id)}
                className="px-4 py-2 text-xs cursor-pointer border-none outline-none relative"
                style={{
                  background: 'transparent',
                  color: isActive ? 'var(--text)' : 'var(--text-muted)',
                  fontWeight: isActive ? 500 : 400,
                  borderBottom: isActive ? '2px solid var(--text)' : '2px solid transparent',
                  transition: 'color 0.1s ease, border-color 0.1s ease',
                }}
                onMouseEnter={(e) => { if (!isActive) e.currentTarget.style.color = 'var(--text-secondary)'; }}
                onMouseLeave={(e) => { if (!isActive) e.currentTarget.style.color = 'var(--text-muted)'; }}
              >
                {tab.label}
              </button>
            );
          })}
        </div>
      </motion.div>

      {/* Divider */}
      <div style={{ borderBottom: '1px solid var(--border)' }} />

      {/* Not-ready gate */}
      {notReady ? (
        <motion.div
          className="flex-1 flex flex-col items-center justify-center gap-3"
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          transition={{ delay: 0.1 }}
        >
          <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'⊞'}</div>
          <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>
            {!dmaConnected ? 'Connect DMA to use CS2 Inspector' : 'Attach to a process first'}
          </p>
        </motion.div>
      ) : (
        <div className="flex-1 min-h-0 overflow-auto">
          <AnimatePresence mode="wait">
            <motion.div
              key={activeTab}
              className="h-full"
              initial={{ opacity: 0, y: 4 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -4 }}
              transition={{ duration: 0.1, ease: 'easeOut' }}
            >
              {activeTab === 'overview' && <OverviewTab pid={pid} />}
              {activeTab === 'players' && (
                <PlayersTab
                  pid={pid}
                  onInspect={handleOpenInInspector}
                />
              )}
              {activeTab === 'inspector' && (
                <InspectorTab
                  pid={pid}
                  pendingAddress={inspectorAddress}
                />
              )}
            </motion.div>
          </AnimatePresence>
        </div>
      )}
    </div>
  );
}

export default CS2Inspector;
