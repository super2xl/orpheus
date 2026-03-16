const DEFAULT_URL = 'http://localhost:8765';

interface RequestOptions {
  timeout?: number;
}

class OrpheusClient {
  private baseUrl: string;
  private apiKey: string | null;
  private connected: boolean = false;
  private listeners: Set<(connected: boolean) => void> = new Set();
  private initPromise: Promise<void>;

  constructor(baseUrl?: string, apiKey?: string) {
    this.baseUrl = baseUrl || localStorage.getItem('orpheus_url') || DEFAULT_URL;
    this.apiKey = apiKey || localStorage.getItem('orpheus_api_key') || null;
    // Auto-init API key from Tauri backend — requests wait for this to complete
    this.initPromise = this.initApiKey();
  }

  /**
   * Attempt to get the API key from Tauri's Rust backend via IPC.
   * Falls back to localStorage if not running in Tauri.
   */
  private async initApiKey() {
    try {
      const { invoke } = await import('@tauri-apps/api/core');
      const key = await invoke<string | null>('get_api_key');
      if (key) {
        this.apiKey = key;
        localStorage.setItem('orpheus_api_key', key);
      }
    } catch {
      // Not running in Tauri (browser dev mode) — use existing key from localStorage
    }
  }

  async request<T>(endpoint: string, body?: object, options?: RequestOptions): Promise<T> {
    // Wait for API key init to complete before first request
    await this.initPromise;

    const headers: Record<string, string> = { 'Content-Type': 'application/json' };
    if (this.apiKey) {
      headers['Authorization'] = `Bearer ${this.apiKey}`;
    }

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), options?.timeout || 10000);

    try {
      const response = await fetch(`${this.baseUrl}/${endpoint}`, {
        method: body ? 'POST' : 'GET',
        headers,
        body: body ? JSON.stringify(body) : undefined,
        signal: controller.signal,
      });

      if (!response.ok) {
        const text = await response.text();
        throw new Error(text || `HTTP ${response.status}`);
      }

      this.setConnected(true);
      const json = await response.json();

      // MCP server wraps responses in { data: {...}, success: true }
      // Unwrap automatically so hooks get clean data
      if (json && typeof json === 'object' && 'success' in json) {
        if (json.success === false) {
          throw new Error(json.error || json.message || 'Request failed');
        }
        if ('data' in json) {
          return json.data as T;
        }
      }
      return json as T;
    } catch (err: any) {
      if (err.name === 'AbortError') {
        throw new Error('Request timeout');
      }
      if (err.message?.includes('fetch') || err.message?.includes('network') || err.message?.includes('Failed')) {
        this.setConnected(false);
      }
      throw err;
    } finally {
      clearTimeout(timeoutId);
    }
  }

  configure(baseUrl: string, apiKey?: string) {
    this.baseUrl = baseUrl;
    this.apiKey = apiKey || null;
    localStorage.setItem('orpheus_url', baseUrl);
    if (apiKey) localStorage.setItem('orpheus_api_key', apiKey);
  }

  getConfig() {
    return { baseUrl: this.baseUrl, apiKey: this.apiKey };
  }

  isConnected() {
    return this.connected;
  }

  private setConnected(value: boolean) {
    if (this.connected !== value) {
      this.connected = value;
      this.listeners.forEach(fn => fn(value));
    }
  }

  onConnectionChange(fn: (connected: boolean) => void): () => void {
    this.listeners.add(fn);
    return () => { this.listeners.delete(fn); };
  }
}

export const orpheus = new OrpheusClient();
export default OrpheusClient;
