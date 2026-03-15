import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { MemoryRegion } from '../api/types';

export function useMemoryRegions() {
  const [regions, setRegions] = useState<MemoryRegion[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async (pid: number) => {
    setLoading(true);
    try {
      const result = await orpheus.request<{ regions: MemoryRegion[] }>('memory_regions', { pid });
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
