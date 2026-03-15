import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { FunctionInfo, FunctionRecoveryResult } from '../api/types';

export interface RecoveryOptions {
  prologues: boolean;
  call_targets: boolean;
  exception_data: boolean;
  rtti: boolean;
  exports: boolean;
  max_functions: number;
}

export function useFunctionRecovery() {
  const [functions, setFunctions] = useState<FunctionInfo[]>([]);
  const [scanTime, setScanTime] = useState<number | null>(null);
  const [stats, setStats] = useState<Record<string, number> | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const recover = useCallback(async (
    pid: number,
    moduleBase: string,
    moduleSize: number,
    options: RecoveryOptions,
  ) => {
    setLoading(true);
    setFunctions([]);
    setScanTime(null);
    setStats(null);
    setError(null);

    try {
      const res = await orpheus.request<FunctionRecoveryResult>('tools/recover_functions', {
        pid,
        module_base: moduleBase,
        module_size: moduleSize,
        ...options,
      }, { timeout: 120000 });

      setFunctions(res.functions || []);
      setScanTime(res.scan_time_ms);
      setStats(res.stats || null);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { functions, scanTime, stats, loading, error, recover };
}
