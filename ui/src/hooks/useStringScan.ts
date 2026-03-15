import { useState, useCallback, useRef } from 'react';
import { orpheus } from '../api/client';
import type { StringScanResult, TaskInfo } from '../api/types';

interface StringScanParams {
  pid: number;
  address: string;
  size: number;
  min_length: number;
  scan_ascii: boolean;
  scan_utf16: boolean;
  contains?: string;
}

export function useStringScan() {
  const [result, setResult] = useState<StringScanResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [task, setTask] = useState<TaskInfo | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const cancel = useCallback(() => {
    if (pollRef.current) {
      clearInterval(pollRef.current);
      pollRef.current = null;
    }
    setLoading(false);
  }, []);

  const scanAsync = useCallback(async (params: StringScanParams) => {
    setLoading(true);
    setResult(null);
    setTask(null);
    setError(null);

    try {
      const res = await orpheus.request<{ task_id: string }>('tools/scan_strings_async', params);

      // Poll for task completion
      if (pollRef.current) clearInterval(pollRef.current);
      pollRef.current = setInterval(async () => {
        try {
          const taskInfo = await orpheus.request<TaskInfo>('tools/task_status', {
            task_id: res.task_id,
          });
          setTask(taskInfo);
          if (taskInfo.state === 'completed' && taskInfo.result) {
            setResult(taskInfo.result as StringScanResult);
            setLoading(false);
            if (pollRef.current) clearInterval(pollRef.current);
          } else if (taskInfo.state === 'failed') {
            setError(taskInfo.error || 'String scan failed');
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

  return { result, loading, error, task, scanAsync, cancel };
}
