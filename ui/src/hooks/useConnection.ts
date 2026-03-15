import { useState, useEffect, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { HealthInfo } from '../api/types';

export function useConnection() {
  const [connected, setConnected] = useState(false);
  const [health, setHealth] = useState<HealthInfo | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    return orpheus.onConnectionChange(setConnected);
  }, []);

  const checkHealth = useCallback(async () => {
    try {
      const h = await orpheus.request<HealthInfo>('get_health');
      setHealth(h);
      setConnected(true);
      setError(null);
    } catch (err: any) {
      setConnected(false);
      setError(err.message);
    }
  }, []);

  useEffect(() => {
    checkHealth();
    const interval = setInterval(checkHealth, 5000);
    return () => clearInterval(interval);
  }, [checkHealth]);

  const configure = useCallback((url: string, apiKey?: string) => {
    orpheus.configure(url, apiKey);
    checkHealth();
  }, [checkHealth]);

  return { connected, health, error, configure, checkHealth };
}
