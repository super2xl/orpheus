import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { ModuleInfo } from '../api/types';

export function useModules() {
  const [modules, setModules] = useState<ModuleInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async (pid: number) => {
    setLoading(true);
    try {
      const result = await orpheus.request<{ modules: ModuleInfo[] }>('get_modules', { pid });
      setModules(result.modules || []);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { modules, loading, error, refresh };
}
