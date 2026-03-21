import { useState, useCallback, useRef } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useProcess } from '../hooks/useProcess';
import { useDma } from '../hooks/useDma';
import { orpheus } from '../api/client';

type ValueType = 'Byte' | 'Int16' | 'Int32' | 'Int64' | 'Float' | 'Double' | 'String' | 'RawHex';

interface WriteEntry {
  id: string;
  address: string;
  type: ValueType;
  value: string;
  hexBytes: string;
  timestamp: Date;
  success: boolean;
  error?: string;
}

const VALUE_TYPES: ValueType[] = ['Byte', 'Int16', 'Int32', 'Int64', 'Float', 'Double', 'String', 'RawHex'];

const TYPE_LABELS: Record<ValueType, string> = {
  Byte: 'Byte',
  Int16: 'Int16',
  Int32: 'Int32',
  Int64: 'Int64',
  Float: 'Float',
  Double: 'Double',
  String: 'String',
  RawHex: 'Raw Hex',
};

/** Convert value + type to a compact hex string (no spaces, no 0x prefix) */
function encodeToHex(value: string, type: ValueType): string {
  switch (type) {
    case 'Byte': {
      const n = parseInt(value, value.startsWith('0x') || value.startsWith('0X') ? 16 : 10);
      if (isNaN(n) || n < 0 || n > 255) throw new Error('Byte must be 0–255');
      return n.toString(16).padStart(2, '0');
    }
    case 'Int16': {
      const n = parseInt(value, 10);
      if (isNaN(n) || n < -32768 || n > 65535) throw new Error('Int16 out of range');
      const buf = new ArrayBuffer(2);
      new DataView(buf).setInt16(0, n, true);
      return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
    }
    case 'Int32': {
      const n = parseInt(value, 10);
      if (isNaN(n)) throw new Error('Invalid Int32');
      const buf = new ArrayBuffer(4);
      new DataView(buf).setInt32(0, n, true);
      return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
    }
    case 'Int64': {
      let bigVal: bigint;
      try { bigVal = BigInt(value); } catch { throw new Error('Invalid Int64'); }
      const buf = new ArrayBuffer(8);
      new DataView(buf).setBigInt64(0, bigVal, true);
      return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
    }
    case 'Float': {
      const n = parseFloat(value);
      if (isNaN(n)) throw new Error('Invalid Float');
      const buf = new ArrayBuffer(4);
      new DataView(buf).setFloat32(0, n, true);
      return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
    }
    case 'Double': {
      const n = parseFloat(value);
      if (isNaN(n)) throw new Error('Invalid Double');
      const buf = new ArrayBuffer(8);
      new DataView(buf).setFloat64(0, n, true);
      return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
    }
    case 'String': {
      if (!value.length) throw new Error('String cannot be empty');
      return Array.from(new TextEncoder().encode(value)).map(b => b.toString(16).padStart(2, '0')).join('');
    }
    case 'RawHex': {
      // Accept "DE AD BE EF" or "DEADBEEF" or "0xDE 0xAD..."
      const clean = value.replace(/0x/gi, '').replace(/\s+/g, '');
      if (!clean.length) throw new Error('Hex cannot be empty');
      if (clean.length % 2 !== 0) throw new Error('Hex must have even number of nibbles');
      if (!/^[0-9a-fA-F]+$/.test(clean)) throw new Error('Invalid hex characters');
      return clean.toLowerCase();
    }
  }
}

function byteCount(hex: string): number {
  return hex.length / 2;
}

function formatTime(d: Date): string {
  return d.toTimeString().slice(0, 8);
}

let entryCounter = 0;

function MemoryWriter() {
  const { process: attachedProcess } = useProcess();
  const { connected: dmaConnected } = useDma();
  const pid = attachedProcess?.pid;

  const [address, setAddress] = useState('');
  const [value, setValue] = useState('');
  const [valueType, setValueType] = useState<ValueType>('Int32');
  const [loading, setLoading] = useState(false);
  const [encodeError, setEncodeError] = useState<string | null>(null);
  const [history, setHistory] = useState<WriteEntry[]>([]);

  // Confirmation dialog state
  const [pendingWrite, setPendingWrite] = useState<{ address: string; hexBytes: string; byteCount: number } | null>(null);

  const addressInputRef = useRef<HTMLInputElement>(null);

  // Live-encode to show byte preview and catch errors early
  const liveHex = useCallback((): { hex: string; error: string | null } => {
    if (!value.trim()) return { hex: '', error: null };
    try {
      const hex = encodeToHex(value, valueType);
      return { hex, error: null };
    } catch (e: any) {
      return { hex: '', error: e.message };
    }
  }, [value, valueType]);

  const { hex: previewHex, error: liveError } = liveHex();

  const handleWrite = useCallback(() => {
    const addrTrimmed = address.trim();
    const valTrimmed = value.trim();
    if (!addrTrimmed || !valTrimmed || !pid) return;

    // Validate & encode
    let hex: string;
    try {
      hex = encodeToHex(valTrimmed, valueType);
      setEncodeError(null);
    } catch (e: any) {
      setEncodeError(e.message);
      return;
    }

    // Normalize address display (always show with 0x prefix)
    const displayAddr = addrTrimmed.startsWith('0x') || addrTrimmed.startsWith('0X')
      ? addrTrimmed.toUpperCase()
      : '0x' + addrTrimmed.toUpperCase();

    // Show confirmation
    setPendingWrite({ address: displayAddr, hexBytes: hex, byteCount: byteCount(hex) });
  }, [address, value, valueType, pid]);

  const handleConfirmWrite = useCallback(async () => {
    if (!pendingWrite || !pid) return;
    const { address: addr, hexBytes } = pendingWrite;
    setPendingWrite(null);
    setLoading(true);

    const id = `write-${++entryCounter}`;
    const entry: WriteEntry = {
      id,
      address: addr,
      type: valueType,
      value: value.trim(),
      hexBytes,
      timestamp: new Date(),
      success: false,
    };

    try {
      await orpheus.request('tools/write_memory', {
        pid,
        address: addr,
        hex_string: hexBytes,
      });
      entry.success = true;
    } catch (e: any) {
      entry.error = e.message;
    } finally {
      setLoading(false);
      setHistory(prev => [entry, ...prev].slice(0, 50));
    }
  }, [pendingWrite, pid, valueType, value]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') handleWrite();
    if (e.key === 'Escape') setPendingWrite(null);
  }, [handleWrite]);

  const placeholder: Record<ValueType, string> = {
    Byte: '0–255 or 0xFF',
    Int16: '-32768 – 65535',
    Int32: '-2147483648 – 2147483647',
    Int64: '-9223372036854775808 – …',
    Float: '3.14',
    Double: '3.14159265358979',
    String: 'Hello, world',
    RawHex: 'DE AD BE EF or DEADBEEF',
  };

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
          <h1 className="text-lg tracking-tight" style={{ color: 'var(--text)', fontWeight: 500 }}>
            Memory Writer
          </h1>
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
              Clear History
            </button>
          )}
        </div>

        {/* Address input */}
        <div className="space-y-2">
          <div className="flex items-center gap-2">
            <span
              className="text-[10px] uppercase shrink-0 w-14"
              style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
            >
              Address
            </span>
            <div className="relative flex-1">
              <span
                className="absolute left-3 top-1/2 -translate-y-1/2 text-xs font-mono pointer-events-none"
                style={{ color: 'var(--text-muted)' }}
              >
                0x
              </span>
              <input
                ref={addressInputRef}
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
                onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
                onBlur={(e) => { e.currentTarget.style.borderColor = 'var(--border)'; }}
              />
            </div>
          </div>

          {/* Type selector + value input */}
          <div className="flex items-center gap-2">
            <span
              className="text-[10px] uppercase shrink-0 w-14"
              style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
            >
              Value
            </span>
            <select
              value={valueType}
              onChange={(e) => { setValueType(e.target.value as ValueType); setValue(''); setEncodeError(null); }}
              className="h-9 px-2 rounded-lg text-xs outline-none cursor-pointer shrink-0"
              style={{
                background: 'var(--surface)',
                border: '1px solid var(--border)',
                color: 'var(--text)',
                transition: 'border-color 0.1s ease',
                minWidth: 88,
              }}
              onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
              onBlur={(e) => { e.currentTarget.style.borderColor = 'var(--border)'; }}
            >
              {VALUE_TYPES.map(t => (
                <option key={t} value={t}>{TYPE_LABELS[t]}</option>
              ))}
            </select>
            <input
              type="text"
              placeholder={placeholder[valueType]}
              value={value}
              onChange={(e) => { setValue(e.target.value); setEncodeError(null); }}
              onKeyDown={handleKeyDown}
              className="flex-1 h-9 px-3 rounded-lg text-sm font-mono outline-none"
              style={{
                background: 'var(--surface)',
                border: `1px solid ${liveError ? 'var(--text-muted)' : 'var(--border)'}`,
                color: 'var(--text)',
                transition: 'border-color 0.1s ease',
              }}
              onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
              onBlur={(e) => { e.currentTarget.style.borderColor = liveError ? 'var(--text-muted)' : 'var(--border)'; }}
            />
            <button
              onClick={handleWrite}
              disabled={loading || !pid || !address.trim() || !value.trim() || !dmaConnected || !!liveError}
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
              Write
            </button>
          </div>

          {/* Byte preview */}
          <AnimatePresence>
            {previewHex && !liveError && (
              <motion.div
                className="flex items-center gap-2 ml-16"
                initial={{ opacity: 0, height: 0 }}
                animate={{ opacity: 1, height: 'auto' }}
                exit={{ opacity: 0, height: 0 }}
                transition={{ duration: 0.1 }}
              >
                <span className="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                  {byteCount(previewHex)} byte{byteCount(previewHex) !== 1 ? 's' : ''}:
                </span>
                <span className="text-[10px] font-mono" style={{ color: 'var(--text-secondary)' }}>
                  {previewHex.match(/.{2}/g)?.join(' ').toUpperCase()}
                </span>
              </motion.div>
            )}
            {(liveError || encodeError) && (
              <motion.div
                className="ml-16 text-[10px]"
                style={{ color: 'var(--text-secondary)' }}
                initial={{ opacity: 0, height: 0 }}
                animate={{ opacity: 1, height: 'auto' }}
                exit={{ opacity: 0, height: 0 }}
                transition={{ duration: 0.1 }}
              >
                {liveError || encodeError}
              </motion.div>
            )}
          </AnimatePresence>
        </div>
      </motion.div>

      {/* Write history */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {history.length === 0 ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>&#x270E;</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No writes yet</p>
            <p className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>
              Write history will appear here
            </p>
          </motion.div>
        ) : (
          <div
            className="rounded-lg overflow-hidden"
            style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}
          >
            {/* History header */}
            <div
              className="flex items-center px-4 h-7 text-[10px] uppercase select-none"
              style={{
                color: 'var(--text-muted)',
                borderBottom: '1px solid var(--border)',
                letterSpacing: '0.08em',
                fontWeight: 400,
              }}
            >
              <span style={{ width: 80 }}>Time</span>
              <span style={{ width: 160 }}>Address</span>
              <span style={{ width: 72 }}>Type</span>
              <span className="flex-1">Value</span>
              <span style={{ width: 200 }}>Bytes</span>
              <span style={{ width: 60 }}>Status</span>
            </div>

            <AnimatePresence initial={false}>
              {history.map((entry, index) => (
                <motion.div
                  key={entry.id}
                  className="flex items-center px-4 font-mono text-xs"
                  style={{
                    height: 32,
                    background: index % 2 === 1 ? 'var(--hover)' : 'transparent',
                    borderBottom: index < history.length - 1 ? '1px solid var(--border)' : 'none',
                  }}
                  initial={{ opacity: 0, x: -8 }}
                  animate={{ opacity: 1, x: 0 }}
                  exit={{ opacity: 0, height: 0 }}
                  transition={{ duration: 0.12, ease: 'easeOut' }}
                >
                  <span style={{ width: 80, color: 'var(--text-muted)' }}>
                    {formatTime(entry.timestamp)}
                  </span>
                  <span className="tabular-nums" style={{ width: 160, color: 'var(--text-secondary)' }}>
                    {entry.address}
                  </span>
                  <span style={{ width: 72, color: 'var(--text-muted)' }}>
                    {TYPE_LABELS[entry.type]}
                  </span>
                  <span className="flex-1 truncate" style={{ color: 'var(--text)' }} title={entry.value}>
                    {entry.value}
                  </span>
                  <span
                    className="truncate"
                    style={{ width: 200, color: 'var(--text-muted)' }}
                    title={entry.hexBytes.match(/.{2}/g)?.join(' ').toUpperCase()}
                  >
                    {entry.hexBytes.match(/.{2}/g)?.join(' ').toUpperCase()}
                  </span>
                  <span
                    style={{
                      width: 60,
                      color: entry.success ? 'var(--dot-connected)' : 'var(--text-secondary)',
                      fontWeight: 500,
                    }}
                    title={entry.error}
                  >
                    {entry.success ? 'OK' : 'ERR'}
                  </span>
                </motion.div>
              ))}
            </AnimatePresence>
          </div>
        )}
      </div>

      {/* Confirmation dialog */}
      <AnimatePresence>
        {pendingWrite && (
          <>
            {/* Backdrop */}
            <motion.div
              className="fixed inset-0 z-40"
              style={{ background: 'rgba(0,0,0,0.5)' }}
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              transition={{ duration: 0.1 }}
              onClick={() => setPendingWrite(null)}
            />
            {/* Dialog */}
            <motion.div
              className="fixed z-50 rounded-xl p-5 shadow-xl"
              style={{
                top: '50%',
                left: '50%',
                transform: 'translate(-50%, -50%)',
                width: 400,
                background: 'var(--surface)',
                border: '1px solid var(--border)',
              }}
              initial={{ opacity: 0, scale: 0.95, y: 8 }}
              animate={{ opacity: 1, scale: 1, y: 0 }}
              exit={{ opacity: 0, scale: 0.95, y: 8 }}
              transition={{ duration: 0.12, ease: 'easeOut' }}
            >
              <h2 className="text-sm font-medium mb-1" style={{ color: 'var(--text)' }}>
                Confirm Memory Write
              </h2>
              <p className="text-xs mb-4" style={{ color: 'var(--text-secondary)', lineHeight: '1.5' }}>
                Are you sure you want to write{' '}
                <span className="font-mono" style={{ color: 'var(--text)' }}>
                  {pendingWrite.byteCount} byte{pendingWrite.byteCount !== 1 ? 's' : ''}
                </span>{' '}
                to{' '}
                <span className="font-mono" style={{ color: 'var(--text)' }}>
                  {pendingWrite.address}
                </span>
                ?
              </p>
              <div
                className="rounded-lg px-3 py-2 mb-4 font-mono text-xs"
                style={{
                  background: 'var(--bg)',
                  border: '1px solid var(--border)',
                  color: 'var(--text-muted)',
                  wordBreak: 'break-all',
                }}
              >
                {pendingWrite.hexBytes.match(/.{2}/g)?.join(' ').toUpperCase()}
              </div>
              <div className="flex items-center justify-end gap-2">
                <button
                  onClick={() => setPendingWrite(null)}
                  className="px-3 h-8 rounded-lg text-xs cursor-pointer border-none outline-none"
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
                <button
                  onClick={handleConfirmWrite}
                  className="px-3 h-8 rounded-lg text-xs cursor-pointer border-none outline-none"
                  style={{
                    fontWeight: 500,
                    background: 'var(--text)',
                    color: 'var(--bg)',
                    border: '1px solid transparent',
                    transition: 'opacity 0.1s ease',
                  }}
                  onMouseEnter={(e) => { e.currentTarget.style.opacity = '0.85'; }}
                  onMouseLeave={(e) => { e.currentTarget.style.opacity = '1'; }}
                >
                  Write Memory
                </button>
              </div>
            </motion.div>
          </>
        )}
      </AnimatePresence>
    </div>
  );
}

export default MemoryWriter;
