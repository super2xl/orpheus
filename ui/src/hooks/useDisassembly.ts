import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { InstructionInfo } from '../api/types';

export function useDisassembly() {
  const [instructions, setInstructions] = useState<InstructionInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const disassemble = useCallback(async (pid: number, address: string, count?: number) => {
    setLoading(true);
    try {
      const result = await orpheus.request<{ instructions: InstructionInfo[] }>('tools/disassemble', {
        pid,
        address,
        ...(count && { count }),
      });
      setInstructions(result.instructions || []);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { instructions, loading, error, disassemble };
}
