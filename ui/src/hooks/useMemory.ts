import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';

export interface MemoryReadResult {
  address: string;
  hex?: string;
  bytes?: number[];
  hexdump?: string;
}

export function useMemory() {
  const [data, setData] = useState<MemoryReadResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async (pid: number, address?: string, size?: number) => {
    setLoading(true);
    try {
      const result = await orpheus.request<MemoryReadResult>('tools/read_memory', {
        pid,
        ...(address && { address }),
        ...(size && { size }),
      });
      setData(result);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { data, loading, error, refresh };
}
