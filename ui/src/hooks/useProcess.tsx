import React, { createContext, useContext, useState, useCallback } from 'react';
import type { ProcessInfo } from '../api/types';

interface ProcessContextValue {
  process: ProcessInfo | null;
  attach: (process: ProcessInfo) => void;
  detach: () => void;
}

const ProcessContext = createContext<ProcessContextValue>({
  process: null,
  attach: () => {},
  detach: () => {},
});

export function ProcessProvider({ children }: { children: React.ReactNode }) {
  const [process, setProcess] = useState<ProcessInfo | null>(null);

  const attach = useCallback((p: ProcessInfo) => {
    setProcess(p);
  }, []);

  const detach = useCallback(() => {
    setProcess(null);
  }, []);

  return (
    <ProcessContext.Provider value={{ process, attach, detach }}>
      {children}
    </ProcessContext.Provider>
  );
}

export function useProcess() {
  return useContext(ProcessContext);
}
