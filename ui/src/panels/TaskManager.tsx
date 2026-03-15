import { useEffect, useMemo } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useConnection } from '../hooks/useConnection';
import { useTasks } from '../hooks/useTasks';

function stateStyle(state: string): { color: string; fontWeight: number } {
  switch (state.toLowerCase()) {
    case 'running':
      return { color: 'var(--text)', fontWeight: 500 };
    case 'failed':
      return { color: 'var(--text)', fontWeight: 600 };
    case 'pending':
    case 'completed':
    case 'cancelled':
    default:
      return { color: 'var(--text-muted)', fontWeight: 400 };
  }
}

function stateLabel(state: string): string {
  return state.charAt(0).toUpperCase() + state.slice(1).toLowerCase();
}

function TaskManager() {
  const { connected } = useConnection();
  const { tasks, error, refresh, cancel, cleanup } = useTasks();
  // Initial fetch + auto-refresh every 2s
  useEffect(() => {
    if (!connected) return;
    refresh();
    const interval = setInterval(refresh, 2000);
    return () => clearInterval(interval);
  }, [connected, refresh]);

  const activeTasks = useMemo(
    () => tasks.filter((t) => t.state.toLowerCase() === 'running' || t.state.toLowerCase() === 'pending'),
    [tasks]
  );

  const canCancel = (state: string) => {
    const s = state.toLowerCase();
    return s === 'running' || s === 'pending';
  };

  return (
    <div className="h-full flex flex-col">
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
              Tasks
            </h1>
            {tasks.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{ color: 'var(--text-secondary)', background: 'var(--active)' }}
              >
                {activeTasks.length} active / {tasks.length} total
              </span>
            )}
          </div>
          <div className="flex items-center gap-2">
            <button
              onClick={cleanup}
              disabled={tasks.length === 0}
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
              Cleanup
            </button>
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

      {/* Task list */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {tasks.length === 0 ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u22EF'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No background tasks</p>
          </motion.div>
        ) : (
          <div className="space-y-2">
            {tasks.map((task, index) => {
              const ss = stateStyle(task.state);
              const isRunning = task.state.toLowerCase() === 'running';
              return (
                <motion.div
                  key={task.id}
                  className="rounded-lg p-4 space-y-2.5"
                  style={{
                    background: 'var(--surface)',
                    border: '1px solid var(--border)',
                  }}
                  initial={{ opacity: 0, y: 8 }}
                  animate={{ opacity: 1, y: 0 }}
                  transition={{
                    duration: 0.12,
                    delay: Math.min(index * 0.03, 0.15),
                  }}
                >
                  {/* Top row: type + id + state badge */}
                  <div className="flex items-center justify-between">
                    <div className="flex items-center gap-2 min-w-0">
                      <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                        {task.type}
                      </span>
                      <span className="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                        {task.id}
                      </span>
                    </div>
                    <span
                      className="text-[10px] uppercase px-2 py-0.5 rounded-md font-mono shrink-0"
                      style={{
                        color: ss.color,
                        fontWeight: ss.fontWeight,
                        background: 'var(--bg)',
                        letterSpacing: '0.05em',
                      }}
                    >
                      {stateLabel(task.state)}
                    </span>
                  </div>

                  {/* Status message */}
                  {task.status_message && (
                    <p className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                      {task.status_message}
                    </p>
                  )}

                  {/* Progress bar (running tasks) */}
                  {isRunning && (
                    <div className="space-y-1">
                      <div
                        className="w-full h-1 rounded-full overflow-hidden"
                        style={{ background: 'var(--border)' }}
                      >
                        <motion.div
                          className="h-full rounded-full"
                          style={{ background: 'var(--text)' }}
                          initial={{ width: 0 }}
                          animate={{ width: `${Math.min(Math.max(task.progress, 0), 100)}%` }}
                          transition={{ duration: 0.3, ease: 'easeOut' }}
                        />
                      </div>
                      <div className="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                        {task.progress.toFixed(0)}%
                      </div>
                    </div>
                  )}

                  {/* Error message */}
                  {task.error && (
                    <div
                      className="px-3 py-2 rounded-md text-xs font-mono"
                      style={{
                        background: 'var(--bg)',
                        border: '1px solid var(--border)',
                        color: 'var(--text)',
                        fontWeight: 500,
                      }}
                    >
                      {task.error}
                    </div>
                  )}

                  {/* Result summary (completed) */}
                  {task.state.toLowerCase() === 'completed' && task.result && (
                    <div
                      className="px-3 py-2 rounded-md text-xs font-mono"
                      style={{
                        background: 'var(--bg)',
                        border: '1px solid var(--border)',
                        color: 'var(--text-secondary)',
                      }}
                    >
                      {typeof task.result === 'string' ? task.result : JSON.stringify(task.result)}
                    </div>
                  )}

                  {/* Cancel button */}
                  {canCancel(task.state) && (
                    <button
                      onClick={() => cancel(task.id)}
                      className="px-2.5 h-6 rounded-md text-[11px] cursor-pointer border-none outline-none"
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
                  )}
                </motion.div>
              );
            })}
          </div>
        )}
      </div>
    </div>
  );
}

export default TaskManager;
