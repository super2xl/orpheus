import { useState } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useConnection } from '../hooks/useConnection';
import { orpheus } from '../api/client';

interface LayoutProps {
  activePanel: string;
  onNavigate: (panel: string) => void;
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
];

function Layout({ activePanel, onNavigate, children }: LayoutProps) {
  const [collapsed, setCollapsed] = useState(false);
  const { connected, health } = useConnection();
  const config = orpheus.getConfig();

  const sidebarWidth = collapsed ? 60 : 240;

  return (
    <div className="h-screen bg-slate-950 text-slate-100 flex flex-col overflow-hidden">
      {/* Main area: sidebar + content */}
      <div className="flex flex-1 min-h-0">
        {/* Sidebar */}
        <motion.aside
          className="flex flex-col bg-slate-900/80 border-r border-slate-800/60 relative z-10"
          animate={{ width: sidebarWidth }}
          transition={{ type: 'spring', stiffness: 300, damping: 30 }}
        >
          {/* App title */}
          <div className="h-14 flex items-center px-4 border-b border-slate-800/40">
            <motion.div
              className="flex items-center gap-3 min-w-0"
              animate={{ opacity: 1 }}
            >
              <div className="w-7 h-7 rounded-lg bg-gradient-to-br from-cyan-400 to-cyan-600 flex items-center justify-center text-xs font-bold text-slate-950 shrink-0">
                O
              </div>
              <AnimatePresence>
                {!collapsed && (
                  <motion.span
                    className="text-base font-semibold tracking-tight bg-gradient-to-r from-slate-100 to-slate-300 bg-clip-text text-transparent whitespace-nowrap"
                    initial={{ opacity: 0, width: 0 }}
                    animate={{ opacity: 1, width: 'auto' }}
                    exit={{ opacity: 0, width: 0 }}
                    transition={{ type: 'spring', stiffness: 300, damping: 30 }}
                  >
                    Orpheus
                  </motion.span>
                )}
              </AnimatePresence>
            </motion.div>
          </div>

          {/* Navigation */}
          <nav className="flex-1 py-2 px-2 space-y-0.5 overflow-hidden">
            {navItems.map((item) => {
              const isActive = activePanel === item.id;
              return (
                <motion.button
                  key={item.id}
                  onClick={() => onNavigate(item.id)}
                  className={`
                    w-full flex items-center gap-3 px-3 h-9 rounded-lg text-sm
                    transition-colors duration-150 relative overflow-hidden cursor-pointer
                    ${isActive
                      ? 'text-cyan-400'
                      : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/60'
                    }
                  `}
                  whileHover={{ scale: 1.01 }}
                  whileTap={{ scale: 0.98 }}
                  transition={{ type: 'spring', stiffness: 400, damping: 25 }}
                >
                  {/* Active background glow */}
                  {isActive && (
                    <motion.div
                      className="absolute inset-0 bg-cyan-500/10 rounded-lg"
                      layoutId="nav-active"
                      transition={{ type: 'spring', stiffness: 350, damping: 30 }}
                    />
                  )}
                  {/* Active left accent */}
                  {isActive && (
                    <motion.div
                      className="absolute left-0 top-1.5 bottom-1.5 w-0.5 bg-cyan-400 rounded-full"
                      layoutId="nav-accent"
                      transition={{ type: 'spring', stiffness: 350, damping: 30 }}
                    />
                  )}
                  <span className="text-base leading-none shrink-0 relative z-10 w-5 text-center">
                    {item.icon}
                  </span>
                  <AnimatePresence>
                    {!collapsed && (
                      <motion.span
                        className="relative z-10 whitespace-nowrap font-medium"
                        initial={{ opacity: 0, x: -8 }}
                        animate={{ opacity: 1, x: 0 }}
                        exit={{ opacity: 0, x: -8 }}
                        transition={{ type: 'spring', stiffness: 300, damping: 25 }}
                      >
                        {item.label}
                      </motion.span>
                    )}
                  </AnimatePresence>
                </motion.button>
              );
            })}
          </nav>

          {/* Collapse toggle */}
          <div className="px-2 pb-2">
            <motion.button
              onClick={() => setCollapsed(!collapsed)}
              className="w-full flex items-center justify-center h-8 rounded-lg text-slate-500 hover:text-slate-300 hover:bg-slate-800/60 transition-colors duration-150 cursor-pointer"
              whileHover={{ scale: 1.02 }}
              whileTap={{ scale: 0.96 }}
            >
              <motion.span
                className="text-sm"
                animate={{ rotate: collapsed ? 0 : 180 }}
                transition={{ type: 'spring', stiffness: 300, damping: 25 }}
              >
                {'\u00BB'}
              </motion.span>
            </motion.button>
          </div>

          {/* Connection status */}
          <div className="px-3 py-3 border-t border-slate-800/40">
            <div className="flex items-center gap-2.5 min-w-0">
              <motion.div
                className={`w-2 h-2 rounded-full shrink-0 ${
                  connected ? 'bg-emerald-400' : 'bg-red-400'
                }`}
                animate={{
                  boxShadow: connected
                    ? ['0 0 4px rgba(52,211,153,0.4)', '0 0 8px rgba(52,211,153,0.6)', '0 0 4px rgba(52,211,153,0.4)']
                    : '0 0 4px rgba(248,113,113,0.4)',
                }}
                transition={{
                  duration: 2,
                  repeat: Infinity,
                  ease: 'easeInOut',
                }}
              />
              <AnimatePresence>
                {!collapsed && (
                  <motion.span
                    className="text-xs text-slate-500 truncate"
                    initial={{ opacity: 0, width: 0 }}
                    animate={{ opacity: 1, width: 'auto' }}
                    exit={{ opacity: 0, width: 0 }}
                    transition={{ type: 'spring', stiffness: 300, damping: 30 }}
                  >
                    {connected ? 'Connected' : 'Disconnected'}
                  </motion.span>
                )}
              </AnimatePresence>
            </div>
          </div>
        </motion.aside>

        {/* Main content */}
        <main className="flex-1 min-w-0 overflow-hidden">
          <AnimatePresence mode="wait">
            <motion.div
              key={activePanel}
              className="h-full"
              initial={{ opacity: 0, y: 8 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -8 }}
              transition={{ type: 'spring', stiffness: 300, damping: 30, duration: 0.2 }}
            >
              {children}
            </motion.div>
          </AnimatePresence>
        </main>
      </div>

      {/* Status bar */}
      <motion.div
        className="h-7 bg-slate-900/80 border-t border-slate-800/40 flex items-center justify-between px-3 text-[11px] text-slate-500 shrink-0"
        initial={{ opacity: 0, y: 10 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ type: 'spring', stiffness: 300, damping: 30, delay: 0.1 }}
      >
        {/* Left: connection + process */}
        <div className="flex items-center gap-3">
          <div className="flex items-center gap-1.5">
            <div
              className={`w-1.5 h-1.5 rounded-full ${
                connected ? 'bg-emerald-400' : 'bg-red-400'
              }`}
            />
            <span>{connected ? 'Online' : 'Offline'}</span>
          </div>
          {health?.process_name && (
            <>
              <span className="text-slate-700">|</span>
              <span className="font-mono text-slate-400">
                {health.process_name}
                {health.pid ? ` (${health.pid})` : ''}
              </span>
            </>
          )}
        </div>

        {/* Right: server URL */}
        <div className="flex items-center gap-2">
          <span className="text-slate-600 font-mono">{config.baseUrl}</span>
        </div>
      </motion.div>
    </div>
  );
}

export default Layout;
