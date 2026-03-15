import { useState, useCallback, useRef } from 'react';
import { orpheus } from '../api/client';
import type { RTTIScanResult, RTTIClassInfo, TaskInfo } from '../api/types';

export function useRTTI() {
  const [results, setResults] = useState<RTTIClassInfo[]>([]);
  const [scanTime, setScanTime] = useState<number | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [progress, setProgress] = useState(0);
  const [statusMessage, setStatusMessage] = useState('');
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const cancelledRef = useRef(false);

  const cancel = useCallback(() => {
    cancelledRef.current = true;
    if (pollRef.current) clearInterval(pollRef.current);
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
    cancelledRef.current = false;

    try {
      const res = await orpheus.request<{ task_id: string }>('tools/rtti_scan_module', {
        pid,
        module_base: moduleBase,
        module_size: moduleSize,
      });

      // Poll for task completion
      if (pollRef.current) clearInterval(pollRef.current);
      pollRef.current = setInterval(async () => {
        if (cancelledRef.current) {
          if (pollRef.current) clearInterval(pollRef.current);
          return;
        }
        try {
          const taskInfo = await orpheus.request<TaskInfo>('tools/task_status', {
            task_id: res.task_id,
          });
          setProgress(taskInfo.progress);
          setStatusMessage(taskInfo.status_message || 'Scanning...');
          if (taskInfo.state === 'completed' && taskInfo.result) {
            const scanResult = taskInfo.result as RTTIScanResult;
            setResults(scanResult.classes || []);
            setScanTime(scanResult.scan_time_ms);
            setLoading(false);
            if (pollRef.current) clearInterval(pollRef.current);
          } else if (taskInfo.state === 'failed') {
            setError(taskInfo.error || 'RTTI scan failed');
            setLoading(false);
            if (pollRef.current) clearInterval(pollRef.current);
          }
        } catch (err: any) {
          setError(err.message);
          setLoading(false);
          if (pollRef.current) clearInterval(pollRef.current);
        }
      }, 500);
    } catch (err: any) {
      setError(err.message);
      setLoading(false);
    }
  }, []);

  const parseVTable = useCallback(async (pid: number, address: string) => {
    try {
      const res = await orpheus.request<{ entries: string[] }>('tools/rtti_parse_vtable', {
        pid,
        address,
      });
      return res.entries || [];
    } catch (err: any) {
      setError(err.message);
      return [];
    }
  }, []);

  return { results, scanTime, loading, error, progress, statusMessage, scan, cancel, parseVTable };
}
