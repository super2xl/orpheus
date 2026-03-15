import { useState, useCallback, useMemo } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useDecompiler } from '../hooks/useDecompiler';
import { useConnection } from '../hooks/useConnection';

const C_KEYWORDS = new Set([
  'if', 'else', 'while', 'for', 'do', 'switch', 'case', 'default', 'break',
  'continue', 'return', 'goto', 'sizeof', 'typedef', 'struct', 'union', 'enum',
  'static', 'extern', 'const', 'volatile', 'register', 'inline', 'restrict',
  'auto', 'signed', 'unsigned', 'true', 'false', 'NULL', 'nullptr',
]);

const C_TYPES = new Set([
  'void', 'int', 'char', 'short', 'long', 'float', 'double', 'bool',
  'int8_t', 'int16_t', 'int32_t', 'int64_t',
  'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
  'size_t', 'ssize_t', 'ptrdiff_t', 'intptr_t', 'uintptr_t',
  'BYTE', 'WORD', 'DWORD', 'QWORD', 'BOOL', 'HANDLE', 'LPVOID', 'LPCSTR',
  'UINT', 'INT', 'LONG', 'ULONG', 'PVOID', 'LPARAM', 'WPARAM', 'HRESULT',
  'undefined', 'undefined1', 'undefined2', 'undefined4', 'undefined8',
  'byte', 'ushort', 'uint', 'ulong', 'longlong', 'ulonglong',
]);

interface Token {
  text: string;
  color: string;
}

function tokenizeLine(line: string): Token[] {
  const tokens: Token[] = [];
  // Combined regex for all token types
  const re = /(\/\/.*$|"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|0x[0-9a-fA-F]+[uUlL]*|\b\d+[uUlL]*\b|\b[a-zA-Z_]\w*\b|[{}()[\];,.*&|!~^%<>=+\-\/?:])/g;
  let lastIndex = 0;
  let match: RegExpExecArray | null;

  while ((match = re.exec(line)) !== null) {
    // Text before this match
    if (match.index > lastIndex) {
      tokens.push({ text: line.slice(lastIndex, match.index), color: 'var(--text)' });
    }

    const text = match[0];

    if (text.startsWith('//')) {
      tokens.push({ text, color: 'var(--syn-nop)' });
    } else if (text.startsWith('"') || text.startsWith("'")) {
      tokens.push({ text, color: 'var(--syn-string)' });
    } else if (/^0x[0-9a-fA-F]/i.test(text) || /^\d/.test(text)) {
      tokens.push({ text, color: 'var(--syn-number)' });
    } else if (C_KEYWORDS.has(text)) {
      tokens.push({ text, color: 'var(--syn-keyword)' });
    } else if (C_TYPES.has(text)) {
      tokens.push({ text, color: 'var(--syn-register)' });
    } else if (/^[a-zA-Z_]\w*$/.test(text)) {
      // Check if followed by '(' — function call
      const after = line.slice(re.lastIndex);
      if (/^\s*\(/.test(after)) {
        tokens.push({ text, color: 'var(--syn-call)' });
      } else {
        tokens.push({ text, color: 'var(--text)' });
      }
    } else {
      tokens.push({ text, color: 'var(--text)' });
    }

    lastIndex = re.lastIndex;
  }

  // Remaining text
  if (lastIndex < line.length) {
    tokens.push({ text: line.slice(lastIndex), color: 'var(--text)' });
  }

  return tokens;
}

function Decompiler() {
  const { health } = useConnection();
  const pid = health?.pid;
  const { code, functionName, loading, error, decompile } = useDecompiler();

  const [address, setAddress] = useState('');
  const [maxInstructions, setMaxInstructions] = useState(100000);
  const [hasDecompiled, setHasDecompiled] = useState(false);

  const handleGo = useCallback(async () => {
    const input = address.trim();
    if (!input || !pid) return;

    let addrStr: string;
    if (input.startsWith('0x') || input.startsWith('0X')) {
      addrStr = input;
    } else {
      addrStr = '0x' + input;
    }
    setHasDecompiled(true);
    decompile(pid, addrStr, maxInstructions);
  }, [address, pid, maxInstructions, decompile]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleGo();
    }
  }, [handleGo]);

  const handleCopy = useCallback(() => {
    if (code) {
      navigator.clipboard.writeText(code);
    }
  }, [code]);

  const lines = useMemo(() => code.split('\n'), [code]);

  const renderedLines = useMemo(() => {
    return lines.map((line, index) => {
      const tokens = tokenizeLine(line);
      return (
        <motion.div
          key={index}
          className="flex font-mono text-xs select-text"
          style={{
            minHeight: 20,
            lineHeight: '20px',
            background: index % 2 === 1 ? 'var(--hover)' : 'transparent',
          }}
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          transition={{
            duration: 0.08,
            ease: 'easeOut',
            delay: Math.min(index * 0.003, 0.15),
          }}
        >
          {/* Line number */}
          <span
            className="shrink-0 text-right pr-4 select-none tabular-nums"
            style={{
              width: 56,
              color: 'var(--text-muted)',
              fontSize: '0.65rem',
            }}
          >
            {index + 1}
          </span>
          {/* Code */}
          <span className="flex-1 whitespace-pre">
            {tokens.map((token, tIdx) => (
              <span key={tIdx} style={{ color: token.color }}>
                {token.text}
              </span>
            ))}
          </span>
        </motion.div>
      );
    });
  }, [lines]);

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
              Decompiler
            </h1>
            {functionName && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {functionName}
              </span>
            )}
            {code && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {lines.length} lines
              </span>
            )}
          </div>
          {code && (
            <button
              onClick={handleCopy}
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
              Copy
            </button>
          )}
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
              placeholder="7FF600001000"
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
            disabled={loading || !pid || !address.trim()}
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
            Decompile
          </button>
        </div>

        {/* Controls bar */}
        <div className="flex items-center gap-4">
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Max Instructions
            </span>
            <input
              type="number"
              value={maxInstructions}
              onChange={(e) => setMaxInstructions(Math.max(1, parseInt(e.target.value) || 1))}
              className="h-7 w-24 px-2 rounded-md text-xs font-mono outline-none"
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

      {/* Code display */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {!hasDecompiled && !loading ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'{ }'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter a function address to decompile</p>
            <p className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>0x7FF600001000</p>
          </motion.div>
        ) : loading ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'{ }'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Decompiling...</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>This may take a few seconds</p>
          </motion.div>
        ) : code ? (
          <div
            className="rounded-lg overflow-hidden"
            style={{
              background: 'var(--surface)',
              border: '1px solid var(--border)',
            }}
          >
            <div
              className="overflow-x-auto"
              style={{ whiteSpace: 'pre', userSelect: 'text' }}
            >
              {renderedLines}
            </div>
          </div>
        ) : hasDecompiled && !error ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'{ }'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Decompiler returned no output</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>The address may not point to a valid function</p>
          </motion.div>
        ) : null}
      </div>
    </div>
  );
}

export default Decompiler;
