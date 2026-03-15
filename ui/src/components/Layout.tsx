import { useState } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useConnection } from '../hooks/useConnection';
import { orpheus } from '../api/client';

interface LayoutProps {
  activePanel: string;
  onNavigate: (panel: string, address?: string) => void;
  dark: boolean;
  onToggleTheme: () => void;
  children: React.ReactNode;
}

interface NavItem {
  id: string;
  label: string;
  icon: string;
}

const navItems: NavItem[] = [
  { id: 'processes', label: 'Processes', icon: '\u25A3' },
  { id: 'modules', label: 'Modules', icon: '\u29C9' },
  { id: 'memory', label: 'Memory', icon: '\u2B1A' },
  { id: 'disassembly', label: 'Disassembly', icon: '\u{1D4AE}' },
  { id: 'scanner', label: 'Scanner', icon: '\u29BF' },
  { id: 'strings', label: 'Strings', icon: 'T' },
  { id: 'xrefs', label: 'Xrefs', icon: '\u2192' },
  { id: 'bookmarks', label: 'Bookmarks', icon: '\u2605' },
  { id: 'rtti', label: 'RTTI', icon: '\u25C8' },
  { id: 'functions', label: 'Functions', icon: '\u03BB' },
  { id: 'decompiler', label: 'Decompiler', icon: '{ }' },
  { id: 'cfg', label: 'CFG', icon: '\u22B6' },
  { id: 'write-tracer', label: 'Write Tracer', icon: '\u270E' },
  { id: 'emulator', label: 'Emulator', icon: '\u25B6' },
  { id: 'regions', label: 'Regions', icon: '\u25A6' },
  { id: 'pointers', label: 'Pointers', icon: '\u21A3' },
  { id: 'vtable', label: 'VTable', icon: '\u25A4' },
];

const utilityItems: NavItem[] = [
  { id: 'console', label: 'Console', icon: '\u25B8' },
  { id: 'tasks', label: 'Tasks', icon: '\u22EF' },
  { id: 'cache', label: 'Cache', icon: '\u25E7' },
];

const settingsItem: NavItem = { id: 'settings', label: 'Settings', icon: '\u2699' };

function Layout({ activePanel, onNavigate, dark, onToggleTheme, children }: LayoutProps) {
  const [collapsed, setCollapsed] = useState(false);
  const { connected, health } = useConnection();
  const config = orpheus.getConfig();

  const sidebarWidth = collapsed ? 60 : 240;

  return (
    <div className="h-screen flex flex-col overflow-hidden" style={{ background: 'var(--bg)', color: 'var(--text)' }}>
      {/* Main area: sidebar + content */}
      <div className="flex flex-1 min-h-0">
        {/* Sidebar */}
        <motion.aside
          className="flex flex-col relative z-10"
          style={{ background: 'var(--surface)', borderRight: '1px solid var(--border)' }}
          animate={{ width: sidebarWidth }}
          transition={{ type: 'spring', stiffness: 400, damping: 35 }}
        >
          {/* App title */}
          <div
            className="h-14 flex items-center px-5"
            style={{ borderBottom: '1px solid var(--border)' }}
          >
            <motion.div
              className="flex items-center gap-3 min-w-0"
              animate={{ opacity: 1 }}
            >
              <div
                className="w-7 h-7 rounded-lg flex items-center justify-center text-xs font-medium shrink-0"
                style={{ background: 'var(--text)', color: 'var(--bg)' }}
              >
                O
              </div>
              <AnimatePresence>
                {!collapsed && (
                  <motion.span
                    className="text-base font-medium tracking-tight whitespace-nowrap"
                    style={{ color: 'var(--text)' }}
                    initial={{ opacity: 0, width: 0 }}
                    animate={{ opacity: 1, width: 'auto' }}
                    exit={{ opacity: 0, width: 0 }}
                    transition={{ duration: 0.15, ease: 'easeInOut' }}
                  >
                    Orpheus
                  </motion.span>
                )}
              </AnimatePresence>
            </motion.div>
          </div>

          {/* Navigation */}
          <nav className="flex-1 py-3 px-2.5 space-y-0.5 overflow-auto">
            {navItems.map((item) => {
              const isActive = activePanel === item.id;
              return (
                <button
                  key={item.id}
                  onClick={() => onNavigate(item.id)}
                  className="w-full flex items-center gap-3 px-3 h-9 rounded-lg text-sm relative cursor-pointer border-none outline-none"
                  style={{
                    background: 'transparent',
                    color: isActive ? 'var(--text)' : 'var(--text-secondary)',
                    fontWeight: isActive ? 500 : 400,
                    transition: 'color 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    if (!isActive) {
                      e.currentTarget.style.color = 'var(--text)';
                    }
                  }}
                  onMouseLeave={(e) => {
                    if (!isActive) {
                      e.currentTarget.style.color = 'var(--text-secondary)';
                    }
                  }}
                >
                  {/* Animated ring border for active item */}
                  {isActive && (
                    <motion.div
                      className="nav-ring"
                      layoutId="nav-ring"
                      transition={{ type: 'spring', stiffness: 400, damping: 30 }}
                    />
                  )}
                  <span className="text-base leading-none shrink-0 w-5 text-center relative z-10">
                    {item.icon}
                  </span>
                  <AnimatePresence>
                    {!collapsed && (
                      <motion.span
                        className="whitespace-nowrap relative z-10"
                        initial={{ opacity: 0, x: -6 }}
                        animate={{ opacity: 1, x: 0 }}
                        exit={{ opacity: 0, x: -6 }}
                        transition={{ duration: 0.12, ease: 'easeOut' }}
                      >
                        {item.label}
                      </motion.span>
                    )}
                  </AnimatePresence>
                </button>
              );
            })}

            {/* Utility section divider */}
            <div className="pt-2 pb-1 px-3">
              <div style={{ borderTop: '1px solid var(--border)' }} />
            </div>

            {utilityItems.map((item) => {
              const isActive = activePanel === item.id;
              return (
                <button
                  key={item.id}
                  onClick={() => onNavigate(item.id)}
                  className="w-full flex items-center gap-3 px-3 h-9 rounded-lg text-sm relative cursor-pointer border-none outline-none"
                  style={{
                    background: 'transparent',
                    color: isActive ? 'var(--text)' : 'var(--text-secondary)',
                    fontWeight: isActive ? 500 : 400,
                    transition: 'color 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    if (!isActive) {
                      e.currentTarget.style.color = 'var(--text)';
                    }
                  }}
                  onMouseLeave={(e) => {
                    if (!isActive) {
                      e.currentTarget.style.color = 'var(--text-secondary)';
                    }
                  }}
                >
                  {isActive && (
                    <motion.div
                      className="nav-ring"
                      layoutId="nav-ring"
                      transition={{ type: 'spring', stiffness: 400, damping: 30 }}
                    />
                  )}
                  <span className="text-base leading-none shrink-0 w-5 text-center relative z-10">
                    {item.icon}
                  </span>
                  <AnimatePresence>
                    {!collapsed && (
                      <motion.span
                        className="whitespace-nowrap relative z-10"
                        initial={{ opacity: 0, x: -6 }}
                        animate={{ opacity: 1, x: 0 }}
                        exit={{ opacity: 0, x: -6 }}
                        transition={{ duration: 0.12, ease: 'easeOut' }}
                      >
                        {item.label}
                      </motion.span>
                    )}
                  </AnimatePresence>
                </button>
              );
            })}
          </nav>

          {/* Settings nav item */}
          <div className="px-2.5 pb-0.5">
            {(() => {
              const isActive = activePanel === settingsItem.id;
              return (
                <button
                  onClick={() => onNavigate(settingsItem.id)}
                  className="w-full flex items-center gap-3 px-3 h-9 rounded-lg text-sm relative cursor-pointer border-none outline-none"
                  style={{
                    background: 'transparent',
                    color: isActive ? 'var(--text)' : 'var(--text-secondary)',
                    fontWeight: isActive ? 500 : 400,
                    transition: 'color 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    if (!isActive) {
                      e.currentTarget.style.color = 'var(--text)';
                    }
                  }}
                  onMouseLeave={(e) => {
                    if (!isActive) {
                      e.currentTarget.style.color = 'var(--text-secondary)';
                    }
                  }}
                >
                  {isActive && (
                    <motion.div
                      className="nav-ring"
                      layoutId="nav-ring"
                      transition={{ type: 'spring', stiffness: 400, damping: 30 }}
                    />
                  )}
                  <span className="text-base leading-none shrink-0 w-5 text-center relative z-10">
                    {settingsItem.icon}
                  </span>
                  <AnimatePresence>
                    {!collapsed && (
                      <motion.span
                        className="whitespace-nowrap relative z-10"
                        initial={{ opacity: 0, x: -6 }}
                        animate={{ opacity: 1, x: 0 }}
                        exit={{ opacity: 0, x: -6 }}
                        transition={{ duration: 0.12, ease: 'easeOut' }}
                      >
                        {settingsItem.label}
                      </motion.span>
                    )}
                  </AnimatePresence>
                </button>
              );
            })()}
          </div>

          {/* Theme toggle */}
          <div className="px-2.5 pb-1">
            <button
              onClick={onToggleTheme}
              className="w-full flex items-center justify-center h-8 rounded-lg cursor-pointer border-none outline-none"
              style={{
                color: 'var(--text-muted)',
                background: 'transparent',
                transition: 'color 0.1s ease, background 0.1s ease',
              }}
              onMouseEnter={(e) => {
                e.currentTarget.style.color = 'var(--text)';
                e.currentTarget.style.background = 'var(--hover)';
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.color = 'var(--text-muted)';
                e.currentTarget.style.background = 'transparent';
              }}
              title={dark ? 'Switch to light mode' : 'Switch to dark mode'}
            >
              <span className="text-sm">{dark ? '\u2600' : '\u263E'}</span>
            </button>
          </div>

          {/* Collapse toggle */}
          <div className="px-2.5 pb-2">
            <button
              onClick={() => setCollapsed(!collapsed)}
              className="w-full flex items-center justify-center h-8 rounded-lg cursor-pointer border-none outline-none"
              style={{
                color: 'var(--text-muted)',
                background: 'transparent',
                transition: 'color 0.1s ease, background 0.1s ease',
              }}
              onMouseEnter={(e) => {
                e.currentTarget.style.color = 'var(--text)';
                e.currentTarget.style.background = 'var(--hover)';
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.color = 'var(--text-muted)';
                e.currentTarget.style.background = 'transparent';
              }}
            >
              <motion.span
                className="text-sm"
                animate={{ rotate: collapsed ? 0 : 180 }}
                transition={{ duration: 0.15, ease: 'easeInOut' }}
              >
                {'\u00BB'}
              </motion.span>
            </button>
          </div>

          {/* Connection status */}
          <div className="px-4 py-3" style={{ borderTop: '1px solid var(--border)' }}>
            <div className="flex items-center gap-2.5 min-w-0">
              <div
                className="w-2 h-2 rounded-full shrink-0"
                style={{
                  background: connected ? 'var(--dot-connected)' : 'var(--dot-disconnected)',
                }}
              />
              <AnimatePresence>
                {!collapsed && (
                  <motion.span
                    className="text-xs truncate"
                    style={{ color: 'var(--text-muted)' }}
                    initial={{ opacity: 0, width: 0 }}
                    animate={{ opacity: 1, width: 'auto' }}
                    exit={{ opacity: 0, width: 0 }}
                    transition={{ duration: 0.15, ease: 'easeInOut' }}
                  >
                    {connected ? 'Connected' : 'Disconnected'}
                  </motion.span>
                )}
              </AnimatePresence>
            </div>
          </div>
        </motion.aside>

        {/* Main content */}
        <main className="flex-1 min-w-0 overflow-hidden" style={{ background: 'var(--bg)' }}>
          <AnimatePresence mode="wait">
            <motion.div
              key={activePanel}
              className="h-full"
              initial={{ opacity: 0, y: 4 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -4 }}
              transition={{ duration: 0.12, ease: 'easeOut' }}
            >
              {children}
            </motion.div>
          </AnimatePresence>
        </main>
      </div>

      {/* Status bar */}
      <div
        className="h-7 flex items-center justify-between px-4 text-[11px] shrink-0"
        style={{
          background: 'var(--surface)',
          borderTop: '1px solid var(--border)',
          color: 'var(--text-muted)',
        }}
      >
        {/* Left: connection + process */}
        <div className="flex items-center gap-3">
          <div className="flex items-center gap-1.5">
            <div
              className="w-1.5 h-1.5 rounded-full"
              style={{
                background: connected ? 'var(--dot-connected)' : 'var(--dot-disconnected)',
              }}
            />
            <span style={{ fontWeight: 300 }}>{connected ? 'Online' : 'Offline'}</span>
          </div>
          {health?.process_name && (
            <>
              <span style={{ color: 'var(--border)' }}>|</span>
              <span className="font-mono" style={{ color: 'var(--text-secondary)', fontWeight: 400 }}>
                {health.process_name}
                {health.pid ? ` (${health.pid})` : ''}
              </span>
            </>
          )}
        </div>

        {/* Right: server URL */}
        <div className="flex items-center gap-2">
          <span className="font-mono" style={{ color: 'var(--text-muted)', fontWeight: 300 }}>
            {config.baseUrl}
          </span>
        </div>
      </div>
    </div>
  );
}

export default Layout;
