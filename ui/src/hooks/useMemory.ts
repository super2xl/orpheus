import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { MemoryRegion } from '../api/types';

export function useMemory() {
  const [regions, setRegions] = useState<MemoryRegion[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async (pid: number, address?: string, size?: number) => {
    setLoading(true);
    try {
      const result = await orpheus.request<{ regions: MemoryRegion[] }>('tools/read_memory', {
        pid,
        ...(address && { address }),
        ...(size && { size }),
      });
      setRegions(result.regions || []);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { regions, loading, error, refresh };
}
