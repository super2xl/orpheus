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
      if (status.connected) {
        setLoading(false); // Connection complete
        setError(null);
      }
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
      await orpheus.request('tools/connect_dma', { device_type: device });
      // Server returns immediately — connection happens in background
      // The checkStatus poll (every 3s) will detect when it's actually connected
      // Keep loading=true until status poll shows connected
      return true;
    } catch (err: any) {
      setError(err.message);
      setLoading(false);
      return false;
    }
    // Note: loading stays true until checkStatus detects connected=true
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
