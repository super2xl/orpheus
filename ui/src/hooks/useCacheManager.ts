import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import { log } from './useLogger';
import type { CacheStats, CacheEntry } from '../api/types';

export function useCacheManager() {
  const [stats, setStats] = useState<CacheStats | null>(null);
  const [rttiEntries, setRttiEntries] = useState<CacheEntry[]>([]);
  const [schemaEntries, setSchemaEntries] = useState<CacheEntry[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refreshStats = useCallback(async () => {
    try {
      const result = await orpheus.request<CacheStats>('cache_stats');
      setStats(result);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    }
  }, []);

  const refreshEntries = useCallback(async () => {
    setLoading(true);
    try {
      const [rtti, schema] = await Promise.allSettled([
        orpheus.request<{ entries: CacheEntry[] }>('rtti_cache_list'),
        orpheus.request<{ entries: CacheEntry[] }>('cs2_schema_cache_list'),
      ]);
      setRttiEntries(rtti.status === 'fulfilled' ? (rtti.value.entries || []) : []);
      setSchemaEntries(schema.status === 'fulfilled' ? (schema.value.entries || []) : []);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  const refresh = useCallback(async () => {
    await Promise.all([refreshStats(), refreshEntries()]);
  }, [refreshStats, refreshEntries]);

  const clearAll = useCallback(async () => {
    try {
      await orpheus.request('clear_cache', {});
      log('INF', 'cache', 'Cleared all caches');
      await refresh();
    } catch (err: any) {
      log('ERR', 'cache', `Failed to clear cache: ${err.message}`);
    }
  }, [refresh]);

  const clearRtti = useCallback(async () => {
    try {
      await orpheus.request('rtti_cache_clear', {});
      log('INF', 'cache', 'Cleared RTTI cache');
      await refresh();
    } catch (err: any) {
      log('ERR', 'cache', `Failed to clear RTTI cache: ${err.message}`);
    }
  }, [refresh]);

  const clearSchema = useCallback(async () => {
    try {
      await orpheus.request('cache_clear', { type: 'schema' });
      log('INF', 'cache', 'Cleared schema cache');
      await refresh();
    } catch (err: any) {
      log('ERR', 'cache', `Failed to clear schema cache: ${err.message}`);
    }
  }, [refresh]);

  const deleteEntry = useCallback(async (filepath: string) => {
    try {
      await orpheus.request('cache_delete', { filepath });
      log('INF', 'cache', `Deleted cache entry: ${filepath}`);
      await refresh();
    } catch (err: any) {
      log('ERR', 'cache', `Failed to delete cache entry: ${err.message}`);
    }
  }, [refresh]);

  return {
    stats,
    rttiEntries,
    schemaEntries,
    loading,
    error,
    refresh,
    refreshStats,
    clearAll,
    clearRtti,
    clearSchema,
    deleteEntry,
  };
}
