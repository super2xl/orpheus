import { useState, useEffect, useCallback } from 'react';
import { motion } from 'motion/react';
import { useConnection } from '../hooks/useConnection';
import { orpheus } from '../api/client';
import type { VersionInfo, CacheStats } from '../api/types';

interface SettingsProps {
  dark: boolean;
  onToggleTheme: () => void;
}

function Settings({ dark, onToggleTheme }: SettingsProps) {
  const { connected, health, configure, checkHealth } = useConnection();
  const config = orpheus.getConfig();

  const [url, setUrl] = useState(config.baseUrl);
  const [apiKey, setApiKey] = useState(config.apiKey || '');
  const [showApiKey, setShowApiKey] = useState(false);
  const [saved, setSaved] = useState(false);
  const [testing, setTesting] = useState(false);
  const [testResult, setTestResult] = useState<{ ok: boolean; message: string } | null>(null);

  const [version, setVersion] = useState<VersionInfo | null>(null);
  const [cacheStats, setCacheStats] = useState<CacheStats | null>(null);
  const [clearingCache, setClearingCache] = useState(false);

  // MCP Integration info
  const [mcpInfo, setMcpInfo] = useState<{
    url: string;
    port: number;
    api_key: string | null;
    auth_required: boolean;
    auth_note: string;
  } | null>(null);
  const [mcpKeyCopied, setMcpKeyCopied] = useState(false);
  const [mcpUrlCopied, setMcpUrlCopied] = useState(false);

  // Fetch version info
  useEffect(() => {
    orpheus.request<VersionInfo>('version')
      .then(setVersion)
      .catch(() => setVersion(null));
  }, []);

  // Fetch MCP info (for integration section)
  useEffect(() => {
    orpheus.request<{
      url: string;
      port: number;
      api_key: string | null;
      auth_required: boolean;
      auth_note: string;
    }>('tools/mcp_info')
      .then(setMcpInfo)
      .catch(() => setMcpInfo(null));
  }, []);

  // Fetch cache stats
  useEffect(() => {
    const fetchStats = () => {
      orpheus.request<CacheStats>('tools/cache_stats')
        .then(setCacheStats)
        .catch(() => setCacheStats(null));
    };
    fetchStats();
    const interval = setInterval(fetchStats, 5000);
    return () => clearInterval(interval);
  }, []);

  const handleSave = useCallback(() => {
    configure(url, apiKey || undefined);
    setSaved(true);
    setTimeout(() => setSaved(false), 2000);
  }, [url, apiKey, configure]);

  const handleTest = useCallback(async () => {
    setTesting(true);
    setTestResult(null);
    try {
      await checkHealth();
      setTestResult({ ok: true, message: 'Connection successful' });
    } catch (err: any) {
      setTestResult({ ok: false, message: err.message || 'Connection failed' });
    } finally {
      setTesting(false);
    }
  }, [checkHealth]);

  const handleClearCache = useCallback(async () => {
    setClearingCache(true);
    try {
      await orpheus.request('tools/cache_clear', {});
      // Refresh stats after clearing
      const stats = await orpheus.request<CacheStats>('tools/cache_stats');
      setCacheStats(stats);
    } catch {
      // Silently handle
    } finally {
      setClearingCache(false);
    }
  }, []);

  return (
    <div className="h-full flex flex-col overflow-auto">
      <motion.div
        className="shrink-0 px-6 pt-6 pb-4"
        initial={{ opacity: 0, y: -8 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.15, ease: 'easeOut' }}
      >
        <h1 className="text-lg tracking-tight" style={{ color: 'var(--text)', fontWeight: 500 }}>
          Settings
        </h1>
      </motion.div>

      <div className="flex-1 min-h-0 overflow-auto px-6 pb-6 space-y-6">
        {/* Section 1: Connection */}
        <motion.section
          className="rounded-lg p-5 space-y-4"
          style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}
          initial={{ opacity: 0, y: 8 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.15, delay: 0.03 }}
        >
          <h2 className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
            Connection
          </h2>

          {/* Server URL */}
          <div className="space-y-1.5">
            <label className="text-xs" style={{ color: 'var(--text-secondary)' }}>Server URL</label>
            <input
              type="text"
              value={url}
              onChange={(e) => setUrl(e.target.value)}
              className="w-full h-9 px-3 rounded-md font-mono text-xs outline-none"
              style={{
                background: 'var(--bg)',
                border: '1px solid var(--border)',
                color: 'var(--text)',
                transition: 'border-color 0.1s ease',
              }}
              onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
              onBlur={(e) => { e.currentTarget.style.borderColor = 'var(--border)'; }}
            />
          </div>

          {/* API Key */}
          <div className="space-y-1.5">
            <label className="text-xs" style={{ color: 'var(--text-secondary)' }}>API Key</label>
            <div className="relative">
              <input
                type={showApiKey ? 'text' : 'password'}
                value={apiKey}
                onChange={(e) => setApiKey(e.target.value)}
                placeholder="Optional"
                className="w-full h-9 px-3 pr-16 rounded-md font-mono text-xs outline-none"
                style={{
                  background: 'var(--bg)',
                  border: '1px solid var(--border)',
                  color: 'var(--text)',
                  transition: 'border-color 0.1s ease',
                }}
                onFocus={(e) => { e.currentTarget.style.borderColor = 'var(--text-muted)'; }}
                onBlur={(e) => { e.currentTarget.style.borderColor = 'var(--border)'; }}
              />
              <button
                onClick={() => setShowApiKey(!showApiKey)}
                className="absolute right-2 top-1/2 -translate-y-1/2 px-2 py-0.5 rounded text-[10px] cursor-pointer border-none outline-none"
                style={{
                  color: 'var(--text-muted)',
                  background: 'transparent',
                  transition: 'color 0.1s ease',
                }}
                onMouseEnter={(e) => { e.currentTarget.style.color = 'var(--text)'; }}
                onMouseLeave={(e) => { e.currentTarget.style.color = 'var(--text-muted)'; }}
              >
                {showApiKey ? 'Hide' : 'Show'}
              </button>
            </div>
          </div>

          {/* Buttons row */}
          <div className="flex items-center gap-2">
            <button
              onClick={handleSave}
              className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none"
              style={{
                fontWeight: 400,
                background: 'transparent',
                color: saved ? 'var(--text)' : 'var(--text-secondary)',
                border: '1px solid var(--border)',
                transition: 'all 0.1s ease',
              }}
              onMouseEnter={(e) => {
                e.currentTarget.style.background = 'var(--hover)';
                e.currentTarget.style.color = 'var(--text)';
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.background = 'transparent';
                e.currentTarget.style.color = saved ? 'var(--text)' : 'var(--text-secondary)';
              }}
            >
              {saved ? 'Saved' : 'Save'}
            </button>
            <button
              onClick={handleTest}
              disabled={testing}
              className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none disabled:opacity-40"
              style={{
                fontWeight: 400,
                background: 'transparent',
                color: 'var(--text-secondary)',
                border: '1px solid var(--border)',
                transition: 'all 0.1s ease',
              }}
              onMouseEnter={(e) => {
                e.currentTarget.style.background = 'var(--hover)';
                e.currentTarget.style.color = 'var(--text)';
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.background = 'transparent';
                e.currentTarget.style.color = 'var(--text-secondary)';
              }}
            >
              {testing ? 'Testing...' : 'Test Connection'}
            </button>
          </div>

          {/* Connection status */}
          <div className="flex items-center gap-2">
            <div
              className="w-2 h-2 rounded-full"
              style={{ background: connected ? 'var(--dot-connected)' : 'var(--dot-disconnected)' }}
            />
            <span className="text-xs" style={{ color: 'var(--text-muted)' }}>
              {connected ? 'Connected' : 'Disconnected'}
              {health?.version && ` \u00B7 v${health.version}`}
            </span>
          </div>

          {/* Test result */}
          {testResult && (
            <div
              className="px-3 py-2 rounded-md text-xs"
              style={{
                background: 'var(--bg)',
                border: '1px solid var(--border)',
                color: testResult.ok ? 'var(--text)' : 'var(--text-secondary)',
              }}
            >
              {testResult.message}
            </div>
          )}
        </motion.section>

        {/* Section 2: MCP Integration (only when connected) */}
        {connected && mcpInfo && (
          <motion.section
            className="rounded-lg p-5 space-y-4"
            style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}
            initial={{ opacity: 0, y: 8 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.15, delay: 0.05 }}
          >
            <h2 className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              MCP Integration
            </h2>

            <p className="text-xs" style={{ color: 'var(--text-secondary)', lineHeight: '1.5' }}>
              Copy the API key and server URL into MCPinstaller to connect Claude Desktop, Cursor, or other MCP clients.
            </p>

            {/* Server URL */}
            <div className="space-y-1.5">
              <label className="text-xs" style={{ color: 'var(--text-secondary)' }}>Server URL</label>
              <div className="flex items-center gap-2">
                <input
                  type="text"
                  value={mcpInfo.url}
                  readOnly
                  className="flex-1 h-9 px-3 rounded-md font-mono text-xs outline-none"
                  style={{
                    background: 'var(--bg)',
                    border: '1px solid var(--border)',
                    color: 'var(--text)',
                  }}
                />
                <button
                  onClick={() => {
                    navigator.clipboard.writeText(mcpInfo.url);
                    setMcpUrlCopied(true);
                    setTimeout(() => setMcpUrlCopied(false), 2000);
                  }}
                  className="px-3 h-9 rounded-md text-xs cursor-pointer border-none outline-none shrink-0"
                  style={{
                    fontWeight: 400,
                    background: 'transparent',
                    color: mcpUrlCopied ? 'var(--text)' : 'var(--text-secondary)',
                    border: '1px solid var(--border)',
                    transition: 'all 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = mcpUrlCopied ? 'var(--text)' : 'var(--text-secondary)';
                  }}
                >
                  {mcpUrlCopied ? 'Copied' : 'Copy'}
                </button>
              </div>
            </div>

            {/* API Key */}
            <div className="space-y-1.5">
              <label className="text-xs" style={{ color: 'var(--text-secondary)' }}>API Key</label>
              {mcpInfo.auth_required && mcpInfo.api_key ? (
                <div className="flex items-center gap-2">
                  <input
                    type="text"
                    value={mcpInfo.api_key}
                    readOnly
                    className="flex-1 h-9 px-3 rounded-md font-mono text-xs outline-none"
                    style={{
                      background: 'var(--bg)',
                      border: '1px solid var(--border)',
                      color: 'var(--text)',
                    }}
                  />
                  <button
                    onClick={() => {
                      if (mcpInfo.api_key) {
                        navigator.clipboard.writeText(mcpInfo.api_key);
                        setMcpKeyCopied(true);
                        setTimeout(() => setMcpKeyCopied(false), 2000);
                      }
                    }}
                    className="px-3 h-9 rounded-md text-xs cursor-pointer border-none outline-none shrink-0"
                    style={{
                      fontWeight: 400,
                      background: 'transparent',
                      color: mcpKeyCopied ? 'var(--text)' : 'var(--text-secondary)',
                      border: '1px solid var(--border)',
                      transition: 'all 0.1s ease',
                    }}
                    onMouseEnter={(e) => {
                      e.currentTarget.style.background = 'var(--hover)';
                      e.currentTarget.style.color = 'var(--text)';
                    }}
                    onMouseLeave={(e) => {
                      e.currentTarget.style.background = 'transparent';
                      e.currentTarget.style.color = mcpKeyCopied ? 'var(--text)' : 'var(--text-secondary)';
                    }}
                  >
                    {mcpKeyCopied ? 'Copied' : 'Copy'}
                  </button>
                </div>
              ) : (
                <div
                  className="px-3 py-2 rounded-md text-xs"
                  style={{
                    background: 'var(--bg)',
                    border: '1px solid var(--border)',
                    color: 'var(--text-muted)',
                  }}
                >
                  {mcpInfo.auth_required
                    ? 'API key is displayed in the Orpheus console on startup'
                    : 'Authentication disabled (embedded mode) — no API key required'}
                </div>
              )}
            </div>

            {/* Auth status */}
            <div className="flex items-center gap-2">
              <div
                className="w-2 h-2 rounded-full"
                style={{ background: mcpInfo.auth_required ? 'var(--dot-connected)' : 'var(--text-muted)' }}
              />
              <span className="text-xs" style={{ color: 'var(--text-muted)' }}>
                {mcpInfo.auth_note}
              </span>
            </div>
          </motion.section>
        )}

        {/* Section 3: Appearance */}
        <motion.section
          className="rounded-lg p-5 space-y-4"
          style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}
          initial={{ opacity: 0, y: 8 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.15, delay: 0.06 }}
        >
          <h2 className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
            Appearance
          </h2>

          <div className="flex items-center justify-between">
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>Theme</span>
            <div className="flex items-center gap-0.5 rounded-md" style={{ border: '1px solid var(--border)' }}>
              <button
                onClick={() => { if (dark) onToggleTheme(); }}
                className="px-3 h-7 rounded-l-md text-xs cursor-pointer border-none outline-none"
                style={{
                  fontWeight: 400,
                  background: !dark ? 'var(--active)' : 'transparent',
                  color: !dark ? 'var(--text)' : 'var(--text-muted)',
                  transition: 'all 0.1s ease',
                }}
              >
                Light
              </button>
              <button
                onClick={() => { if (!dark) onToggleTheme(); }}
                className="px-3 h-7 rounded-r-md text-xs cursor-pointer border-none outline-none"
                style={{
                  fontWeight: 400,
                  background: dark ? 'var(--active)' : 'transparent',
                  color: dark ? 'var(--text)' : 'var(--text-muted)',
                  transition: 'all 0.1s ease',
                }}
              >
                Dark
              </button>
            </div>
          </div>
        </motion.section>

        {/* Section 4: Cache (only when connected) */}
        {connected && cacheStats && (
          <motion.section
            className="rounded-lg p-5 space-y-4"
            style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}
            initial={{ opacity: 0, y: 8 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.15, delay: 0.09 }}
          >
            <h2 className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
              Cache
            </h2>

            {/* Stats grid */}
            <div className="grid grid-cols-2 gap-3">
              {([
                ['Hit Rate', `${((cacheStats.hit_rate ?? 0) * 100).toFixed(1)}%`],
                ['Hits', (cacheStats.hits ?? 0).toLocaleString()],
                ['Misses', (cacheStats.misses ?? 0).toLocaleString()],
                ['Pages Cached', (cacheStats.current_pages ?? 0).toLocaleString()],
              ] as [string, string][]).map(([label, value]) => (
                <div key={label} className="space-y-0.5">
                  <div className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
                    {label}
                  </div>
                  <div className="text-sm font-mono" style={{ color: 'var(--text)' }}>
                    {value}
                  </div>
                </div>
              ))}
            </div>

            <button
              onClick={handleClearCache}
              disabled={clearingCache}
              className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none disabled:opacity-40"
              style={{
                fontWeight: 400,
                background: 'transparent',
                color: 'var(--text-secondary)',
                border: '1px solid var(--border)',
                transition: 'all 0.1s ease',
              }}
              onMouseEnter={(e) => {
                e.currentTarget.style.background = 'var(--hover)';
                e.currentTarget.style.color = 'var(--text)';
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.background = 'transparent';
                e.currentTarget.style.color = 'var(--text-secondary)';
              }}
            >
              {clearingCache ? 'Clearing...' : 'Clear Cache'}
            </button>
          </motion.section>
        )}

        {/* Section 5: About */}
        <motion.section
          className="rounded-lg p-5 space-y-4"
          style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}
          initial={{ opacity: 0, y: 8 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.15, delay: 0.12 }}
        >
          <h2 className="text-[10px] uppercase" style={{ color: 'var(--text-muted)', letterSpacing: '0.08em', fontWeight: 400 }}>
            About
          </h2>

          <div className="space-y-2">
            <div className="flex items-center justify-between">
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>Version</span>
              <span className="text-xs font-mono" style={{ color: 'var(--text)' }}>
                {version ? version.version : 'N/A'}
              </span>
            </div>
            {version?.git_hash && (
              <div className="flex items-center justify-between">
                <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>Build</span>
                <span className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>
                  {version.git_hash.substring(0, 8)}
                  {version.build_type ? ` (${version.build_type})` : ''}
                </span>
              </div>
            )}
            {version?.platform && (
              <div className="flex items-center justify-between">
                <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>Platform</span>
                <span className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>
                  {version.platform}
                </span>
              </div>
            )}
            <div className="flex items-center justify-between">
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>Source</span>
              <a
                href="https://github.com"
                target="_blank"
                rel="noopener noreferrer"
                className="text-xs font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  textDecoration: 'underline',
                  textDecorationColor: 'var(--border)',
                  textUnderlineOffset: '2px',
                  transition: 'color 0.1s ease',
                }}
                onMouseEnter={(e) => { e.currentTarget.style.color = 'var(--text)'; }}
                onMouseLeave={(e) => { e.currentTarget.style.color = 'var(--text-secondary)'; }}
              >
                GitHub
              </a>
            </div>
          </div>
        </motion.section>
      </div>
    </div>
  );
}

export default Settings;
