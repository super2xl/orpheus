import { useState, useEffect, useCallback } from 'react';
import Layout from './components/Layout';
import CommandPalette from './components/CommandPalette';
import ProcessList from './panels/ProcessList';
import ModuleBrowser from './panels/ModuleBrowser';
import MemoryViewer from './panels/MemoryViewer';
import Disassembly from './panels/Disassembly';
import PatternScanner from './panels/PatternScanner';
import StringScanner from './panels/StringScanner';
import Bookmarks from './panels/Bookmarks';
import XrefFinder from './panels/XrefFinder';
import RTTIScanner from './panels/RTTIScanner';
import FunctionRecovery from './panels/FunctionRecovery';
import WriteTracer from './panels/WriteTracer';
import Emulator from './panels/Emulator';
import Decompiler from './panels/Decompiler';
import CFGViewer from './panels/CFGViewer';
import MemoryRegions from './panels/MemoryRegions';
import PointerChainPanel from './panels/PointerChain';
import VTableReader from './panels/VTableReader';
import Settings from './panels/Settings';

function App() {
  const [activePanel, setActivePanel] = useState('processes');
  const [commandPaletteOpen, setCommandPaletteOpen] = useState(false);
  const [dark, setDark] = useState(() => {
    const stored = localStorage.getItem('orpheus-theme');
    return stored ? stored === 'dark' : true; // default to dark
  });

  useEffect(() => {
    document.documentElement.classList.toggle('dark', dark);
    localStorage.setItem('orpheus-theme', dark ? 'dark' : 'light');
  }, [dark]);

  const toggleTheme = useCallback(() => setDark((d) => !d), []);

  const handleNavigate = useCallback((panel: string, _address?: string) => {
    setActivePanel(panel);
  }, []);

  // Global Ctrl+K listener for command palette
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
        e.preventDefault();
        setCommandPaletteOpen(prev => !prev);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);

  return (
    <>
      <Layout activePanel={activePanel} onNavigate={handleNavigate} dark={dark} onToggleTheme={toggleTheme}>
        {activePanel === 'processes' && <ProcessList onNavigate={handleNavigate} />}
        {activePanel === 'modules' && <ModuleBrowser onNavigate={handleNavigate} />}
        {activePanel === 'memory' && <MemoryViewer />}
        {activePanel === 'disassembly' && <Disassembly onNavigate={handleNavigate} />}
        {activePanel === 'scanner' && <PatternScanner onNavigate={handleNavigate} />}
        {activePanel === 'strings' && <StringScanner onNavigate={handleNavigate} />}
        {activePanel === 'xrefs' && <XrefFinder onNavigate={handleNavigate} />}
        {activePanel === 'bookmarks' && <Bookmarks onNavigate={handleNavigate} />}
        {activePanel === 'rtti' && <RTTIScanner onNavigate={handleNavigate} />}
        {activePanel === 'functions' && <FunctionRecovery onNavigate={handleNavigate} />}
        {activePanel === 'decompiler' && <Decompiler />}
        {activePanel === 'cfg' && <CFGViewer />}
        {activePanel === 'write-tracer' && <WriteTracer onNavigate={handleNavigate} />}
        {activePanel === 'emulator' && <Emulator onNavigate={handleNavigate} />}
        {activePanel === 'regions' && <MemoryRegions onNavigate={handleNavigate} />}
        {activePanel === 'pointers' && <PointerChainPanel onNavigate={handleNavigate} />}
        {activePanel === 'vtable' && <VTableReader onNavigate={handleNavigate} />}
        {activePanel === 'settings' && <Settings dark={dark} onToggleTheme={toggleTheme} />}
      </Layout>
      <CommandPalette
        open={commandPaletteOpen}
        onClose={() => setCommandPaletteOpen(false)}
        onNavigate={handleNavigate}
        onToggleTheme={toggleTheme}
      />
    </>
  );
}

export default App;
