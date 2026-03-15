const DEFAULT_URL = 'http://localhost:8765';

interface RequestOptions {
  timeout?: number;
}

class OrpheusClient {
  private baseUrl: string;
  private apiKey: string | null;
  private connected: boolean = false;
  private listeners: Set<(connected: boolean) => void> = new Set();

  constructor(baseUrl?: string, apiKey?: string) {
    this.baseUrl = baseUrl || localStorage.getItem('orpheus_url') || DEFAULT_URL;
    this.apiKey = apiKey || localStorage.getItem('orpheus_api_key') || null;
  }

  async request<T>(endpoint: string, body?: object, options?: RequestOptions): Promise<T> {
    const headers: Record<string, string> = { 'Content-Type': 'application/json' };
    if (this.apiKey) {
      headers['X-API-Key'] = this.apiKey;
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
      return response.json();
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
