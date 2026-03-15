import { useState, useEffect } from 'react';

export type LogLevel = 'TRC' | 'DBG' | 'INF' | 'WRN' | 'ERR';

export interface LogEntry {
  id: number;
  timestamp: number;
  level: LogLevel;
  module: string;
  message: string;
}

const MAX_ENTRIES = 500;
let nextId = 0;
const logBuffer: LogEntry[] = [];
const listeners: Set<() => void> = new Set();

function notify() {
  listeners.forEach((fn) => fn());
}

export function log(level: LogLevel, module: string, message: string) {
  const entry: LogEntry = {
    id: nextId++,
    timestamp: Date.now(),
    level,
    module,
    message,
  };
  logBuffer.push(entry);
  if (logBuffer.length > MAX_ENTRIES) {
    logBuffer.splice(0, logBuffer.length - MAX_ENTRIES);
  }
  notify();
}

export function clearLog() {
  logBuffer.length = 0;
  notify();
}

function getSnapshot(): LogEntry[] {
  return [...logBuffer];
}

function subscribe(callback: () => void): () => void {
  listeners.add(callback);
  return () => {
    listeners.delete(callback);
  };
}

export function useLogger() {
  const [entries, setEntries] = useState<LogEntry[]>(() => getSnapshot());

  useEffect(() => {
    const unsub = subscribe(() => {
      setEntries(getSnapshot());
    });
    return unsub;
  }, []);

  return { entries, log, clearLog };
}
