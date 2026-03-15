import { useState } from 'react';
import Layout from './components/Layout';
import ProcessList from './panels/ProcessList';

function App() {
  const [activePanel, setActivePanel] = useState('processes');

  return (
    <Layout activePanel={activePanel} onNavigate={setActivePanel}>
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
      <div className="text-3xl text-slate-700">{icon}</div>
      <p className="text-sm text-slate-500">{name} panel coming soon</p>
    </div>
  );
}

export default App;
