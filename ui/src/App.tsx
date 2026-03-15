import { useState, useEffect, useCallback } from 'react';
import Layout from './components/Layout';
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
import Settings from './panels/Settings';

function App() {
  const [activePanel, setActivePanel] = useState('processes');
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

  return (
    <Layout activePanel={activePanel} onNavigate={handleNavigate} dark={dark} onToggleTheme={toggleTheme}>
      {activePanel === 'processes' && <ProcessList />}
      {activePanel === 'modules' && <ModuleBrowser />}
      {activePanel === 'memory' && <MemoryViewer />}
      {activePanel === 'disassembly' && <Disassembly />}
      {activePanel === 'scanner' && <PatternScanner />}
      {activePanel === 'strings' && <StringScanner />}
      {activePanel === 'xrefs' && <XrefFinder onNavigate={handleNavigate} />}
      {activePanel === 'bookmarks' && <Bookmarks />}
      {activePanel === 'rtti' && <RTTIScanner />}
      {activePanel === 'functions' && <FunctionRecovery />}
      {activePanel === 'settings' && <Settings dark={dark} onToggleTheme={toggleTheme} />}
    </Layout>
  );
}

export default App;
