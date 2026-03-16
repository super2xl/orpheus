import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { FunctionRecoveryResult } from '../api/types';

export interface RecoveryOptions {
  use_prologues: boolean;
  follow_calls: boolean;
  use_exception_data: boolean;
  max_functions: number;
}

export function useFunctionRecovery() {
  const [summary, setSummary] = useState<FunctionRecoveryResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const recover = useCallback(async (
    pid: number,
    moduleBase: string,
    moduleSize: number,
    options: RecoveryOptions,
  ) => {
    setLoading(true);
    setSummary(null);
    setError(null);

    try {
      const res = await orpheus.request<FunctionRecoveryResult>('tools/recover_functions', {
        pid,
        module_base: moduleBase,
        module_size: moduleSize,
        use_prologues: options.use_prologues,
        follow_calls: options.follow_calls,
        use_exception_data: options.use_exception_data,
        max_functions: options.max_functions,
      }, { timeout: 120000 });

      setSummary(res);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { summary, loading, error, recover };
}
