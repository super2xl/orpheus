import { useEffect, useRef } from 'react';
import { motion } from 'motion/react';

export interface MenuItem {
  label: string;
  action: () => void;
  separator?: boolean;
  disabled?: boolean;
}

interface ContextMenuProps {
  x: number;
  y: number;
  items: MenuItem[];
  onClose: () => void;
}

function ContextMenu({ x, y, items, onClose }: ContextMenuProps) {
  const menuRef = useRef<HTMLDivElement>(null);

  // Adjust position to keep menu on screen
  const adjustedPosition = useRef({ x, y });
  useEffect(() => {
    if (menuRef.current) {
      const rect = menuRef.current.getBoundingClientRect();
      const vw = window.innerWidth;
      const vh = window.innerHeight;

      let ax = x;
      let ay = y;

      if (x + rect.width > vw - 8) {
        ax = vw - rect.width - 8;
      }
      if (y + rect.height > vh - 8) {
        ay = vh - rect.height - 8;
      }
      if (ax < 8) ax = 8;
      if (ay < 8) ay = 8;

      adjustedPosition.current = { x: ax, y: ay };
      menuRef.current.style.left = `${ax}px`;
      menuRef.current.style.top = `${ay}px`;
    }
  }, [x, y]);

  // Close on click outside
  useEffect(() => {
    const handleClick = (e: MouseEvent) => {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) {
        onClose();
      }
    };
    const handleContextMenu = (e: MouseEvent) => {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) {
        onClose();
      }
    };
    // Use capture to catch before other handlers
    document.addEventListener('mousedown', handleClick, true);
    document.addEventListener('contextmenu', handleContextMenu, true);
    return () => {
      document.removeEventListener('mousedown', handleClick, true);
      document.removeEventListener('contextmenu', handleContextMenu, true);
    };
  }, [onClose]);

  // Close on Escape
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        onClose();
      }
    };
    document.addEventListener('keydown', handleKeyDown);
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [onClose]);

  return (
    <motion.div
      ref={menuRef}
      className="fixed z-50 min-w-[160px] rounded-lg py-1 select-none"
      style={{
        left: x,
        top: y,
        background: 'var(--surface)',
        border: '1px solid var(--border)',
        boxShadow: '0 4px 16px rgba(0, 0, 0, 0.12), 0 1px 4px rgba(0, 0, 0, 0.08)',
      }}
      initial={{ opacity: 0, scale: 0.95 }}
      animate={{ opacity: 1, scale: 1 }}
      exit={{ opacity: 0, scale: 0.95 }}
      transition={{ duration: 0.1, ease: 'easeOut' }}
    >
      {items.map((item, index) => (
        <div key={index}>
          {item.separator && (
            <div
              className="mx-2 my-1"
              style={{
                height: 1,
                background: 'var(--border)',
              }}
            />
          )}
          <button
            onClick={() => {
              if (!item.disabled) {
                item.action();
                onClose();
              }
            }}
            disabled={item.disabled}
            className="w-full text-left px-3 py-1.5 cursor-pointer border-none outline-none"
            style={{
              fontSize: 13,
              fontFamily: 'inherit',
              background: 'transparent',
              color: item.disabled ? 'var(--text-muted)' : 'var(--text)',
              opacity: item.disabled ? 0.5 : 1,
              cursor: item.disabled ? 'default' : 'pointer',
              transition: 'background 0.1s ease, color 0.1s ease',
            }}
            onMouseEnter={(e) => {
              if (!item.disabled) {
                e.currentTarget.style.background = 'var(--hover)';
              }
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.background = 'transparent';
            }}
          >
            {item.label}
          </button>
        </div>
      ))}
    </motion.div>
  );
}

export default ContextMenu;
