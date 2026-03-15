import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { DecompileResult } from '../api/types';

export function useDecompiler() {
  const [code, setCode] = useState<string>('');
  const [functionName, setFunctionName] = useState<string>('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const decompile = useCallback(async (pid: number, address: string, maxInstructions?: number) => {
    setLoading(true);
    setError(null);
    try {
      const result = await orpheus.request<DecompileResult>('decompile', {
        pid,
        address,
        ...(maxInstructions && { max_instructions: maxInstructions }),
      }, { timeout: 30000 });
      setCode(result.code || '');
      setFunctionName(result.function_name || '');
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { code, functionName, loading, error, decompile };
}
