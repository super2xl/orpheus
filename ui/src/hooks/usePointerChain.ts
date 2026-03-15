import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { PointerChainResult } from '../api/types';

export function usePointerChain() {
  const [chain, setChain] = useState<PointerChainResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const resolve = useCallback(async (pid: number, baseAddress: string, offsets: number[]) => {
    setLoading(true);
    setError(null);
    setChain(null);
    try {
      // Build nested expression: [[[base]+0x10]+0x20]+0x08
      let expression = baseAddress;
      for (const offset of offsets) {
        const offsetHex = '0x' + offset.toString(16).toUpperCase();
        expression = `[${expression}]+${offsetHex}`;
      }

      const result = await orpheus.request<PointerChainResult>('tools/resolve_pointer', {
        pid,
        expression,
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
