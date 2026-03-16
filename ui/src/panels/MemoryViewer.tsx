import { useState, useMemo, useCallback, useRef, useEffect } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useProcess } from '../hooks/useProcess';
import { useDma } from '../hooks/useDma';
import { orpheus } from '../api/client';

interface MemoryData {
  address: string;
  hex: string;
  size: number;
}

function formatSize(bytes: number): string {
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
  if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return bytes + ' B';
}

/** Parse a compact hex string into a byte array */
function hexToBytes(hex: string): number[] {
  const bytes: number[] = [];
  for (let i = 0; i < hex.length; i += 2) {
    bytes.push(parseInt(hex.substring(i, i + 2), 16));
  }
  return bytes;
}

interface MemoryViewerProps {
  pendingAddress?: string | null;
  onAddressConsumed?: () => void;
}

function MemoryViewer({ pendingAddress, onAddressConsumed }: MemoryViewerProps) {
  const { process: attachedProcess } = useProcess();
  const { connected: dmaConnected } = useDma();
  const pid = attachedProcess?.pid;

  const [address, setAddress] = useState('');
  const [currentAddress, setCurrentAddress] = useState<bigint>(0n);
  const [bytesPerRow, setBytesPerRow] = useState(16);
  const [groupSize, setGroupSize] = useState(1);
  const [readSize, setReadSize] = useState(256);
  const [memoryData, setMemoryData] = useState<number[] | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [selectedOffset, setSelectedOffset] = useState<number | null>(null);
  const [showInspector, setShowInspector] = useState(true);
  const addressInputRef = useRef<HTMLInputElement>(null);

  const readMemory = useCallback(async (addr: bigint, size: number) => {
    if (!pid) return;
    setLoading(true);
    setError(null);
    try {
      const addrHex = '0x' + addr.toString(16).toUpperCase();
      const result = await orpheus.request<MemoryData>('tools/read_memory', {
        pid,
        address: addrHex,
        size,
        format: 'hex',
      });
      // Server returns compact hex string; parse into byte array for the grid
      const bytes = result.hex ? hexToBytes(result.hex) : [];
      setMemoryData(bytes);
      setCurrentAddress(addr);
      setSelectedOffset(null);
    } catch (err: any) {
      setError(err.message);
      setMemoryData(null);
    } finally {
      setLoading(false);
    }
  }, [pid]);

  // Consume pending address from cross-panel navigation
  useEffect(() => {
    if (pendingAddress && pid) {
      setAddress(pendingAddress);
      try {
        let addr: bigint;
        if (pendingAddress.startsWith('0x') || pendingAddress.startsWith('0X')) {
          addr = BigInt(pendingAddress);
        } else {
          addr = BigInt('0x' + pendingAddress);
        }
        readMemory(addr, readSize);
      } catch {
        // Address may be a module+offset expression; just set the input
      }
      onAddressConsumed?.();
    }
  }, [pendingAddress, pid, readSize, readMemory, onAddressConsumed]);

  const handleGo = useCallback(async () => {
    const input = address.trim();
    if (!input || !pid) return;

    // Check if it's a module+offset expression like "client.dll+0x1000"
    const exprMatch = input.match(/^(.+?)\+(.+)$/);
    if (exprMatch && !input.startsWith('0x') && !input.startsWith('0X')) {
      try {
        const result = await orpheus.request<{ address: string }>('tools/evaluate_expression', {
          pid,
          expression: input,
        });
        const resolved = BigInt(result.address);
        readMemory(resolved, readSize);
        return;
      } catch {
        // Fall through to try as raw address
      }
    }

    // Parse as hex address
    try {
      let addr: bigint;
      if (input.startsWith('0x') || input.startsWith('0X')) {
        addr = BigInt(input);
      } else {
        addr = BigInt('0x' + input);
      }
      readMemory(addr, readSize);
    } catch {
      setError('Invalid address format');
    }
  }, [address, pid, readSize, readMemory]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleGo();
    }
  }, [handleGo]);

  const handleRefresh = useCallback(() => {
    if (currentAddress > 0n) {
      readMemory(currentAddress, readSize);
    }
  }, [currentAddress, readSize, readMemory]);

  const handleByteClick = useCallback((absoluteOffset: number) => {
    setSelectedOffset((prev) => (prev === absoluteOffset ? null : absoluteOffset));
  }, []);

  // Build hex display rows
  const rows = useMemo(() => {
    if (!memoryData || memoryData.length === 0) return [];

    const result: { address: string; hexGroups: { bytes: number[]; text: string; startOffset: number }[]; ascii: string; rowOffset: number }[] = [];
    const totalRows = Math.ceil(memoryData.length / bytesPerRow);

    for (let row = 0; row < totalRows; row++) {
      const offset = row * bytesPerRow;
      const rowAddr = currentAddress + BigInt(offset);
      const addrStr = '0x' + rowAddr.toString(16).toUpperCase().padStart(16, '0');

      // Build hex groups
      const hexGroups: { bytes: number[]; text: string; startOffset: number }[] = [];
      for (let g = 0; g < bytesPerRow; g += groupSize) {
        const groupBytes: number[] = [];
        const parts: string[] = [];
        const startOffset = offset + g;
        for (let b = 0; b < groupSize; b++) {
          const idx = offset + g + b;
          if (idx < memoryData.length) {
            const byte = memoryData[idx];
            groupBytes.push(byte);
            parts.push(byte.toString(16).toUpperCase().padStart(2, '0'));
          }
        }
        hexGroups.push({ bytes: groupBytes, text: parts.join(''), startOffset });
      }

      // Build ASCII
      let ascii = '';
      for (let i = 0; i < bytesPerRow; i++) {
        const idx = offset + i;
        if (idx < memoryData.length) {
          const byte = memoryData[idx];
          ascii += (byte >= 32 && byte <= 126) ? String.fromCharCode(byte) : '.';
        }
      }

      result.push({ address: addrStr, hexGroups, ascii, rowOffset: offset });
    }

    return result;
  }, [memoryData, currentAddress, bytesPerRow, groupSize]);

  // Format the current range display
  const rangeDisplay = useMemo(() => {
    if (!memoryData || memoryData.length === 0) return '';
    const start = '0x' + currentAddress.toString(16).toUpperCase();
    const end = '0x' + (currentAddress + BigInt(memoryData.length)).toString(16).toUpperCase();
    return `${start} - ${end} (${formatSize(memoryData.length)})`;
  }, [memoryData, currentAddress]);

  // Data Inspector interpretations
  const inspectorData = useMemo(() => {
    if (selectedOffset === null || !memoryData) return null;
    if (selectedOffset < 0 || selectedOffset >= memoryData.length) return null;

    const remaining = memoryData.length - selectedOffset;
    const buffer = new ArrayBuffer(8);
    const view = new DataView(buffer);

    // Fill buffer with available bytes (little-endian memory)
    for (let i = 0; i < Math.min(8, remaining); i++) {
      view.setUint8(i, memoryData[selectedOffset + i]);
    }

    const byte = memoryData[selectedOffset];
    const entries: { label: string; value: string }[] = [];

    entries.push({ label: 'Int8', value: view.getInt8(0).toString() });
    entries.push({ label: 'UInt8', value: view.getUint8(0).toString() });

    if (remaining >= 2) {
      entries.push({ label: 'Int16', value: view.getInt16(0, true).toString() });
      entries.push({ label: 'UInt16', value: view.getUint16(0, true).toString() });
    }

    if (remaining >= 4) {
      entries.push({ label: 'Int32', value: view.getInt32(0, true).toString() });
      entries.push({ label: 'UInt32', value: view.getUint32(0, true).toString() });
    }

    if (remaining >= 8) {
      entries.push({ label: 'Int64', value: view.getBigInt64(0, true).toString() });
    }

    if (remaining >= 4) {
      const f = view.getFloat32(0, true);
      entries.push({ label: 'Float', value: isFinite(f) ? f.toPrecision(7).replace(/\.?0+$/, '') : f.toString() });
    }

    if (remaining >= 8) {
      const d = view.getFloat64(0, true);
      entries.push({ label: 'Double', value: isFinite(d) ? d.toPrecision(15).replace(/\.?0+$/, '') : d.toString() });
    }

    entries.push({ label: 'Hex', value: '0x' + byte.toString(16).toUpperCase().padStart(2, '0') });
    entries.push({ label: 'Binary', value: byte.toString(2).padStart(8, '0') });

    const ascii = (byte >= 32 && byte <= 126) ? String.fromCharCode(byte) : '\u00B7';
    entries.push({ label: 'ASCII', value: ascii });

    const selectedAddr = '0x' + (currentAddress + BigInt(selectedOffset)).toString(16).toUpperCase().padStart(16, '0');

    return { entries, selectedAddr };
  }, [selectedOffset, memoryData, currentAddress]);

  // Check if a byte offset is within the selected group
  const isOffsetSelected = useCallback((offset: number) => {
    return selectedOffset === offset;
  }, [selectedOffset]);

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
              Memory
            </h1>
            {rangeDisplay && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {rangeDisplay}
              </span>
            )}
          </div>
          <div className="flex items-center gap-2">
            {/* Inspector toggle */}
            {memoryData && (
              <button
                onClick={() => setShowInspector(!showInspector)}
                className="px-2.5 h-7 rounded-md text-xs cursor-pointer border-none outline-none"
                style={{
                  fontWeight: 400,
                  background: showInspector ? 'var(--active)' : 'transparent',
                  color: showInspector ? 'var(--text)' : 'var(--text-secondary)',
                  border: '1px solid var(--border)',
                  transition: 'all 0.1s ease',
                }}
                onMouseEnter={(e) => {
                  if (!showInspector) {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }
                }}
                onMouseLeave={(e) => {
                  if (!showInspector) {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }
                }}
              >
                Inspector
              </button>
            )}
            <button
              onClick={handleRefresh}
              disabled={loading || !memoryData}
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
              {'\u21BB'} Refresh
            </button>
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
              ref={addressInputRef}
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
          {/* Bytes per row */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Columns
            </span>
            {[16, 32].map((n) => (
              <button
                key={n}
                onClick={() => setBytesPerRow(n)}
                className="px-2 h-6 rounded text-[11px] font-mono cursor-pointer border-none outline-none"
                style={{
                  fontWeight: 400,
                  background: bytesPerRow === n ? 'var(--active)' : 'transparent',
                  color: bytesPerRow === n ? 'var(--text)' : 'var(--text-secondary)',
                  border: '1px solid var(--border)',
                  transition: 'all 0.1s ease',
                }}
                onMouseEnter={(e) => {
                  if (bytesPerRow !== n) {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }
                }}
                onMouseLeave={(e) => {
                  if (bytesPerRow !== n) {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }
                }}
              >
                {n}
              </button>
            ))}
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Group size */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Group
            </span>
            {[1, 2, 4, 8].map((n) => (
              <button
                key={n}
                onClick={() => setGroupSize(n)}
                className="px-2 h-6 rounded text-[11px] font-mono cursor-pointer border-none outline-none"
                style={{
                  fontWeight: 400,
                  background: groupSize === n ? 'var(--active)' : 'transparent',
                  color: groupSize === n ? 'var(--text)' : 'var(--text-secondary)',
                  border: '1px solid var(--border)',
                  transition: 'all 0.1s ease',
                }}
                onMouseEnter={(e) => {
                  if (groupSize !== n) {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }
                }}
                onMouseLeave={(e) => {
                  if (groupSize !== n) {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }
                }}
              >
                {n}
              </button>
            ))}
          </div>

          {/* Separator */}
          <div className="w-px h-4" style={{ background: 'var(--border)' }} />

          {/* Read size */}
          <div className="flex items-center gap-1.5">
            <span className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Size
            </span>
            {[256, 512, 1024].map((n) => (
              <button
                key={n}
                onClick={() => setReadSize(n)}
                className="px-2 h-6 rounded text-[11px] font-mono cursor-pointer border-none outline-none"
                style={{
                  fontWeight: 400,
                  background: readSize === n ? 'var(--active)' : 'transparent',
                  color: readSize === n ? 'var(--text)' : 'var(--text-secondary)',
                  border: '1px solid var(--border)',
                  transition: 'all 0.1s ease',
                }}
                onMouseEnter={(e) => {
                  if (readSize !== n) {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }
                }}
                onMouseLeave={(e) => {
                  if (readSize !== n) {
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

      {/* Hex display + Data Inspector */}
      <div className="flex-1 min-h-0 flex px-6 pb-4 gap-4">
        {/* Hex view */}
        <div className="flex-1 min-w-0 overflow-auto">
          {!memoryData ? (
            /* Empty state: no address entered */
            <motion.div
              className="h-full flex flex-col items-center justify-center gap-3"
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              transition={{ delay: 0.1 }}
            >
              <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u2B1A'}</div>
              <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter an address to read memory</p>
              <p className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>0x7FF600001000 or module.dll+0x1000</p>
            </motion.div>
          ) : (
            /* Hex grid */
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
                {/* Address column */}
                <span className="shrink-0" style={{ width: 160 }}>Address</span>
                {/* Hex column headers */}
                <div className="flex-1 flex items-center gap-0">
                  {Array.from({ length: Math.ceil(bytesPerRow / groupSize) }, (_, g) => (
                    <span
                      key={g}
                      className="text-center"
                      style={{ width: groupSize * 24 + (groupSize > 1 ? 4 : 0), marginRight: 4 }}
                    >
                      {g * groupSize < 16
                        ? (g * groupSize).toString(16).toUpperCase().padStart(2, '0')
                        : ''}
                    </span>
                  ))}
                </div>
                {/* ASCII column */}
                <span className="shrink-0 text-right" style={{ width: bytesPerRow * 8 + 16 }}>ASCII</span>
              </div>

              {/* Data rows */}
              {rows.map((row, rowIndex) => (
                <motion.div
                  key={row.address}
                  className="flex items-center px-3 font-mono text-xs select-text"
                  style={{
                    height: 24,
                    background: rowIndex % 2 === 1 ? 'var(--hover)' : 'transparent',
                    transition: 'background 0.1s ease',
                  }}
                  initial={{ opacity: 0 }}
                  animate={{ opacity: 1 }}
                  transition={{
                    duration: 0.08,
                    ease: 'easeOut',
                    delay: Math.min(rowIndex * 0.008, 0.15),
                  }}
                >
                  {/* Address */}
                  <span
                    className="shrink-0 tabular-nums"
                    style={{ width: 160, color: 'var(--text-muted)' }}
                  >
                    {row.address}
                  </span>

                  {/* Hex bytes - clickable for data inspector */}
                  <div className="flex-1 flex items-center gap-0">
                    {row.hexGroups.map((group, gIdx) => {
                      // For group size 1, each span is one byte; for larger groups, clicking selects the first byte
                      const isSelected = selectedOffset !== null &&
                        selectedOffset >= group.startOffset &&
                        selectedOffset < group.startOffset + group.bytes.length;
                      return (
                        <span
                          key={gIdx}
                          className="tabular-nums cursor-pointer rounded-sm"
                          style={{
                            width: groupSize * 24 + (groupSize > 1 ? 4 : 0),
                            marginRight: 4,
                            color: isSelected ? 'var(--text)' : getByteColor(group.bytes),
                            background: isSelected ? 'var(--active)' : 'transparent',
                            transition: 'background 0.1s ease, color 0.1s ease',
                          }}
                          onClick={() => handleByteClick(group.startOffset)}
                        >
                          {group.text}
                        </span>
                      );
                    })}
                  </div>

                  {/* ASCII */}
                  <span
                    className="shrink-0 tabular-nums"
                    style={{ width: bytesPerRow * 8 + 16 }}
                  >
                    {row.ascii.split('').map((char, cIdx) => {
                      const byteOffset = row.rowOffset + cIdx;
                      const isSel = isOffsetSelected(byteOffset);
                      return (
                        <span
                          key={cIdx}
                          className="cursor-pointer"
                          style={{
                            color: isSel ? 'var(--text)' : (char === '.' ? 'var(--text-muted)' : 'var(--text)'),
                            background: isSel ? 'var(--active)' : 'transparent',
                            borderRadius: 2,
                          }}
                          onClick={() => handleByteClick(byteOffset)}
                        >
                          {char}
                        </span>
                      );
                    })}
                  </span>
                </motion.div>
              ))}

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
                    <span className="text-xs" style={{ color: 'var(--text-muted)' }}>Reading memory...</span>
                  </motion.div>
                )}
              </AnimatePresence>
            </div>
          )}
        </div>

        {/* Data Inspector sidebar */}
        <AnimatePresence>
          {showInspector && memoryData && (
            <motion.div
              className="shrink-0 overflow-auto"
              style={{ width: 200 }}
              initial={{ opacity: 0, width: 0 }}
              animate={{ opacity: 1, width: 200 }}
              exit={{ opacity: 0, width: 0 }}
              transition={{ duration: 0.15, ease: 'easeInOut' }}
            >
              <div
                className="rounded-lg p-3 h-full"
                style={{
                  background: 'var(--surface)',
                  border: '1px solid var(--border)',
                }}
              >
                <span
                  className="text-[10px] uppercase block mb-3"
                  style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}
                >
                  Data Inspector
                </span>

                {/* Divider */}
                <div className="mb-3" style={{ borderBottom: '1px solid var(--border)' }} />

                {inspectorData ? (
                  <div className="space-y-0.5">
                    {/* Selected address */}
                    <div className="mb-2">
                      <span className="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                        {inspectorData.selectedAddr}
                      </span>
                    </div>
                    {inspectorData.entries.map((entry) => (
                      <div key={entry.label} className="flex items-baseline justify-between gap-2">
                        <span
                          className="text-[10px] shrink-0"
                          style={{ color: 'var(--text-secondary)' }}
                        >
                          {entry.label}
                        </span>
                        <span
                          className="text-[11px] font-mono tabular-nums text-right truncate"
                          style={{ color: 'var(--text)', userSelect: 'text' }}
                        >
                          {entry.value}
                        </span>
                      </div>
                    ))}
                  </div>
                ) : (
                  <p className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                    Click a byte to inspect
                  </p>
                )}
              </div>
            </motion.div>
          )}
        </AnimatePresence>
      </div>
    </div>
  );
}

/** Returns the appropriate CSS color variable for a byte group based on byte values */
function getByteColor(bytes: number[]): string {
  if (bytes.length === 0) return 'var(--text-secondary)';
  // For groups, use the first byte to determine color
  // All zero: dimmed, all 0xFF: bright, otherwise normal
  const allZero = bytes.every((b) => b === 0x00);
  const hasFF = bytes.some((b) => b === 0xFF);
  if (allZero) return 'var(--text-muted)';
  if (hasFF) return 'var(--text)';
  return 'var(--text-secondary)';
}

export default MemoryViewer;
