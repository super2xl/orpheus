import { useState, useCallback, useRef } from 'react';
import { orpheus } from '../api/client';
import type { ScanResult, TaskInfo } from '../api/types';

export function usePatternScan() {
  const [result, setResult] = useState<ScanResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [task, setTask] = useState<TaskInfo | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const scan = useCallback(async (pid: number, pattern: string, moduleName?: string) => {
    setLoading(true);
    setResult(null);
    setTask(null);
    try {
      const res = await orpheus.request<ScanResult>('scan_pattern', {
        pid,
        pattern,
        ...(moduleName && { module_name: moduleName }),
      });
      setResult(res);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  const scanAsync = useCallback(async (pid: number, pattern: string, moduleName?: string) => {
    setLoading(true);
    setResult(null);
    setTask(null);
    try {
      const res = await orpheus.request<{ task_id: string }>('scan_pattern_async', {
        pid,
        pattern,
        ...(moduleName && { module_name: moduleName }),
      });

      // Poll for task completion
      if (pollRef.current) clearInterval(pollRef.current);
      pollRef.current = setInterval(async () => {
        try {
          const taskInfo = await orpheus.request<TaskInfo>('get_task_status', {
            task_id: res.task_id,
          });
          setTask(taskInfo);
          if (taskInfo.state === 'completed' && taskInfo.result) {
            setResult(taskInfo.result as ScanResult);
            setLoading(false);
            if (pollRef.current) clearInterval(pollRef.current);
          } else if (taskInfo.state === 'failed') {
            setError(taskInfo.error || 'Scan failed');
            setLoading(false);
            if (pollRef.current) clearInterval(pollRef.current);
          }
        } catch (err: any) {
          setError(err.message);
          setLoading(false);
          if (pollRef.current) clearInterval(pollRef.current);
        }
      }, 500);

      setError(null);
    } catch (err: any) {
      setError(err.message);
      setLoading(false);
    }
  }, []);

  return { result, loading, error, task, scan, scanAsync };
}
