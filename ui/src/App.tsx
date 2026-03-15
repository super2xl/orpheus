import { useState, useEffect, useCallback } from 'react';
import Layout from './components/Layout';
import ProcessList from './panels/ProcessList';
import ModuleBrowser from './panels/ModuleBrowser';
import MemoryViewer from './panels/MemoryViewer';
import Disassembly from './panels/Disassembly';
import PatternScanner from './panels/PatternScanner';

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

  return (
    <Layout activePanel={activePanel} onNavigate={setActivePanel} dark={dark} onToggleTheme={toggleTheme}>
      {activePanel === 'processes' && <ProcessList />}
      {activePanel === 'modules' && <ModuleBrowser />}
      {activePanel === 'memory' && <MemoryViewer />}
      {activePanel === 'disassembly' && <Disassembly />}
      {activePanel === 'scanner' && <PatternScanner />}
    </Layout>
  );
}

export default App;
