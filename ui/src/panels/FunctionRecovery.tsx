import { useState, useEffect, useMemo, useCallback } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useFunctionRecovery } from '../hooks/useFunctionRecovery';
import { useModules } from '../hooks/useModules';
import { useProcess } from '../hooks/useProcess';
import { useDma } from '../hooks/useDma';

function FunctionRecovery({ onNavigate: _onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { process: attachedProcess } = useProcess();
  const { connected: dmaConnected } = useDma();
  const pid = attachedProcess?.pid;
  const { summary, loading, error, recover } = useFunctionRecovery();
  const { modules, refresh: refreshModules } = useModules();

  const [selectedModule, setSelectedModule] = useState('');
  const [hasScanned, setHasScanned] = useState(false);
  const [maxFunctions, setMaxFunctions] = useState(10000);

  // Technique checkboxes
  const [prologues, setPrologues] = useState(true);
  const [callTargets, setCallTargets] = useState(true);
  const [exceptionData, setExceptionData] = useState(true);

  // Fetch modules when DMA connected and attached
  useEffect(() => {
    if (dmaConnected && pid) {
      refreshModules(pid);
    }
  }, [dmaConnected, pid, refreshModules]);

  const selectedModuleInfo = useMemo(() => {
    return modules.find((m) => m.name === selectedModule);
  }, [modules, selectedModule]);

  const handleRecover = useCallback(() => {
    if (!pid || !selectedModuleInfo) return;
    setHasScanned(true);
    recover(pid, selectedModuleInfo.base, selectedModuleInfo.size, {
      use_prologues: prologues,
      follow_calls: callTargets,
      use_exception_data: exceptionData,
      max_functions: maxFunctions,
    });
  }, [pid, selectedModuleInfo, recover, prologues, callTargets, exceptionData, maxFunctions]);

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
              Functions
            </h1>
            {summary && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {summary.count} found
              </span>
            )}
          </div>
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
              <option value="">Select module...</option>
              {modules.map((m) => (
                <option key={m.base} value={m.name}>
                  {m.name}
                </option>
              ))}
            </select>
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Technique checkboxes */}
          {([
            ['Prologues', prologues, setPrologues],
            ['Follow Calls', callTargets, setCallTargets],
            ['.pdata', exceptionData, setExceptionData],
          ] as [string, boolean, (v: boolean) => void][]).map(([label, checked, setter]) => (
            <label
              key={label}
              className="flex items-center gap-1 text-xs cursor-pointer select-none"
              style={{ color: checked ? 'var(--text)' : 'var(--text-muted)' }}
            >
              <input
                type="checkbox"
                checked={checked}
                onChange={(e) => setter(e.target.checked)}
                className="cursor-pointer"
                style={{ accentColor: 'var(--text)' }}
              />
              {label}
            </label>
          ))}

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Max functions */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Max
            </span>
            <input
              type="number"
              value={maxFunctions}
              onChange={(e) => setMaxFunctions(Math.max(1, parseInt(e.target.value) || 1))}
              className="h-7 w-20 px-2 rounded-md text-xs font-mono outline-none"
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

          {/* Recover button */}
          <button
            onClick={handleRecover}
            disabled={loading || !pid || !selectedModule || !dmaConnected}
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
            {loading ? 'Recovering...' : 'Recover'}
          </button>
        </div>

        {/* Progress bar -- shown while loading */}
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
                  animate={{ width: ['0%', '60%', '80%', '90%'] }}
                  transition={{ duration: 30, ease: 'easeOut' }}
                />
              </div>
              <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                Recovering functions...
              </span>
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
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u03BB'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Select a module to discover functions</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Prologues, calls, and .pdata</p>
          </motion.div>
        ) : !summary && !loading ? (
          /* Empty state: no results */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u03BB'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No functions found</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Try enabling more techniques or a different module</p>
          </motion.div>
        ) : summary ? (
          /* Summary display */
          <motion.div
            className="rounded-lg p-5 space-y-4"
            style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}
            initial={{ opacity: 0, y: 8 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.15 }}
          >
            <h2 className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Recovery Summary
            </h2>

            {/* Count large display */}
            <div className="flex items-baseline gap-1.5">
              <span className="text-3xl font-mono" style={{ color: 'var(--text)', fontWeight: 500 }}>
                {summary.count.toLocaleString()}
              </span>
              <span className="text-sm" style={{ color: 'var(--text-muted)' }}>functions recovered</span>
            </div>

            {/* Status */}
            <div className="space-y-0.5">
              <div className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
                Status
              </div>
              <div className="text-sm font-mono" style={{ color: 'var(--text)' }}>
                {summary.status}
              </div>
            </div>

            {/* Summary details */}
            {summary.summary && Object.keys(summary.summary).length > 0 && (
              <div className="space-y-2">
                <div className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
                  Details
                </div>
                <div className="grid grid-cols-2 gap-3">
                  {Object.entries(summary.summary).map(([key, value]) => (
                    <div key={key} className="space-y-0.5">
                      <div className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                        {key}
                      </div>
                      <div className="text-sm font-mono" style={{ color: 'var(--text)' }}>
                        {typeof value === 'number' ? value.toLocaleString() : String(value)}
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </motion.div>
        ) : null}
      </div>
    </div>
  );
}

export default FunctionRecovery;
