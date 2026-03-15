import { useState, useEffect, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { HealthInfo } from '../api/types';

interface DmaStatus {
  connected: boolean;
  device_type?: string;
}

export function useConnection() {
  const [connected, setConnected] = useState(false);
  const [health, setHealth] = useState<HealthInfo | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [dmaStatus, setDmaStatus] = useState<DmaStatus>({ connected: false });
  const [dmaLoading, setDmaLoading] = useState(false);
  const [dmaError, setDmaError] = useState<string | null>(null);

  useEffect(() => {
    return orpheus.onConnectionChange(setConnected);
  }, []);

  const checkHealth = useCallback(async () => {
    try {
      const h = await orpheus.request<HealthInfo>('health');
      setHealth(h);
      setConnected(true);
      setError(null);
    } catch (err: any) {
      setConnected(false);
      setError(err.message);
    }
  }, []);

  const checkDmaStatus = useCallback(async () => {
    try {
      const status = await orpheus.request<DmaStatus>('tools/dma_status');
      setDmaStatus(status);
    } catch {
      // Server not reachable, leave status as-is
    }
  }, []);

  useEffect(() => {
    checkHealth();
    const interval = setInterval(checkHealth, 5000);
    return () => clearInterval(interval);
  }, [checkHealth]);

  // Poll DMA status when server is connected
  useEffect(() => {
    if (!connected) return;
    checkDmaStatus();
    const interval = setInterval(checkDmaStatus, 3000);
    return () => clearInterval(interval);
  }, [connected, checkDmaStatus]);

  const configure = useCallback((url: string, apiKey?: string) => {
    orpheus.configure(url, apiKey);
    checkHealth();
  }, [checkHealth]);

  const connectDma = useCallback(async (deviceType: string = 'fpga') => {
    setDmaLoading(true);
    setDmaError(null);
    try {
      const result = await orpheus.request<{ success: boolean; message?: string; error?: string; device_type?: string }>(
        'tools/connect_dma',
        { device_type: deviceType },
      );
      if (result.success) {
        setDmaStatus({ connected: true, device_type: result.device_type });
      } else {
        setDmaError(result.error || 'Failed to connect');
      }
    } catch (err: any) {
      setDmaError(err.message);
    } finally {
      setDmaLoading(false);
    }
  }, []);

  const disconnectDma = useCallback(async () => {
    setDmaLoading(true);
    setDmaError(null);
    try {
      await orpheus.request('tools/disconnect_dma', {});
      setDmaStatus({ connected: false });
    } catch (err: any) {
      setDmaError(err.message);
    } finally {
      setDmaLoading(false);
    }
  }, []);

  return {
    connected,
    health,
    error,
    configure,
    checkHealth,
    dmaStatus,
    dmaLoading,
    dmaError,
    connectDma,
    disconnectDma,
  };
}
