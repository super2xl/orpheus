import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { VTableInfo } from '../api/types';

export function useVTableReader() {
  const [vtable, setVtable] = useState<VTableInfo | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const read = useCallback(async (pid: number, address: string, maxEntries: number = 20) => {
    setLoading(true);
    setError(null);
    setVtable(null);
    try {
      const result = await orpheus.request<VTableInfo>('read_vtable', {
        pid,
        address,
        max_entries: maxEntries,
      });
      setVtable(result);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { vtable, loading, error, read };
}
