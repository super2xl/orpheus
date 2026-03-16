import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { PointerChainResult } from '../api/types';

export function usePointerChain() {
  const [chain, setChain] = useState<PointerChainResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const resolve = useCallback(async (pid: number, baseAddress: string, offsets: number[], readFinal?: boolean) => {
    setLoading(true);
    setError(null);
    setChain(null);
    try {
      const result = await orpheus.request<PointerChainResult>('tools/resolve_pointer', {
        pid,
        base: baseAddress,
        offsets,
        ...(readFinal !== undefined && { read_final: readFinal }),
      });
      setChain(result);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { chain, loading, error, resolve };
}
