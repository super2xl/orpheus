import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { RTTIClassInfo } from '../api/types';

interface RTTIScanResponse {
  status: string;
  module: string;
  count: number;
  summary: Record<string, unknown>;
}

export function useRTTI() {
  const [results, setResults] = useState<RTTIClassInfo[]>([]);
  const [scanTime, setScanTime] = useState<number | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [progress, setProgress] = useState(0);
  const [statusMessage, setStatusMessage] = useState('');

  const cancel = useCallback(() => {
    setLoading(false);
    setProgress(0);
    setStatusMessage('');
  }, []);

  const scan = useCallback(async (pid: number, moduleBase: string, moduleSize: number) => {
    setLoading(true);
    setResults([]);
    setScanTime(null);
    setError(null);
    setProgress(0);
    setStatusMessage('Starting RTTI scan...');

    try {
      // rtti_scan_module is synchronous - returns results directly
      const res = await orpheus.request<RTTIScanResponse>('tools/rtti_scan_module', {
        pid,
        module_base: moduleBase,
        module_size: moduleSize,
      }, { timeout: 120000 });

      // The response has { status, module, count, summary }
      // Classes aren't returned inline - they're cached. Show summary info.
      setStatusMessage(`Scan complete: ${res.count} classes found`);
      setProgress(100);
      // If the response includes classes data in summary, extract it
      if (res.summary && Array.isArray((res.summary as any).classes)) {
        setResults((res.summary as any).classes);
      }
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  const parseVTable = useCallback(async (pid: number, vtableAddress: string) => {
    try {
      const res = await orpheus.request<{ entries: string[] }>('tools/rtti_parse_vtable', {
        pid,
        vtable_address: vtableAddress,
      });
      return res.entries || [];
    } catch (err: any) {
      setError(err.message);
      return [];
    }
  }, []);

  return { results, scanTime, loading, error, progress, statusMessage, scan, cancel, parseVTable };
}
