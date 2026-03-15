import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { XrefResult } from '../api/types';

export function useXrefs() {
  const [results, setResults] = useState<XrefResult[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const find = useCallback(async (pid: number, address: string, moduleBase?: string, moduleSize?: number) => {
    setLoading(true);
    setError(null);
    try {
      const body: Record<string, unknown> = { pid, address };
      if (moduleBase) body.module_base = moduleBase;
      if (moduleSize) body.module_size = moduleSize;
      const result = await orpheus.request<{ xrefs: XrefResult[] }>('tools/find_xrefs', body);
      setResults(result.xrefs || []);
    } catch (err: any) {
      setError(err.message);
      setResults([]);
    } finally {
      setLoading(false);
    }
  }, []);

  const clear = useCallback(() => {
    setResults([]);
    setError(null);
  }, []);

  return { results, loading, error, find, clear };
}
