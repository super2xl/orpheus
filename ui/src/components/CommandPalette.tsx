import { useState, useCallback, useEffect, useRef, useMemo } from 'react';
import { motion, AnimatePresence } from 'motion/react';

interface Command {
  id: string;
  label: string;
  icon: string;
  shortcut?: string;
}

interface CommandPaletteProps {
  open: boolean;
  onClose: () => void;
  onNavigate: (panel: string) => void;
  onToggleTheme: () => void;
}

const commands: Command[] = [
  { id: 'processes', label: 'Go to Processes', icon: '\u25A3', shortcut: '1' },
  { id: 'modules', label: 'Go to Modules', icon: '\u29C9', shortcut: '2' },
  { id: 'memory', label: 'Go to Memory', icon: '\u2B1A', shortcut: '3' },
  { id: 'disassembly', label: 'Go to Disassembly', icon: '\u{1D4AE}', shortcut: '4' },
  { id: 'scanner', label: 'Go to Scanner', icon: '\u29BF', shortcut: '5' },
  { id: 'strings', label: 'Go to Strings', icon: 'T' },
  { id: 'xrefs', label: 'Go to Xrefs', icon: '\u2192' },
  { id: 'rtti', label: 'Go to RTTI', icon: '\u25C8' },
  { id: 'functions', label: 'Go to Functions', icon: '\u03BB' },
  { id: 'bookmarks', label: 'Go to Bookmarks', icon: '\u2605' },
  { id: 'write-tracer', label: 'Go to Write Tracer', icon: '\u270E' },
  { id: 'emulator', label: 'Go to Emulator', icon: '\u25B6' },
  { id: 'decompiler', label: 'Go to Decompiler', icon: '{ }' },
  { id: 'cfg', label: 'Go to CFG', icon: '\u22B6' },
  { id: 'settings', label: 'Open Settings', icon: '\u2699' },
  { id: 'theme', label: 'Toggle Theme', icon: '\u25D1' },
];

function CommandPalette({ open, onClose, onNavigate, onToggleTheme }: CommandPaletteProps) {
  const [query, setQuery] = useState('');
  const [selectedIndex, setSelectedIndex] = useState(0);
  const inputRef = useRef<HTMLInputElement>(null);
  const listRef = useRef<HTMLDivElement>(null);

  const filtered = useMemo(() => {
    if (!query) return commands;
    const lower = query.toLowerCase();
    return commands.filter(cmd => cmd.label.toLowerCase().includes(lower));
  }, [query]);

  // Reset state when opened
  useEffect(() => {
    if (open) {
      setQuery('');
      setSelectedIndex(0);
      // Focus input after animation starts
      requestAnimationFrame(() => {
        inputRef.current?.focus();
      });
    }
  }, [open]);

  // Keep selected index in bounds
  useEffect(() => {
    if (selectedIndex >= filtered.length) {
      setSelectedIndex(Math.max(0, filtered.length - 1));
    }
  }, [filtered.length, selectedIndex]);

  // Scroll selected item into view
  useEffect(() => {
    if (!listRef.current) return;
    const items = listRef.current.children;
    if (items[selectedIndex]) {
      (items[selectedIndex] as HTMLElement).scrollIntoView({ block: 'nearest' });
    }
  }, [selectedIndex]);

  const executeCommand = useCallback((cmd: Command) => {
    if (cmd.id === 'theme') {
      onToggleTheme();
    } else {
      onNavigate(cmd.id);
    }
    onClose();
  }, [onNavigate, onToggleTheme, onClose]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    switch (e.key) {
      case 'ArrowDown':
        e.preventDefault();
        setSelectedIndex(prev => Math.min(prev + 1, filtered.length - 1));
        break;
      case 'ArrowUp':
        e.preventDefault();
        setSelectedIndex(prev => Math.max(prev - 1, 0));
        break;
      case 'Enter':
        e.preventDefault();
        if (filtered[selectedIndex]) {
          executeCommand(filtered[selectedIndex]);
        }
        break;
      case 'Escape':
        e.preventDefault();
        onClose();
        break;
    }
  }, [filtered, selectedIndex, executeCommand, onClose]);

  return (
    <AnimatePresence>
      {open && (
        <motion.div
          className="fixed inset-0 z-50 flex items-start justify-center"
          style={{ paddingTop: '20vh' }}
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          exit={{ opacity: 0 }}
          transition={{ duration: 0.15 }}
        >
          {/* Backdrop */}
          <motion.div
            className="absolute inset-0"
            style={{ background: 'var(--bg)', opacity: 0.6 }}
            onClick={onClose}
            initial={{ opacity: 0 }}
            animate={{ opacity: 0.6 }}
            exit={{ opacity: 0 }}
            transition={{ duration: 0.15 }}
          />

          {/* Modal */}
          <motion.div
            className="relative w-full rounded-xl overflow-hidden"
            style={{
              maxWidth: 520,
              background: 'var(--surface)',
              border: '1px solid var(--border)',
              backdropFilter: 'blur(24px)',
              boxShadow: '0 25px 50px -12px rgba(0, 0, 0, 0.25)',
            }}
            initial={{ opacity: 0, scale: 0.95, y: -8 }}
            animate={{ opacity: 1, scale: 1, y: 0 }}
            exit={{ opacity: 0, scale: 0.95, y: -8 }}
            transition={{ type: 'spring', stiffness: 500, damping: 35 }}
            onKeyDown={handleKeyDown}
          >
            {/* Search input */}
            <div style={{ borderBottom: '1px solid var(--border)' }}>
              <input
                ref={inputRef}
                type="text"
                placeholder="Type a command..."
                value={query}
                onChange={(e) => {
                  setQuery(e.target.value);
                  setSelectedIndex(0);
                }}
                className="w-full h-12 px-4 text-sm outline-none"
                style={{
                  background: 'transparent',
                  color: 'var(--text)',
                  border: 'none',
                }}
              />
            </div>

            {/* Results */}
            <div
              ref={listRef}
              className="py-1.5 overflow-y-auto"
              style={{ maxHeight: 340 }}
            >
              {filtered.length === 0 ? (
                <div className="px-4 py-6 text-center text-xs" style={{ color: 'var(--text-muted)' }}>
                  No matching commands
                </div>
              ) : (
                filtered.map((cmd, index) => (
                  <motion.button
                    key={cmd.id}
                    onClick={() => executeCommand(cmd)}
                    onMouseEnter={() => setSelectedIndex(index)}
                    className="w-full flex items-center gap-3 px-4 h-10 text-sm cursor-pointer border-none outline-none"
                    style={{
                      background: index === selectedIndex ? 'var(--hover)' : 'transparent',
                      color: index === selectedIndex ? 'var(--text)' : 'var(--text-secondary)',
                      transition: 'background 0.05s ease, color 0.05s ease',
                    }}
                    initial={{ opacity: 0, x: -4 }}
                    animate={{ opacity: 1, x: 0 }}
                    transition={{
                      duration: 0.08,
                      ease: 'easeOut',
                      delay: Math.min(index * 0.015, 0.1),
                    }}
                  >
                    {/* Icon */}
                    <span
                      className="text-base leading-none shrink-0 w-6 text-center"
                      style={{ color: index === selectedIndex ? 'var(--text)' : 'var(--text-muted)' }}
                    >
                      {cmd.icon}
                    </span>
                    {/* Label */}
                    <span className="flex-1 text-left">{cmd.label}</span>
                    {/* Shortcut */}
                    {cmd.shortcut && (
                      <span
                        className="text-[10px] font-mono px-1.5 py-0.5 rounded"
                        style={{
                          color: 'var(--text-muted)',
                          background: 'var(--active)',
                        }}
                      >
                        {cmd.shortcut}
                      </span>
                    )}
                  </motion.button>
                ))
              )}
            </div>

            {/* Footer hint */}
            <div
              className="flex items-center gap-3 px-4 py-2 text-[10px]"
              style={{
                borderTop: '1px solid var(--border)',
                color: 'var(--text-muted)',
              }}
            >
              <span>
                <span
                  className="font-mono px-1 py-0.5 rounded mr-1"
                  style={{ background: 'var(--active)' }}
                >
                  {'\u2191\u2193'}
                </span>
                navigate
              </span>
              <span>
                <span
                  className="font-mono px-1 py-0.5 rounded mr-1"
                  style={{ background: 'var(--active)' }}
                >
                  {'\u23CE'}
                </span>
                select
              </span>
              <span>
                <span
                  className="font-mono px-1 py-0.5 rounded mr-1"
                  style={{ background: 'var(--active)' }}
                >
                  esc
                </span>
                close
              </span>
            </div>
          </motion.div>
        </motion.div>
      )}
    </AnimatePresence>
  );
}

export default CommandPalette;
