import { useState, useCallback, useMemo } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useDisassembly } from '../hooks/useDisassembly';
import { useConnection } from '../hooks/useConnection';
import { useDma } from '../hooks/useDma';
import { useContextMenu } from '../hooks/useContextMenu';
import ContextMenu from '../components/ContextMenu';
import { copyToClipboard } from '../utils/clipboard';
import { orpheus } from '../api/client';
import type { InstructionInfo } from '../api/types';

function getMnemonicColor(category: string): string {
  switch (category) {
    case 'Call': return 'var(--syn-call)';
    case 'Jump':
    case 'ConditionalJump': return 'var(--syn-jump)';
    case 'Return': return 'var(--syn-return)';
    case 'Compare': return 'var(--syn-compare)';
    case 'Nop': return 'var(--syn-nop)';
    case 'Push':
    case 'Pop': return 'var(--syn-keyword)';
    default: return 'var(--syn-keyword)';
  }
}

// Simple regex-based operand colorizer
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

interface OperandToken {
  text: string;
  color: string;
}

function tokenizeOperands(operands: string): OperandToken[] {
  if (!operands) return [];
  const tokens: OperandToken[] = [];
  // Split by regex keeping delimiters
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

function Disassembly({ onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { health } = useConnection();
  const { connected: dmaConnected } = useDma();
  const pid = health?.pid;
  const { instructions, loading, error, disassemble } = useDisassembly();

  const [address, setAddress] = useState('');
  const [instructionCount, setInstructionCount] = useState(50);
  const [hoveredAddr, setHoveredAddr] = useState<string | null>(null);
  const { menu, show: showContextMenu, close: closeContextMenu } = useContextMenu();

  const handleGo = useCallback(async () => {
    const input = address.trim();
    if (!input || !pid) return;

    // Check if it's a module+offset expression
    const exprMatch = input.match(/^(.+?)\+(.+)$/);
    if (exprMatch && !input.startsWith('0x') && !input.startsWith('0X')) {
      try {
        const result = await orpheus.request<{ address: string }>('tools/evaluate_expression', {
          pid,
          expression: input,
        });
        disassemble(pid, result.address, instructionCount);
        return;
      } catch {
        // Fall through to try as raw address
      }
    }

    // Parse as hex address
    let addrStr: string;
    if (input.startsWith('0x') || input.startsWith('0X')) {
      addrStr = input;
    } else {
      addrStr = '0x' + input;
    }
    disassemble(pid, addrStr, instructionCount);
  }, [address, pid, instructionCount, disassemble]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleGo();
    }
  }, [handleGo]);

  const navigateToAddress = useCallback((addr: string) => {
    if (!pid) return;
    setAddress(addr);
    disassemble(pid, addr, instructionCount);
  }, [pid, instructionCount, disassemble]);

  // Memoize the rendered instruction list
  const renderedInstructions = useMemo(() => {
    return instructions.map((inst: InstructionInfo, index: number) => {
      const operandTokens = tokenizeOperands(inst.operands);
      const isClickable = !!inst.branch_target;

      return (
        <motion.div
          key={inst.address}
          className="flex items-center px-3 font-mono text-xs select-text group"
          style={{
            height: 24,
            background: index % 2 === 1 ? 'var(--hover)' : 'transparent',
            cursor: isClickable ? 'pointer' : 'default',
            transition: 'background 0.1s ease',
          }}
          onClick={() => {
            if (inst.branch_target) {
              navigateToAddress(inst.branch_target);
            }
          }}
          onContextMenu={(e: React.MouseEvent) => showContextMenu(e, [
            { label: 'View in Memory', action: () => onNavigate?.('memory', inst.address) },
            { label: 'Find XRefs', action: () => onNavigate?.('xrefs', inst.address) },
            { label: 'Generate Signature', action: () => {}, disabled: true },
            { label: 'Copy Address', action: () => copyToClipboard(inst.address), separator: true },
            { label: 'Copy Instruction', action: () => copyToClipboard(inst.full_text) },
          ])}
          onMouseEnter={() => setHoveredAddr(inst.address)}
          onMouseLeave={() => setHoveredAddr(null)}
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          transition={{
            duration: 0.08,
            ease: 'easeOut',
            delay: Math.min(index * 0.005, 0.15),
          }}
          title={inst.full_text}
        >
          {/* Address */}
          <span
            className="shrink-0 tabular-nums cursor-pointer"
            style={{
              width: 160,
              color: hoveredAddr === inst.address ? 'var(--text-secondary)' : 'var(--text-muted)',
              transition: 'color 0.1s ease',
            }}
            onClick={(e) => {
              e.stopPropagation();
              navigateToAddress(inst.address);
            }}
          >
            {inst.address.replace(/^0x/i, '').toUpperCase().padStart(16, '0')}
          </span>

          {/* Bytes */}
          <span
            className="shrink-0 tabular-nums truncate"
            style={{
              width: 140,
              color: 'var(--text-muted)',
              fontSize: '0.65rem',
            }}
          >
            {inst.bytes}
          </span>

          {/* Mnemonic */}
          <span
            className="shrink-0"
            style={{
              width: 72,
              color: getMnemonicColor(inst.category),
              fontWeight: 500,
            }}
          >
            {inst.mnemonic}
          </span>

          {/* Operands */}
          <span className="flex-1 truncate">
            {operandTokens.map((token, tIdx) => (
              <span key={tIdx} style={{ color: token.color }}>
                {token.text}
              </span>
            ))}
            {/* Branch target indicator */}
            {inst.branch_target && (
              <span
                className="ml-2 opacity-0 group-hover:opacity-100"
                style={{
                  color: 'var(--text-muted)',
                  fontSize: '0.65rem',
                  transition: 'opacity 0.1s ease',
                }}
              >
                {'\u2192'} {inst.branch_target}
              </span>
            )}
          </span>
        </motion.div>
      );
    });
  }, [instructions, hoveredAddr, navigateToAddress, showContextMenu, onNavigate]);

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
              Disassembly
            </h1>
            {instructions.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {instructions.length} instructions
              </span>
            )}
          </div>
        </div>

        {/* Address bar */}
        <div className="flex items-center gap-2">
          <div className="relative flex-1">
            <span
              className="absolute left-3 top-1/2 -translate-y-1/2 text-xs font-mono pointer-events-none"
              style={{ color: 'var(--text-muted)' }}
            >
              {'0x'}
            </span>
            <input
              type="text"
              placeholder="7FF600001000 or module.dll+0x1000"
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
          <button
            onClick={handleGo}
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
            Go
          </button>
        </div>

        {/* Controls bar */}
        <div className="flex items-center gap-4">
          {/* Instruction count */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Count
            </span>
            {[20, 50, 100, 200].map((n) => (
              <button
                key={n}
                onClick={() => setInstructionCount(n)}
                className="px-2 h-6 rounded text-[11px] font-mono cursor-pointer border-none outline-none"
                style={{
                  fontWeight: 400,
                  background: instructionCount === n ? 'var(--active)' : 'transparent',
                  color: instructionCount === n ? 'var(--text)' : 'var(--text-secondary)',
                  border: '1px solid var(--border)',
                  transition: 'all 0.1s ease',
                }}
                onMouseEnter={(e) => {
                  if (instructionCount !== n) {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }
                }}
                onMouseLeave={(e) => {
                  if (instructionCount !== n) {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }
                }}
              >
                {n}
              </button>
            ))}
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

      {/* Disassembly listing */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {instructions.length === 0 && !loading ? (
          /* Empty state: no address entered */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u2B1A'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter an address to disassemble</p>
            <p className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>0x7FF600001000 or module.dll+0x1000</p>
          </motion.div>
        ) : (
          /* Disassembly grid */
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
              <span className="shrink-0" style={{ width: 140 }}>Bytes</span>
              <span className="shrink-0" style={{ width: 72 }}>Mnemonic</span>
              <span className="flex-1">Operands</span>
            </div>

            {/* Instructions */}
            {renderedInstructions}

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
                  <span className="text-xs" style={{ color: 'var(--text-muted)' }}>Disassembling...</span>
                </motion.div>
              )}
            </AnimatePresence>
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

export default Disassembly;
