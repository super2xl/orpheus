import React, { createContext, useContext, useState, useCallback, useEffect } from 'react';
import { orpheus } from '../api/client';

interface DmaStatus {
  connected: boolean;
  device_type?: string;
}

interface DmaContextValue {
  connected: boolean;
  deviceType: string | null;
  loading: boolean;
  error: string | null;
  connect: (deviceType?: string) => Promise<boolean>;
  disconnect: () => Promise<void>;
}

const DmaContext = createContext<DmaContextValue>({
  connected: false,
  deviceType: null,
  loading: false,
  error: null,
  connect: async () => false,
  disconnect: async () => {},
});

export function DmaProvider({ children }: { children: React.ReactNode }) {
  const [connected, setConnected] = useState(false);
  const [deviceType, setDeviceType] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // Check DMA status on mount and periodically
  const checkStatus = useCallback(async () => {
    try {
      const status = await orpheus.request<DmaStatus>('tools/dma_status');
      setConnected(status.connected);
      setDeviceType(status.device_type || null);
    } catch {
      // Server not reachable
    }
  }, []);

  useEffect(() => {
    checkStatus();
    const interval = setInterval(checkStatus, 3000);
    return () => clearInterval(interval);
  }, [checkStatus]);

  const connect = useCallback(async (device: string = 'fpga') => {
    setLoading(true);
    setError(null);
    try {
      const result = await orpheus.request<{ success: boolean; device_type?: string; error?: string }>(
        'tools/connect_dma', { device_type: device }
      );
      if (result.success) {
        setConnected(true);
        setDeviceType(result.device_type || device);
        return true;
      } else {
        setError(result.error || 'Failed to connect');
        return false;
      }
    } catch (err: any) {
      setError(err.message);
      return false;
    } finally {
      setLoading(false);
    }
  }, []);

  const disconnect = useCallback(async () => {
    try {
      await orpheus.request('tools/disconnect_dma', {});
      setConnected(false);
      setDeviceType(null);
    } catch {}
  }, []);

  return (
    <DmaContext.Provider value={{ connected, deviceType, loading, error, connect, disconnect }}>
      {children}
    </DmaContext.Provider>
  );
}

export function useDma() {
  return useContext(DmaContext);
}
