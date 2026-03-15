import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { ControlFlowGraph } from '../api/types';

export function useCFG() {
  const [graph, setGraph] = useState<ControlFlowGraph | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const build = useCallback(async (pid: number, address: string) => {
    setLoading(true);
    setError(null);
    try {
      const result = await orpheus.request<ControlFlowGraph>('tools/build_cfg', {
        pid,
        address,
      }, { timeout: 30000 });
      setGraph(result);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { graph, loading, error, build };
}
