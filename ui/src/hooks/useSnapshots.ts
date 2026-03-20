import { useState, useCallback, useEffect, useRef } from 'react';
import { orpheus } from '../api/client';

export interface SnapshotInfo {
  name: string;
  address: string;
  size: number;
  timestamp: number;
}

export interface DiffEntry {
  offset: number;
  old_value: number;
  new_value: number;
}

interface UseSnapshotsReturn {
  snapshots: SnapshotInfo[];
  loadingSnapshots: boolean;
  snapshotError: string | null;
  takeSnapshot: (pid: number, address: string, size: number, name: string) => Promise<void>;
  deleteSnapshot: (name: string) => Promise<void>;
  refreshSnapshots: () => Promise<void>;
  diff: DiffEntry[];
  diffLoading: boolean;
  diffError: string | null;
  runDiff: (pid: number, name1: string, name2?: string) => Promise<void>;
  clearDiff: () => void;
  takingSnapshot: boolean;
  deletingSnapshot: string | null;
}

export function useSnapshots(): UseSnapshotsReturn {
  const [snapshots, setSnapshots] = useState<SnapshotInfo[]>([]);
  const [loadingSnapshots, setLoadingSnapshots] = useState(false);
  const [snapshotError, setSnapshotError] = useState<string | null>(null);
  const [takingSnapshot, setTakingSnapshot] = useState(false);
  const [deletingSnapshot, setDeletingSnapshot] = useState<string | null>(null);

  const [diff, setDiff] = useState<DiffEntry[]>([]);
  const [diffLoading, setDiffLoading] = useState(false);
  const [diffError, setDiffError] = useState<string | null>(null);

  const refreshSnapshots = useCallback(async () => {
    setLoadingSnapshots(true);
    setSnapshotError(null);
    try {
      const result = await orpheus.request<SnapshotInfo[]>('tools/memory_snapshot_list', {});
      setSnapshots(Array.isArray(result) ? result : []);
    } catch (err: any) {
      setSnapshotError(err.message);
    } finally {
      setLoadingSnapshots(false);
    }
  }, []);

  // Auto-refresh on mount and every 5 seconds
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);
  useEffect(() => {
    refreshSnapshots();
    intervalRef.current = setInterval(refreshSnapshots, 5000);
    return () => {
      if (intervalRef.current) clearInterval(intervalRef.current);
    };
  }, [refreshSnapshots]);

  const takeSnapshot = useCallback(async (pid: number, address: string, size: number, name: string) => {
    setTakingSnapshot(true);
    setSnapshotError(null);
    try {
      await orpheus.request('tools/memory_snapshot', { pid, address, size, name });
      await refreshSnapshots();
    } catch (err: any) {
      setSnapshotError(err.message);
      throw err;
    } finally {
      setTakingSnapshot(false);
    }
  }, [refreshSnapshots]);

  const deleteSnapshot = useCallback(async (name: string) => {
    setDeletingSnapshot(name);
    setSnapshotError(null);
    try {
      await orpheus.request('tools/memory_snapshot_delete', { name });
      await refreshSnapshots();
    } catch (err: any) {
      setSnapshotError(err.message);
    } finally {
      setDeletingSnapshot(null);
    }
  }, [refreshSnapshots]);

  const runDiff = useCallback(async (pid: number, name1: string, name2?: string) => {
    setDiffLoading(true);
    setDiffError(null);
    setDiff([]);
    try {
      const body: Record<string, unknown> = { pid, name1 };
      if (name2 && name2 !== '__current__') body.name2 = name2;
      const result = await orpheus.request<DiffEntry[]>('tools/memory_diff', body);
      setDiff(Array.isArray(result) ? result : []);
    } catch (err: any) {
      setDiffError(err.message);
    } finally {
      setDiffLoading(false);
    }
  }, []);

  const clearDiff = useCallback(() => {
    setDiff([]);
    setDiffError(null);
  }, []);

  return {
    snapshots,
    loadingSnapshots,
    snapshotError,
    takeSnapshot,
    deleteSnapshot,
    refreshSnapshots,
    diff,
    diffLoading,
    diffError,
    runDiff,
    clearDiff,
    takingSnapshot,
    deletingSnapshot,
  };
}
