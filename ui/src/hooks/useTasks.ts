import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import { log } from './useLogger';
import type { TaskInfo } from '../api/types';

export function useTasks() {
  const [tasks, setTasks] = useState<TaskInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const result = await orpheus.request<{ tasks: TaskInfo[] }>('tools/task_list', {});
      setTasks(result.tasks || []);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  const cancel = useCallback(async (taskId: string) => {
    try {
      await orpheus.request('tools/task_cancel', { task_id: taskId });
      log('INF', 'tasks', `Cancelled task ${taskId}`);
      await refresh();
    } catch (err: any) {
      log('ERR', 'tasks', `Failed to cancel task ${taskId}: ${err.message}`);
    }
  }, [refresh]);

  const cleanup = useCallback(async () => {
    try {
      await orpheus.request('tools/task_cleanup', {});
      log('INF', 'tasks', 'Cleaned up completed tasks');
      await refresh();
    } catch (err: any) {
      log('ERR', 'tasks', `Failed to cleanup tasks: ${err.message}`);
    }
  }, [refresh]);

  return { tasks, loading, error, refresh, cancel, cleanup };
}
