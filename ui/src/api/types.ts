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

export interface RTTIClassInfo {
  vtable_address: string;
  mangled_name: string;
  demangled_name: string;
  base_classes: string[];
  method_count: number;
  is_multiple_inheritance: boolean;
  has_virtual_base: boolean;
}

export interface RTTIScanResult {
  classes: RTTIClassInfo[];
  scan_time_ms: number;
}

export interface FunctionInfo {
  entry_address: string;
  end_address: string;
  size: number;
  name: string;
  source: string; // "Prologue" | "CallTarget" | "ExceptionData" | "RTTI" | "Export"
  confidence: number; // 0.0 - 1.0
  instruction_count: number;
  basic_block_count: number;
  is_leaf: boolean;
  is_thunk: boolean;
}

export interface FunctionRecoveryResult {
  functions: FunctionInfo[];
  scan_time_ms: number;
  stats: Record<string, number>; // source -> count breakdown
}

export interface WriteInfo {
  instruction_address: string;
  mnemonic: string;
  operands: string;
  full_text: string;
  function_address: string;
  function_name: string;
}

export interface CallGraphNode {
  address: string;
  name: string;
  depth: number;
  type: string; // "DirectWriter" | "Caller"
  children: CallGraphNode[];
}

export interface EmulationResult {
  success: boolean;
  error?: string;
  instructions_executed: number;
  final_rip: string;
  registers: Record<string, string>;
}

export interface DecompileResult {
  code: string;
  function_name: string;
}

export interface CFGNode {
  address: string;
  end_address: string;
  size: number;
  instructions: InstructionInfo[];
  successors: string[];
  predecessors: string[];
  type: string; // Normal, Entry, Exit, Call, ConditionalJump, etc.
  x: number;
  y: number;
  width: number;
  height: number;
  is_loop_header: boolean;
}

export interface CFGEdge {
  from: string;
  to: string;
  type: string; // FallThrough, Branch, Unconditional, Call
  is_back_edge: boolean;
}

export interface ControlFlowGraph {
  function_address: string;
  function_name: string;
  nodes: Record<string, CFGNode>;
  edges: CFGEdge[];
  has_loops: boolean;
}
