import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { ProcessInfo } from '../api/types';

export function useProcesses() {
  const [processes, setProcesses] = useState<ProcessInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const result = await orpheus.request<{ processes: ProcessInfo[] }>('tools/processes');
      setProcesses(result.processes || []);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { processes, loading, error, refresh };
}
