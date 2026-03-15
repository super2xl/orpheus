import { useState, useEffect, useCallback, useMemo } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useWriteTracer } from '../hooks/useWriteTracer';
import { useModules } from '../hooks/useModules';
import { useConnection } from '../hooks/useConnection';
import { useDma } from '../hooks/useDma';
import { useContextMenu } from '../hooks/useContextMenu';
import ContextMenu from '../components/ContextMenu';
import { copyToClipboard } from '../utils/clipboard';
import type { CallGraphNode, ModuleInfo } from '../api/types';

// Syntax coloring — same as Disassembly
const REGISTER_NAMES = new Set([
  'rax','rbx','rcx','rdx','rsi','rdi','rbp','rsp','r8','r9','r10','r11','r12','r13','r14','r15',
  'eax','ebx','ecx','edx','esi','edi','ebp','esp','r8d','r9d','r10d','r11d','r12d','r13d','r14d','r15d',
  'ax','bx','cx','dx','si','di','bp','sp',
  'al','bl','cl','dl','ah','bh','ch','dh','sil','dil','bpl','spl',
  'r8b','r9b','r10b','r11b','r12b','r13b','r14b','r15b',
  'r8w','r9w','r10w','r11w','r12w','r13w','r14w','r15w',
  'xmm0','xmm1','xmm2','xmm3','xmm4','xmm5','xmm6','xmm7',
  'xmm8','xmm9','xmm10','xmm11','xmm12','xmm13','xmm14','xmm15',
  'cs','ds','es','fs','gs','ss','rip','eip',
]);

function getMnemonicColor(mnemonic: string): string {
  const m = mnemonic.toLowerCase();
  if (m.startsWith('mov') || m === 'xchg' || m === 'cmpxchg') return 'var(--syn-keyword)';
  if (m.startsWith('add') || m.startsWith('sub') || m === 'inc' || m === 'dec') return 'var(--syn-compare)';
  if (m.startsWith('and') || m.startsWith('or') || m.startsWith('xor') || m === 'neg' || m === 'not') return 'var(--syn-call)';
  if (m.startsWith('stos')) return 'var(--syn-jump)';
  return 'var(--syn-keyword)';
}

interface OperandToken {
  text: string;
  color: string;
}

function tokenizeOperands(operands: string): OperandToken[] {
  if (!operands) return [];
  const tokens: OperandToken[] = [];
  const parts = operands.split(/(\[|\]|,\s*|\s+|0x[0-9a-fA-F]+|\b[a-zA-Z][a-zA-Z0-9]*\b)/);

  for (const part of parts) {
    if (!part) continue;
    const lower = part.toLowerCase().trim();

    if (part === '[' || part === ']') {
      tokens.push({ text: part, color: 'var(--syn-memory)' });
    } else if (/^0x[0-9a-fA-F]+$/i.test(part)) {
      tokens.push({ text: part, color: 'var(--syn-number)' });
    } else if (REGISTER_NAMES.has(lower)) {
      tokens.push({ text: part, color: 'var(--syn-register)' });
    } else if (/^[0-9]+$/.test(part)) {
      tokens.push({ text: part, color: 'var(--syn-number)' });
    } else {
      tokens.push({ text: part, color: 'var(--text)' });
    }
  }

  return tokens;
}

// Tree node component
function TreeNode({
  node,
  onAddressClick,
}: {
  node: CallGraphNode;
  onAddressClick: (addr: string) => void;
}) {
  const [expanded, setExpanded] = useState(node.depth === 0);
  const hasChildren = node.children.length > 0;

  return (
    <div>
      <div
        className="flex items-center gap-1.5 h-7 group"
        style={{
          paddingLeft: `${node.depth * 20 + 8}px`,
          cursor: 'pointer',
          transition: 'background 0.1s ease',
        }}
        onMouseEnter={(e) => {
          e.currentTarget.style.background = 'var(--hover)';
        }}
        onMouseLeave={(e) => {
          e.currentTarget.style.background = 'transparent';
        }}
        onClick={() => hasChildren && setExpanded(!expanded)}
      >
        {/* Expand/collapse indicator */}
        <span
          className="text-[10px] w-3 text-center shrink-0 select-none"
          style={{ color: 'var(--text-muted)' }}
        >
          {hasChildren ? (expanded ? '\u25BC' : '\u25B6') : '\u00B7'}
        </span>

        {/* Indent lines */}
        {node.depth > 0 && (
          <div
            className="absolute"
            style={{
              left: `${(node.depth - 1) * 20 + 16}px`,
              top: 0,
              bottom: 0,
              width: 1,
              background: 'var(--border-subtle)',
            }}
          />
        )}

        {/* Address */}
        <span
          className="font-mono text-xs tabular-nums shrink-0 cursor-pointer"
          style={{
            color: 'var(--text-secondary)',
            textDecoration: 'underline',
            textDecorationColor: 'var(--border)',
            textUnderlineOffset: '2px',
          }}
          onClick={(e) => {
            e.stopPropagation();
            onAddressClick(node.address);
          }}
        >
          {node.address}
        </span>

        {/* Function name */}
        <span
          className="font-mono text-xs truncate"
          style={{ color: 'var(--text)' }}
        >
          {node.name}
        </span>

        {/* Type badge */}
        <span
          className="text-[10px] font-mono px-1.5 py-0.5 rounded shrink-0"
          style={{
            fontWeight: 400,
            background: 'var(--active)',
            color: node.type === 'DirectWriter' ? 'var(--text)' : 'var(--text-secondary)',
          }}
        >
          {node.type === 'DirectWriter' ? 'Writer' : `Depth ${node.depth}`}
        </span>
      </div>

      {/* Children */}
      <AnimatePresence>
        {expanded && hasChildren && (
          <motion.div
            initial={{ opacity: 0, height: 0 }}
            animate={{ opacity: 1, height: 'auto' }}
            exit={{ opacity: 0, height: 0 }}
            transition={{ duration: 0.12, ease: 'easeOut' }}
          >
            {node.children.map((child, idx) => (
              <TreeNode
                key={`${child.address}-${idx}`}
                node={child}
                onAddressClick={onAddressClick}
              />
            ))}
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  );
}

function WriteTracer({ onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { health } = useConnection();
  const { connected: dmaConnected } = useDma();
  const pid = health?.pid;
  const { writes, callGraph, loading, error, progress, trace, cancel } = useWriteTracer();
  const { modules, refresh: refreshModules } = useModules();

  const [address, setAddress] = useState('');
  const [selectedModule, setSelectedModule] = useState('');
  const [maxDepth, setMaxDepth] = useState(3);
  const [hasTraced, setHasTraced] = useState(false);
  const [showCallGraph, setShowCallGraph] = useState(false);
  const { menu, show: showContextMenu, close: closeContextMenu } = useContextMenu();

  // Fetch modules when DMA connected and attached
  useEffect(() => {
    if (dmaConnected && pid) {
      refreshModules(pid);
    }
  }, [dmaConnected, pid, refreshModules]);

  const getSelectedModuleInfo = useCallback((): ModuleInfo | undefined => {
    if (!selectedModule) return undefined;
    return modules.find((m) => m.name === selectedModule);
  }, [selectedModule, modules]);

  const handleTrace = useCallback(() => {
    const input = address.trim();
    if (!input || !pid) return;
    setHasTraced(true);
    setShowCallGraph(false);
    const mod = getSelectedModuleInfo();
    trace(pid, input, mod?.base_address, mod?.size, maxDepth);
  }, [address, pid, trace, getSelectedModuleInfo, maxDepth]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleTrace();
    }
  }, [handleTrace]);

  const handleAddressClick = useCallback((addr: string) => {
    if (onNavigate) {
      onNavigate('disassembly', addr);
    }
  }, [onNavigate]);

  // Memoize rendered writes table
  const renderedWrites = useMemo(() => {
    return writes.map((write, index) => {
      const operandTokens = tokenizeOperands(write.operands);
      return (
        <motion.div
          key={`${write.instruction_address}-${index}`}
          className="flex items-center px-3 font-mono text-xs select-text group"
          style={{
            height: 28,
            background: index % 2 === 1 ? 'var(--hover)' : 'transparent',
            transition: 'background 0.1s ease',
          }}
          onContextMenu={(e: React.MouseEvent) => showContextMenu(e, [
            { label: 'View in Disassembly', action: () => onNavigate?.('disassembly', write.instruction_address) },
            { label: 'View in Memory', action: () => onNavigate?.('memory', write.instruction_address) },
            { label: 'Copy Address', action: () => copyToClipboard(write.instruction_address), separator: true },
          ])}
          onMouseEnter={(e) => {
            e.currentTarget.style.background = 'var(--active)';
          }}
          onMouseLeave={(e) => {
            e.currentTarget.style.background = index % 2 === 1 ? 'var(--hover)' : 'transparent';
          }}
          initial={{ opacity: 0, x: -4 }}
          animate={{ opacity: 1, x: 0 }}
          transition={{
            duration: 0.08,
            ease: 'easeOut',
            delay: Math.min(index * 0.01, 0.2),
          }}
          title={write.full_text}
        >
          {/* Address */}
          <span
            className="shrink-0 tabular-nums cursor-pointer"
            style={{
              width: 160,
              color: 'var(--text-secondary)',
              textDecoration: 'underline',
              textDecorationColor: 'var(--border)',
              textUnderlineOffset: '2px',
            }}
            onClick={() => handleAddressClick(write.instruction_address)}
          >
            {write.instruction_address.replace(/^0x/i, '').toUpperCase().padStart(16, '0')}
          </span>

          {/* Instruction (mnemonic + operands, syntax colored) */}
          <span className="flex-1 truncate flex items-center gap-1">
            <span
              className="shrink-0"
              style={{
                width: 72,
                color: getMnemonicColor(write.mnemonic),
                fontWeight: 500,
              }}
            >
              {write.mnemonic}
            </span>
            <span className="truncate">
              {operandTokens.map((token, tIdx) => (
                <span key={tIdx} style={{ color: token.color }}>
                  {token.text}
                </span>
              ))}
            </span>
          </span>

          {/* Function name */}
          <span
            className="shrink-0 truncate text-right"
            style={{
              width: 200,
              color: 'var(--text-muted)',
            }}
          >
            {write.function_name}
          </span>
        </motion.div>
      );
    });
  }, [writes, handleAddressClick, showContextMenu, onNavigate]);

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
              Write Tracer
            </h1>
            {hasTraced && !loading && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {writes.length} write{writes.length !== 1 ? 's' : ''}
              </span>
            )}
          </div>
        </div>

        {/* Target address input */}
        <div className="relative">
          <input
            type="text"
            placeholder="0x7FF6A1B2C3D4"
            value={address}
            onChange={(e) => setAddress(e.target.value)}
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
            {address && (
              <motion.button
                onClick={() => setAddress('')}
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
              Scope
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

          {/* Max depth */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Depth
            </span>
            <input
              type="number"
              min={0}
              max={10}
              value={maxDepth}
              onChange={(e) => setMaxDepth(Math.max(0, Math.min(10, parseInt(e.target.value) || 0)))}
              className="w-12 h-7 px-2 rounded-md text-xs font-mono text-center outline-none"
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
            />
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Trace / Cancel button */}
          <button
            onClick={loading ? cancel : handleTrace}
            disabled={!loading && (!pid || !address.trim() || !dmaConnected)}
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
            {loading ? 'Cancel' : 'Trace'}
          </button>
        </div>

        {/* Progress indicator */}
        <AnimatePresence>
          {loading && progress && (
            <motion.div
              className="text-xs font-mono"
              style={{ color: 'var(--text-muted)' }}
              initial={{ opacity: 0, height: 0 }}
              animate={{ opacity: 1, height: 'auto' }}
              exit={{ opacity: 0, height: 0 }}
              transition={{ duration: 0.1 }}
            >
              {progress}
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
        {!hasTraced ? (
          /* Empty state: no trace yet */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u270E'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter a target address to find writes</p>
            <p className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>Finds instructions that write to the address</p>
          </motion.div>
        ) : writes.length === 0 && !loading ? (
          /* Empty state: no results */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u270E'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No writes found to this address</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Try a different address or module scope</p>
          </motion.div>
        ) : (
          <div className="space-y-4">
            {/* Section A: Direct Writes */}
            <div
              className="rounded-lg overflow-hidden"
              style={{
                background: 'var(--surface)',
                border: '1px solid var(--border)',
              }}
            >
              {/* Column header */}
              <div
                className="flex items-center px-3 h-7 text-[10px] font-mono uppercase select-none"
                style={{
                  color: 'var(--text-muted)',
                  borderBottom: '1px solid var(--border)',
                  letterSpacing: '0.08em',
                  fontWeight: 400,
                }}
              >
                <span className="shrink-0" style={{ width: 160 }}>Address</span>
                <span className="flex-1">Instruction</span>
                <span className="shrink-0 text-right" style={{ width: 200 }}>Function</span>
              </div>

              {/* Write rows */}
              {renderedWrites}

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
                    <span className="text-xs" style={{ color: 'var(--text-muted)' }}>Tracing...</span>
                  </motion.div>
                )}
              </AnimatePresence>
            </div>

            {/* Section B: Call Graph */}
            {callGraph.length > 0 && (
              <div
                className="rounded-lg overflow-hidden"
                style={{
                  background: 'var(--surface)',
                  border: '1px solid var(--border)',
                }}
              >
                {/* Call graph header */}
                <button
                  className="w-full flex items-center gap-2 px-3 h-8 text-[10px] uppercase select-none cursor-pointer border-none outline-none"
                  style={{
                    background: 'transparent',
                    color: 'var(--text-muted)',
                    letterSpacing: '0.08em',
                    fontWeight: 400,
                    borderBottom: showCallGraph ? '1px solid var(--border)' : 'none',
                    transition: 'all 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = 'var(--hover)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent';
                  }}
                  onClick={() => setShowCallGraph(!showCallGraph)}
                >
                  <span className="text-[10px]">
                    {showCallGraph ? '\u25BC' : '\u25B6'}
                  </span>
                  <span>Call Graph</span>
                  <span
                    className="font-mono px-1.5 py-0.5 rounded"
                    style={{
                      background: 'var(--active)',
                      color: 'var(--text-secondary)',
                    }}
                  >
                    {callGraph.length} root{callGraph.length !== 1 ? 's' : ''}
                  </span>
                </button>

                {/* Call graph tree */}
                <AnimatePresence>
                  {showCallGraph && (
                    <motion.div
                      initial={{ opacity: 0, height: 0 }}
                      animate={{ opacity: 1, height: 'auto' }}
                      exit={{ opacity: 0, height: 0 }}
                      transition={{ duration: 0.15, ease: 'easeOut' }}
                      className="py-1"
                    >
                      {callGraph.map((node, idx) => (
                        <TreeNode
                          key={`${node.address}-${idx}`}
                          node={node}
                          onAddressClick={handleAddressClick}
                        />
                      ))}
                    </motion.div>
                  )}
                </AnimatePresence>
              </div>
            )}
          </div>
        )}
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

export default WriteTracer;
