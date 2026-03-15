import { useState, useEffect, useCallback } from 'react';
import Layout from './components/Layout';
import ProcessList from './panels/ProcessList';

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
      {activePanel === 'modules' && <PlaceholderPanel name="Modules" icon={'\u29C9'} />}
      {activePanel === 'memory' && <PlaceholderPanel name="Memory" icon={'\u2B1A'} />}
      {activePanel === 'disassembly' && <PlaceholderPanel name="Disassembly" icon={'\u{1D4AE}'} />}
      {activePanel === 'scanner' && <PlaceholderPanel name="Scanner" icon={'\u29BF'} />}
    </Layout>
  );
}

function PlaceholderPanel({ name, icon }: { name: string; icon: string }) {
  return (
    <div className="h-full flex flex-col items-center justify-center gap-3">
      <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{icon}</div>
      <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>{name} panel coming soon</p>
    </div>
  );
}

export default App;
