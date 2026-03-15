export interface ProcessInfo {
  pid: number;
  name: string;
  ppid: number;
  base_address: string; // hex string
  is_64bit: boolean;
  is_wow64: boolean;
}

export interface ModuleInfo {
  name: string;
  path: string;
  base_address: string;
  entry_point: string;
  size: number;
}

export interface MemoryRegion {
  base_address: string;
  size: number;
  protection: string;
  type: string;
  info: string;
}

export interface InstructionInfo {
  address: string;
  bytes: string;
  mnemonic: string;
  operands: string;
  full_text: string;
  category: string;
  is_call: boolean;
  is_jump: boolean;
  is_ret: boolean;
  branch_target?: string;
}

export interface PatternMatch {
  address: string;
  module_name?: string;
  context?: string;
}

export interface ScanResult {
  matches: PatternMatch[];
  scan_time_ms: number;
}

export interface TaskInfo {
  id: string;
  type: string;
  state: string;
  progress: number;
  status_message: string;
  result?: any;
  error?: string;
}

export interface HealthInfo {
  status: string;
  pid?: number;
  process_name?: string;
  device_type?: string;
  uptime_seconds?: number;
}

export interface VersionInfo {
  version: string;
  git_hash: string;
  build_type: string;
  platform: string;
}

export interface StringMatch {
  address: string;
  value: string;
  type: string; // "ASCII" | "UTF16_LE"
  raw_length: number;
}

export interface StringScanResult {
  matches: StringMatch[];
  scan_time_ms: number;
}

export interface Bookmark {
  address: string;
  label: string;
  notes: string;
  category: string;
  module: string;
  created_at: number;
}

export interface XrefResult {
  address: string;
  instruction: string;
  mnemonic: string;
  module_name?: string;
  module_offset?: string;
}

export interface CacheStats {
  enabled: boolean;
  hits: number;
  misses: number;
  evictions: number;
  hit_rate: number;
  pages_cached: number;
}
