import { useState, useCallback, useRef, useEffect } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useProcess } from '../hooks/useProcess';
import { useDma } from '../hooks/useDma';
import { orpheus } from '../api/client';

interface EvalResult {
  id: string;
  expression: string;
  hex: string;
  decimal: string;
  timestamp: Date;
  error?: string;
}

let evalCounter = 0;

function ExpressionEval() {
  const { process: attachedProcess } = useProcess();
  const { connected: dmaConnected } = useDma();
  const pid = attachedProcess?.pid;

  const [expression, setExpression] = useState('');
  const [loading, setLoading] = useState(false);
  const [history, setHistory] = useState<EvalResult[]>([]);
  const [historyIndex, setHistoryIndex] = useState(-1);

  // Track the committed input before navigating history
  const committedExpr = useRef('');

  const inputRef = useRef<HTMLInputElement>(null);

  const handleEvaluate = useCallback(async () => {
    const expr = expression.trim();
    if (!expr || !pid || !dmaConnected) return;

    setLoading(true);
    committedExpr.current = expr;
    setHistoryIndex(-1);

    const id = `eval-${++evalCounter}`;
    const entry: EvalResult = {
      id,
      expression: expr,
      hex: '',
      decimal: '',
      timestamp: new Date(),
    };

    try {
      const result = await orpheus.request<{ result?: string; hex?: string; address?: string; value?: string }>(
        'tools/evaluate_expression',
        { pid, expression: expr }
      );

      // The server may return result/hex, address, or value — handle all shapes
      const rawHex = result.hex || result.address || result.result || result.value || '';
      const normalized = rawHex.startsWith('0x') || rawHex.startsWith('0X')
        ? rawHex
        : '0x' + rawHex;

      entry.hex = normalized.toUpperCase().replace(/^0X/, '0x');

      // Convert hex to decimal (BigInt to handle 64-bit addresses)
      try {
        const big = BigInt(normalized);
        entry.decimal = big.toString(10);
      } catch {
        entry.decimal = '—';
      }
    } catch (e: any) {
      entry.error = e.message;
      entry.hex = '—';
      entry.decimal = '—';
    } finally {
      setLoading(false);
      setHistory(prev => [entry, ...prev].slice(0, 20));
    }
  }, [expression, pid, dmaConnected]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') {
      handleEvaluate();
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      setHistory(prev => {
        if (prev.length === 0) return prev;
        const nextIndex = Math.min(historyIndex + 1, prev.length - 1);
        if (historyIndex === -1) committedExpr.current = expression;
        setHistoryIndex(nextIndex);
        setExpression(prev[nextIndex].expression);
        return prev;
      });
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (historyIndex <= 0) {
        setHistoryIndex(-1);
        setExpression(committedExpr.current);
      } else {
        const nextIndex = historyIndex - 1;
        setHistoryIndex(nextIndex);
        setHistory(prev => {
          setExpression(prev[nextIndex].expression);
          return prev;
        });
      }
    }
  }, [handleEvaluate, historyIndex, expression]);

  // Re-focus after evaluate
  useEffect(() => {
    if (!loading) {
      inputRef.current?.focus();
    }
  }, [loading]);

  const handleClickHistoryItem = useCallback((expr: string) => {
    setExpression(expr);
    setHistoryIndex(-1);
    inputRef.current?.focus();
  }, []);

  return (
    <div className="h-full flex flex-col">
      {/* Header */}
      <motion.div
        className="shrink-0 px-6 pt-6 pb-4 space-y-4"
        initial={{ opacity: 0, y: -8 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.15, ease: 'easeOut' }}
      >
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <h1 className="text-lg tracking-tight" style={{ color: 'var(--text)', fontWeight: 500 }}>
              Expression Evaluator
            </h1>
            {history.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{ color: 'var(--text-secondary)', background: 'var(--active)' }}
              >
                {history.length} / 20
              </span>
            )}
          </div>
          {history.length > 0 && (
            <button
              onClick={() => setHistory([])}
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
          )}
        </div>

        {/* Expression input */}
        <div className="flex items-center gap-2">
          <input
            ref={inputRef}
            type="text"
            placeholder="client.dll+0x1234  or  [rsp+0x8]  or  0x7FF600001000"
            value={expression}
            onChange={(e) => setExpression(e.target.value)}
            onKeyDown={handleKeyDown}
            className="flex-1 h-9 px-3 rounded-lg text-sm font-mono outline-none"
            style={{
              background: 'var(--surface)',
              border: '1px solid var(--border)',
              color: 'var(--text)',
              transition: 'border-color 0.1s ease',
            }}
            onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
            onBlur={(e) => { e.currentTarget.style.borderColor = 'var(--border)'; }}
            autoFocus
          />
          <button
            onClick={handleEvaluate}
            disabled={loading || !pid || !expression.trim() || !dmaConnected}
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
            {loading ? '...' : 'Eval'}
          </button>
        </div>

        {/* Hint */}
        <p className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
          ↑ ↓ to navigate history · Enter to evaluate
        </p>
      </motion.div>

      {/* Results */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {history.length === 0 ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>&#x3B5;</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter an expression to evaluate</p>
            <div className="space-y-1 text-center">
              {['client.dll+0x17B8E8', '[rsp+0x8]', '0x7FF600001000+0x10'].map(ex => (
                <button
                  key={ex}
                  onClick={() => { setExpression(ex); inputRef.current?.focus(); }}
                  className="block w-full text-[11px] font-mono cursor-pointer border-none outline-none bg-transparent px-0"
                  style={{ color: 'var(--text-muted)', transition: 'color 0.1s ease' }}
                  onMouseEnter={(e) => { e.currentTarget.style.color = 'var(--text-secondary)'; }}
                  onMouseLeave={(e) => { e.currentTarget.style.color = 'var(--text-muted)'; }}
                >
                  {ex}
                </button>
              ))}
            </div>
          </motion.div>
        ) : (
          <div className="space-y-2">
            <AnimatePresence initial={false}>
              {history.map((entry, index) => (
                <motion.div
                  key={entry.id}
                  className="rounded-lg overflow-hidden"
                  style={{
                    background: 'var(--surface)',
                    border: `1px solid ${entry.error ? 'var(--border)' : 'var(--border)'}`,
                  }}
                  initial={{ opacity: 0, y: -4 }}
                  animate={{ opacity: 1, y: 0 }}
                  exit={{ opacity: 0, height: 0 }}
                  transition={{ duration: 0.12, ease: 'easeOut', delay: index === 0 ? 0 : 0 }}
                >
                  {/* Expression row */}
                  <div
                    className="flex items-center px-4 py-2 gap-3"
                    style={{ borderBottom: '1px solid var(--border)' }}
                  >
                    <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                      {entry.timestamp.toTimeString().slice(0, 8)}
                    </span>
                    <button
                      onClick={() => handleClickHistoryItem(entry.expression)}
                      className="flex-1 text-left text-xs font-mono cursor-pointer border-none outline-none bg-transparent p-0"
                      style={{ color: 'var(--text-secondary)', transition: 'color 0.1s ease' }}
                      onMouseEnter={(e) => { e.currentTarget.style.color = 'var(--text)'; }}
                      onMouseLeave={(e) => { e.currentTarget.style.color = 'var(--text-secondary)'; }}
                      title="Click to load into input"
                    >
                      {entry.expression}
                    </button>
                  </div>

                  {/* Result row */}
                  {entry.error ? (
                    <div className="px-4 py-2">
                      <span className="text-xs" style={{ color: 'var(--text-muted)' }}>
                        {entry.error}
                      </span>
                    </div>
                  ) : (
                    <div className="flex items-center px-4 py-2 gap-6">
                      <div className="flex items-center gap-2">
                        <span
                          className="text-[10px] uppercase"
                          style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400, width: 24 }}
                        >
                          Hex
                        </span>
                        <span
                          className="text-sm font-mono tabular-nums select-text"
                          style={{ color: 'var(--text)', fontWeight: 500 }}
                        >
                          {entry.hex}
                        </span>
                      </div>
                      <div
                        className="w-px self-stretch"
                        style={{ background: 'var(--border)' }}
                      />
                      <div className="flex items-center gap-2">
                        <span
                          className="text-[10px] uppercase"
                          style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400, width: 28 }}
                        >
                          Dec
                        </span>
                        <span
                          className="text-sm font-mono tabular-nums select-text"
                          style={{ color: 'var(--text-secondary)' }}
                        >
                          {entry.decimal}
                        </span>
                      </div>
                    </div>
                  )}
                </motion.div>
              ))}
            </AnimatePresence>
          </div>
        )}
      </div>
    </div>
  );
}

export default ExpressionEval;
