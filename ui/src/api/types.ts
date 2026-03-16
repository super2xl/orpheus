export interface ProcessInfo {
  pid: number;
  name: string;
  base: string;        // hex string — server sends "base", not "base_address"
  is_64bit: boolean;
}

export interface ModuleInfo {
  name: string;
  path?: string;
  base: string;
  entry: string;
  size: number;
}

export interface MemoryRegion {
  base: string;
  size: number;
  protection: string;
  type: string;
  info: string;
}

export interface InstructionInfo {
  addr: string;
  bytes: string;
  text: string;      // full instruction text (mnemonic + operands)
  type?: string;      // "Call", "Jump", "ConditionalJump", "Return", etc.
  target?: string;    // branch/call target address
}

export interface ScanResult {
  addresses: string[];
  count: number;
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
  service?: string;
  version?: string;
  version_full?: string;
  git_hash?: string;
  build_date?: string;
  platform?: string;
  // Not sent by /health endpoint, but used by panels for attached process PID.
  // Will be populated when process attachment is implemented.
  pid?: number;
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
}

export interface StringScanResult {
  strings: StringMatch[];
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
  type: string;
  context: string;
}

export interface CacheStats {
  enabled: boolean;
  hits: number;
  misses: number;
  evictions: number;
  hit_rate: number;
  current_pages: number;
  max_pages?: number;
}

export interface CacheEntry {
  name: string;
  size: number;
  filepath: string;
  cached_at: string;
  item_count: number;
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
  status: string;
  count: number;
  summary: Record<string, unknown>;
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
  c_code: string;
  function_name: string;
}

export interface PointerChainStep {
  offset: number;
  address: string;     // address being read
  value: string;       // value at that address (next pointer or final)
  success: boolean;
}

export interface PointerChainResult {
  chain: PointerChainStep[];
  final_address: string;
  visualization: string;
  final_value?: string;
}

export interface VTableEntry {
  index: number;
  offset: number;
  address: string;
  status: string;
}

export interface VTableInfo {
  address: string;
  class_name?: string;
  base_classes?: string[];
  entries: VTableEntry[];
}

export interface CFGNodeLayout {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface CFGNode {
  address: string;
  end_address: string;
  size: number;
  instructions: InstructionInfo[];
  successors: string[];
  predecessors: string[];
  type: string; // Normal, Entry, Exit, Call, ConditionalJump, etc.
  layout: CFGNodeLayout;
  is_loop_header: boolean;
}

export interface CFGEdge {
  from: string;
  to: string;
  type: string; // FallThrough, Branch, Unconditional, Call
  is_back_edge: boolean;
}

// Server response shape (nodes as array)
export interface ControlFlowGraphResponse {
  nodes: CFGNode[];
  edges: CFGEdge[];
  has_loops: boolean;
  node_count: number;
  edge_count: number;
}

// Internal shape used by UI (nodes as Record for lookups)
export interface ControlFlowGraph {
  nodes: Record<string, CFGNode>;
  edges: CFGEdge[];
  has_loops: boolean;
  node_count: number;
  edge_count: number;
}
