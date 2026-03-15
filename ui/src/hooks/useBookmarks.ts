import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { Bookmark } from '../api/types';

interface NewBookmark {
  address: string;
  label: string;
  notes: string;
  category: string;
  module: string;
}

export function useBookmarks() {
  const [bookmarks, setBookmarks] = useState<Bookmark[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const res = await orpheus.request<{ bookmarks: Bookmark[] }>('tools/bookmarks');
      setBookmarks(res.bookmarks || []);
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  const add = useCallback(async (bookmark: NewBookmark) => {
    try {
      await orpheus.request('tools/bookmarks/add', bookmark);
      setError(null);
      await refresh();
    } catch (err: any) {
      setError(err.message);
    }
  }, [refresh]);

  const remove = useCallback(async (index: number) => {
    try {
      await orpheus.request('tools/bookmarks/remove', { index });
      setError(null);
      await refresh();
    } catch (err: any) {
      setError(err.message);
    }
  }, [refresh]);

  const update = useCallback(async (index: number, bookmark: Partial<Bookmark>) => {
    try {
      await orpheus.request('tools/bookmarks/update', { index, ...bookmark });
      setError(null);
      await refresh();
    } catch (err: any) {
      setError(err.message);
    }
  }, [refresh]);

  return { bookmarks, loading, error, refresh, add, remove, update };
}
