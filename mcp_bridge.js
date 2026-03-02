#!/usr/bin/env node

/**
 * Orpheus MCP Bridge
 * stdio-to-HTTP adapter for Model Context Protocol (JSON-RPC 2.0)
 */

// Bridge version - increment when tools or functionality changes
const BRIDGE_VERSION = '1.3.0';

const http = require('http');
const https = require('https');
const { URL } = require('url');

// ============================================================================
// Configuration
// ============================================================================

const MCP_URL = process.env.ORPHEUS_MCP_URL || 'http://localhost:8765';
const API_KEY = process.env.ORPHEUS_API_KEY;
const DEBUG = process.env.DEBUG === 'true';

if (!API_KEY) {
  console.error('Error: ORPHEUS_API_KEY environment variable is required');
  console.error('Set it in your MCP server configuration or export it before running');
  process.exit(1);
}

// ============================================================================
// Request Queue (Serialization)
// ============================================================================

/**
 * Simple request queue to serialize all MCP operations.
 * Prevents flooding Orpheus server with concurrent requests.
 * Operations execute one at a time in FIFO order.
 */
class RequestQueue {
  constructor() {
    this.queue = [];
    this.processing = false;
  }

  /**
   * Add a request to the queue and wait for execution
   * @param {Function} operation - Async function to execute
   * @returns {Promise<any>} Result of the operation
   */
  async enqueue(operation) {
    return new Promise((resolve, reject) => {
      this.queue.push({ operation, resolve, reject });
      this.processNext();
    });
  }

  async processNext() {
    if (this.processing || this.queue.length === 0) return;

    this.processing = true;
    const { operation, resolve, reject } = this.queue.shift();

    try {
      const result = await operation();
      resolve(result);
    } catch (error) {
      reject(error);
    } finally {
      this.processing = false;
      // Process next item in queue
      if (this.queue.length > 0) {
        setImmediate(() => this.processNext());
      }
    }
  }

  /**
   * Get current queue depth (for debugging)
   */
  get depth() {
    return this.queue.length + (this.processing ? 1 : 0);
  }
}

// Global request queue - all MCP operations go through this
const requestQueue = new RequestQueue();

// ============================================================================
// Logging
// ============================================================================

function log(level, message, data = null) {
  if (!DEBUG && level === 'DEBUG') return;

  const timestamp = new Date().toISOString();
  const logEntry = {
    timestamp,
    level,
    message,
    ...(data && { data })
  };

  // Write to stderr so it doesn't interfere with JSON-RPC on stdout
  console.error(JSON.stringify(logEntry));
}

// ============================================================================
// HTTP Client
// ============================================================================

/**
 * Make HTTP request to Orpheus MCP server
 * @param {string} method - HTTP method (GET, POST)
 * @param {string} path - Endpoint path
 * @param {object|null} body - Request body (for POST)
 * @returns {Promise<object>} Response data
 */
function makeHttpRequest(method, path, body = null) {
  return new Promise((resolve, reject) => {
    const url = new URL(path, MCP_URL);
    const client = url.protocol === 'https:' ? https : http;

    const options = {
      hostname: url.hostname,
      port: url.port || (url.protocol === 'https:' ? 443 : 80),
      path: url.pathname + url.search,
      method: method,
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${API_KEY}`,
        'User-Agent': 'Orpheus-MCP-Bridge/1.0.0'
      },
      timeout: 30000 // 30 second timeout
    };

    const req = client.request(options, (res) => {
      let data = '';

      res.on('data', (chunk) => {
        data += chunk;
      });

      res.on('end', () => {
        try {
          const parsed = JSON.parse(data);

          // Handle Orpheus response format: { success: bool, data: {}, error: string }
          if (res.statusCode >= 200 && res.statusCode < 300) {
            if (parsed.success === false) {
              reject(new Error(parsed.error || 'Request failed'));
            } else {
              resolve(parsed.data || parsed);
            }
          } else {
            reject(new Error(parsed.error || `HTTP ${res.statusCode}: ${res.statusMessage}`));
          }
        } catch (e) {
          reject(new Error(`Invalid JSON response: ${data.substring(0, 200)}`));
        }
      });
    });

    req.on('error', (err) => {
      reject(new Error(`Network error: ${err.message}`));
    });

    req.on('timeout', () => {
      req.destroy();
      reject(new Error('Request timeout'));
    });

    if (method === 'POST' && body) {
      req.write(JSON.stringify(body));
    }

    req.end();
  });
}

// ============================================================================
// MCP Tool Definitions
// ============================================================================

const MCP_TOOLS = [
  {
    name: 'read_memory',
    description: 'Read memory from a process via DMA. Returns structured data including hex string, byte array, hexdump, and type interpretations (int32, int64, float, ptr). Use format parameter to control output.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID to read from'
        },
        address: {
          type: 'string',
          description: 'Memory address in hexadecimal (e.g., "0x7FF600000000" or "7FF600000000")'
        },
        size: {
          type: 'integer',
          description: 'Number of bytes to read (max 16MB for safety)',
          minimum: 1,
          maximum: 16777216
        },
        format: {
          type: 'string',
          description: 'Output format: "auto" (all formats), "hex" (compact hex only), "bytes" (byte array), "hexdump" (IDA-style)',
          default: 'auto'
        }
      },
      required: ['pid', 'address', 'size']
    }
  },
  {
    name: 'write_memory',
    description: 'Write data to process memory via DMA. Takes a hex string of bytes to write at the specified address. Returns the address and number of bytes written. Requires allow_write to be enabled in config.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID to write to'
        },
        address: {
          type: 'string',
          description: 'Memory address in hexadecimal (e.g., "0x7FF600000000" or "7FF600000000")'
        },
        data: {
          type: 'string',
          description: 'Hex string of bytes to write (e.g., "90 90 90" or "909090"). Spaces optional.'
        }
      },
      required: ['pid', 'address', 'data']
    }
  },
  {
    name: 'scan_pattern',
    description: 'Scan for byte pattern in process memory using IDA-style pattern syntax. Returns array of matching addresses.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID to scan'
        },
        base: {
          type: 'string',
          description: 'Base address to start scanning from (hex)'
        },
        size: {
          type: 'integer',
          description: 'Size of memory region to scan in bytes'
        },
        pattern: {
          type: 'string',
          description: 'IDA-style pattern (e.g., "48 8B ?? 74 ?? ?? ?? ??") where ?? = wildcard'
        }
      },
      required: ['pid', 'base', 'size', 'pattern']
    }
  },
  {
    name: 'scan_pattern_async',
    description: 'Async version of scan_pattern. Returns a task_id immediately. Use task_status to check progress and get results. Supports cancellation via task_cancel.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID to scan'
        },
        base: {
          type: 'string',
          description: 'Base address to start scanning from (hex)'
        },
        size: {
          type: 'integer',
          description: 'Size of memory region to scan in bytes'
        },
        pattern: {
          type: 'string',
          description: 'IDA-style pattern (e.g., "48 8B ?? 74 ?? ?? ?? ??") where ?? = wildcard'
        }
      },
      required: ['pid', 'base', 'size', 'pattern']
    }
  },
  {
    name: 'scan_strings',
    description: 'Scan for ASCII/Unicode strings in process memory. Returns array of found strings with addresses.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID to scan'
        },
        base: {
          type: 'string',
          description: 'Base address to start scanning from (hex)'
        },
        size: {
          type: 'integer',
          description: 'Size of memory region to scan in bytes'
        },
        min_length: {
          type: 'integer',
          description: 'Minimum string length to match',
          default: 4,
          minimum: 1
        },
        contains: {
          type: 'string',
          description: 'Filter: only return strings containing this substring (case-insensitive). E.g., "Resource", "Error", "Player"'
        },
        max_results: {
          type: 'integer',
          description: 'Maximum number of strings to return. Default: 1000',
          default: 1000
        }
      },
      required: ['pid', 'base', 'size']
    }
  },
  {
    name: 'scan_strings_async',
    description: 'Async version of scan_strings. Returns a task_id immediately. Use task_status to check progress and get results. Supports cancellation via task_cancel.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID to scan'
        },
        base: {
          type: 'string',
          description: 'Base address to start scanning from (hex)'
        },
        size: {
          type: 'integer',
          description: 'Size of memory region to scan in bytes'
        },
        min_length: {
          type: 'integer',
          description: 'Minimum string length to match',
          default: 4,
          minimum: 1
        },
        contains: {
          type: 'string',
          description: 'Filter: only return strings containing this substring (case-insensitive). E.g., "Resource", "Error", "Player"'
        },
        max_results: {
          type: 'integer',
          description: 'Maximum number of strings to return. Default: 1000',
          default: 1000
        }
      },
      required: ['pid', 'base', 'size']
    }
  },
  {
    name: 'disassemble',
    description: 'Disassemble x64 assembly code at specified address. Returns human-readable assembly instructions.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        address: {
          type: 'string',
          description: 'Address to disassemble from (hex)'
        },
        count: {
          type: 'integer',
          description: 'Number of instructions to disassemble',
          default: 20,
          minimum: 1,
          maximum: 1000
        }
      },
      required: ['pid', 'address']
    }
  },
  {
    name: 'decompile',
    description: 'Decompile a function at the specified address using the Ghidra decompiler. Returns C-like pseudocode. Best used on function entry points. For CS2: use inject_schema=true after cs2_init to enable automatic field naming.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        address: {
          type: 'string',
          description: 'Address of the function to decompile (hex). Should be the function entry point.'
        },
        function_name: {
          type: 'string',
          description: 'Optional name for the function (default: auto-generated)'
        },
        inject_schema: {
          type: 'boolean',
          description: 'Inject CS2 schema types into the decompiler for automatic field naming. Requires cs2_init to be called first. Default: false',
          default: false
        },
        this_type: {
          type: 'string',
          description: 'Class name for the "this" pointer type (e.g., "CCSPlayerController"). When specified, sets the first parameter type to enable field name resolution. Requires inject_schema=true.'
        },
        max_instructions: {
          type: 'integer',
          description: 'Maximum instructions for flow analysis (default: 100000). Increase for large functions that fail with "Flow exceeded maximum allowable instructions", or decrease to fail faster on huge functions.',
          minimum: 1000,
          maximum: 10000000
        }
      },
      required: ['pid', 'address']
    }
  },
  {
    name: 'generate_signature',
    description: 'Generate an IDA-style byte signature from code at the specified address. Automatically wildcards relocatable bytes (RIP-relative offsets, call targets, large immediates). Returns pattern in IDA and Cheat Engine formats with quality metrics.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        address: {
          type: 'string',
          description: 'Address to generate signature from (hex)'
        },
        size: {
          type: 'integer',
          description: 'Number of bytes to include in signature. Default: 64',
          default: 64
        },
        instruction_count: {
          type: 'integer',
          description: 'Alternative to size: number of instructions to include. If set, overrides size.'
        },
        wildcard_rip_relative: {
          type: 'boolean',
          description: 'Wildcard RIP-relative memory offsets. Default: true',
          default: true
        },
        wildcard_calls: {
          type: 'boolean',
          description: 'Wildcard CALL rel32 offsets. Default: true',
          default: true
        },
        wildcard_jumps: {
          type: 'boolean',
          description: 'Wildcard JMP rel32 offsets. Default: true',
          default: true
        },
        wildcard_large_immediates: {
          type: 'boolean',
          description: 'Wildcard 4+ byte immediate values. Default: true',
          default: true
        },
        min_unique_bytes: {
          type: 'integer',
          description: 'Minimum non-wildcarded bytes for a valid signature. Default: 8',
          default: 8
        },
        max_length: {
          type: 'integer',
          description: 'Maximum signature length in bytes. Default: 64',
          default: 64
        }
      },
      required: ['pid', 'address']
    }
  },
  {
    name: 'memory_snapshot',
    description: 'Take a snapshot of a memory region for later comparison. Snapshots are stored in memory and can be compared to find changed values.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        address: {
          type: 'string',
          description: 'Base address of memory region (hex)'
        },
        size: {
          type: 'integer',
          description: 'Size of region to snapshot in bytes (max 16MB)'
        },
        name: {
          type: 'string',
          description: 'Optional name for the snapshot. Auto-generated if not provided.'
        }
      },
      required: ['pid', 'address', 'size']
    }
  },
  {
    name: 'memory_snapshot_list',
    description: 'List all stored memory snapshots with their addresses, sizes, and timestamps.',
    inputSchema: {
      type: 'object',
      properties: {}
    }
  },
  {
    name: 'memory_snapshot_delete',
    description: 'Delete a stored memory snapshot by name.',
    inputSchema: {
      type: 'object',
      properties: {
        name: {
          type: 'string',
          description: 'Name of the snapshot to delete'
        }
      },
      required: ['name']
    }
  },
  {
    name: 'memory_diff',
    description: 'Compare memory snapshots to find changed values. Can compare two snapshots or a snapshot against current memory. Useful for finding dynamic values like health, ammo, position, etc.',
    inputSchema: {
      type: 'object',
      properties: {
        mode: {
          type: 'string',
          enum: ['snapshot_vs_current', 'snapshot_vs_snapshot'],
          description: 'Comparison mode. Default: snapshot_vs_current',
          default: 'snapshot_vs_current'
        },
        snapshot: {
          type: 'string',
          description: 'Snapshot name (for snapshot_vs_current mode)'
        },
        snapshot_a: {
          type: 'string',
          description: 'First snapshot name (for snapshot_vs_snapshot mode)'
        },
        snapshot_b: {
          type: 'string',
          description: 'Second snapshot name (for snapshot_vs_snapshot mode)'
        },
        pid: {
          type: 'integer',
          description: 'Process ID (optional, uses snapshot PID if not provided)'
        },
        filter: {
          type: 'string',
          enum: ['all', 'changed', 'increased', 'decreased', 'unchanged'],
          description: 'Filter results by change type. Default: all',
          default: 'all'
        },
        value_size: {
          type: 'integer',
          enum: [1, 2, 4, 8],
          description: 'Size of values to compare in bytes. Default: 4',
          default: 4
        },
        max_results: {
          type: 'integer',
          description: 'Maximum number of results to return. Default: 1000',
          default: 1000
        }
      },
      required: []
    }
  },
  {
    name: 'get_health',
    description: 'Get Orpheus server health status and basic version info. Use this to verify the server is running and get version details.',
    inputSchema: {
      type: 'object',
      properties: {}
    }
  },
  {
    name: 'get_version',
    description: 'Get detailed Orpheus version information including git hash, branch, build date, platform, and compiler info.',
    inputSchema: {
      type: 'object',
      properties: {}
    }
  },
  {
    name: 'get_processes',
    description: 'List all running processes visible to DMA. Returns array of process info with PID, name, and base address.',
    inputSchema: {
      type: 'object',
      properties: {}
    }
  },
  {
    name: 'get_modules',
    description: 'Get loaded modules (DLLs) for a specific process. Returns array of module info with base address, size, and path.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID to query'
        }
      },
      required: ['pid']
    }
  },
  {
    name: 'resolve_pointer',
    description: 'Follow a pointer chain in process memory. Given a base address and array of offsets, dereferences each pointer and applies offset. Returns the final address and optionally reads the value there. Example: base=0x1000, offsets=[0x10, 0x20] follows [[0x1000]+0x10]+0x20.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        base: {
          type: 'string',
          description: 'Starting address (hex)'
        },
        offsets: {
          type: 'array',
          items: { type: ['integer', 'string'] },
          description: 'Array of offsets to apply after each dereference. Can be integers or hex strings.'
        },
        read_final: {
          type: 'boolean',
          description: 'If true, read value at final address',
          default: false
        },
        read_size: {
          type: 'integer',
          description: 'Bytes to read at final address (if read_final=true)',
          default: 8
        }
      },
      required: ['pid', 'base', 'offsets']
    }
  },
  {
    name: 'find_xrefs',
    description: 'Find cross-references to a target address within a memory region. Scans for both direct 64-bit pointers and 32-bit RIP-relative offsets.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        target: {
          type: 'string',
          description: 'Target address to find references to (hex)'
        },
        base: {
          type: 'string',
          description: 'Base address of region to scan (hex)'
        },
        size: {
          type: 'integer',
          description: 'Size of region to scan in bytes'
        },
        max_results: {
          type: 'integer',
          description: 'Maximum number of results to return',
          default: 100
        }
      },
      required: ['pid', 'target', 'base', 'size']
    }
  },
  {
    name: 'memory_regions',
    description: 'Get memory regions (VADs) for a process. Returns base address, size, protection, and type for each region.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        }
      },
      required: ['pid']
    }
  },
  {
    name: 'dump_module',
    description: 'Dump a module from process memory to disk. Reads the entire module and saves to file.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        module: {
          type: 'string',
          description: 'Module name (e.g., "client.dll")'
        },
        output: {
          type: 'string',
          description: 'Output file path (defaults to module name + .dump)'
        }
      },
      required: ['pid', 'module']
    }
  },
  // ============================================================================
  // Emulation Tools
  // ============================================================================
  {
    name: 'emu_create',
    description: 'Create a Unicorn x64 emulator instance for a process. Memory is lazily loaded from the target via DMA as the emulator executes. Call this before using other emu_ tools.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID to emulate'
        },
        stack_base: {
          type: 'string',
          description: 'Stack base address (hex), default: 0x80000000'
        },
        stack_size: {
          type: 'integer',
          description: 'Stack size in bytes, default: 2MB'
        },
        max_instructions: {
          type: 'integer',
          description: 'Maximum instructions to execute before stopping, default: 100000'
        },
        timeout_us: {
          type: 'integer',
          description: 'Timeout in microseconds, default: 5000000 (5 seconds)'
        },
        lazy_mapping: {
          type: 'boolean',
          description: 'Map pages on-demand from DMA (recommended), default: true'
        }
      },
      required: ['pid']
    }
  },
  {
    name: 'emu_destroy',
    description: 'Destroy the active emulator instance and free resources.',
    inputSchema: {
      type: 'object',
      properties: {}
    }
  },
  {
    name: 'emu_map_module',
    description: 'Pre-map an entire module into the emulator. Use this if you know which module the code will access heavily.',
    inputSchema: {
      type: 'object',
      properties: {
        module: {
          type: 'string',
          description: 'Module name to map (e.g., "game.exe", "client.dll")'
        }
      },
      required: ['module']
    }
  },
  {
    name: 'emu_map_region',
    description: 'Pre-map a memory region into the emulator.',
    inputSchema: {
      type: 'object',
      properties: {
        address: {
          type: 'string',
          description: 'Base address (hex)'
        },
        size: {
          type: 'integer',
          description: 'Size in bytes'
        }
      },
      required: ['address', 'size']
    }
  },
  {
    name: 'emu_set_registers',
    description: 'Set CPU register values before running emulation. Supports all x64 GP registers (rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8-r15, rip, rflags).',
    inputSchema: {
      type: 'object',
      properties: {
        registers: {
          type: 'object',
          description: 'Object mapping register names to values. Values can be integers or hex strings.',
          additionalProperties: {
            oneOf: [
              { type: 'integer' },
              { type: 'string' }
            ]
          }
        }
      },
      required: ['registers']
    }
  },
  {
    name: 'emu_get_registers',
    description: 'Get current CPU register values from the emulator.',
    inputSchema: {
      type: 'object',
      properties: {
        registers: {
          type: 'array',
          items: { type: 'string' },
          description: 'Specific registers to read (optional - omit for all GP registers)'
        }
      }
    }
  },
  {
    name: 'emu_run',
    description: 'Run emulation from start_address until end_address is reached. Perfect for running decryption stubs. Returns final register state.',
    inputSchema: {
      type: 'object',
      properties: {
        start_address: {
          type: 'string',
          description: 'Address to begin execution (hex)'
        },
        end_address: {
          type: 'string',
          description: 'Address to stop execution at (hex) - execution stops when RIP reaches this'
        }
      },
      required: ['start_address', 'end_address']
    }
  },
  {
    name: 'emu_run_instructions',
    description: 'Run emulation for a specific number of instructions. Useful for stepping through code.',
    inputSchema: {
      type: 'object',
      properties: {
        start_address: {
          type: 'string',
          description: 'Address to begin execution (hex)'
        },
        count: {
          type: 'integer',
          description: 'Number of instructions to execute'
        }
      },
      required: ['start_address', 'count']
    }
  },
  {
    name: 'emu_reset',
    description: 'Reset the emulator state. By default resets only CPU (registers), set full=true to also clear memory mappings.',
    inputSchema: {
      type: 'object',
      properties: {
        full: {
          type: 'boolean',
          description: 'If true, reset both CPU and memory mappings. If false (default), reset only CPU registers.',
          default: false
        }
      }
    }
  },
  // ============================================================================
  // RTTI Analysis Tools
  // ============================================================================
  {
    name: 'rtti_parse_vtable',
    description: 'Parse MSVC RTTI from a vtable address. Returns class info including demangled name, method count, inheritance flags (M=multiple, V=virtual), and base class hierarchy in ClassInformer-style format.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        vtable_address: {
          type: 'string',
          description: 'Address of the vtable (hex). This is the address of the first function pointer, NOT vtable[-1].'
        },
        module_base: {
          type: 'string',
          description: 'Base address of the module containing the vtable (hex). Auto-detected if not provided.'
        }
      },
      required: ['pid', 'vtable_address']
    }
  },
  {
    name: 'rtti_scan',
    description: 'Scan a memory region for vtables with valid MSVC RTTI. Returns list of discovered classes with vtable addresses, method counts, flags (M/V), and inheritance hierarchy.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        base: {
          type: 'string',
          description: 'Base address of region to scan (hex)'
        },
        size: {
          type: 'integer',
          description: 'Size of region to scan in bytes (max 256MB)'
        },
        max_results: {
          type: 'integer',
          description: 'Maximum number of vtables to return',
          default: 100
        }
      },
      required: ['pid', 'base', 'size']
    }
  },
  {
    name: 'rtti_scan_module',
    description: 'Scan an entire PE module for RTTI by automatically parsing PE sections and scanning .rdata/.data sections. Auto-caches results for fast subsequent queries. Returns summary only (use rtti_cache_query to search classes).',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        module_base: {
          type: 'string',
          description: 'Base address of the PE module to scan (hex)'
        },
        force_rescan: {
          type: 'boolean',
          description: 'Force re-scan even if cache exists',
          default: false
        }
      },
      required: ['pid', 'module_base']
    }
  },
  // RTTI Cache tools
  {
    name: 'rtti_cache_list',
    description: 'List all cached RTTI scan results. Returns module names, sizes, class counts, and cache file paths.',
    inputSchema: {
      type: 'object',
      properties: {}
    }
  },
  {
    name: 'rtti_cache_query',
    description: 'Search cached RTTI results by class name. Searches across all cached modules or a specific one. Returns matching classes with vtable, methods, flags, and hierarchy.',
    inputSchema: {
      type: 'object',
      properties: {
        query: {
          type: 'string',
          description: 'Class name to search for (case-insensitive, partial match)'
        },
        module: {
          type: 'string',
          description: 'Optional module name to search in (e.g., "client.dll"). Searches all if not specified.'
        },
        pid: {
          type: 'integer',
          description: 'Optional Process ID to resolve RVAs to absolute addresses for current session'
        },
        max_results: {
          type: 'integer',
          description: 'Maximum number of results to return',
          default: 100
        }
      },
      required: ['query']
    }
  },
  {
    name: 'rtti_cache_get',
    description: 'Get full cached RTTI data for a specific module. Returns all classes with their vtables, methods, flags, and hierarchies.',
    inputSchema: {
      type: 'object',
      properties: {
        module: {
          type: 'string',
          description: 'Module name (e.g., "client.dll")'
        },
        pid: {
          type: 'integer',
          description: 'Optional Process ID to resolve RVAs to absolute addresses for current session'
        },
        max_results: {
          type: 'integer',
          description: 'Maximum number of classes to return',
          default: 1000
        }
      },
      required: ['module']
    }
  },
  {
    name: 'rtti_cache_clear',
    description: 'Clear cached RTTI data. Can clear a specific module or all cached data.',
    inputSchema: {
      type: 'object',
      properties: {
        module: {
          type: 'string',
          description: 'Module name to clear (e.g., "client.dll"). Clears all if not specified.'
        }
      }
    }
  },
  {
    name: 'read_vtable',
    description: 'Read vtable entries from a given address. Returns function pointers with context (module+offset), validity status, and optionally disassembles first instructions of each function. Also attempts to resolve class name via RTTI.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        vtable_address: {
          type: 'string',
          description: 'Address of the vtable (hex). This is the address of the first function pointer.'
        },
        count: {
          type: 'integer',
          description: 'Number of entries to read. Default: 20, max: 500',
          default: 20,
          minimum: 1,
          maximum: 500
        },
        disassemble: {
          type: 'boolean',
          description: 'If true, disassemble first few instructions of each valid function. Default: false',
          default: false
        },
        disasm_instructions: {
          type: 'integer',
          description: 'Number of instructions to disassemble per entry (if disassemble=true). Default: 5',
          default: 5,
          minimum: 1,
          maximum: 20
        }
      },
      required: ['pid', 'vtable_address']
    }
  },
  // ============================================================================
  // Bookmark Tools
  // ============================================================================
  {
    name: 'bookmark_list',
    description: 'List all saved bookmarks. Returns addresses, labels, notes, categories, and modules.',
    inputSchema: {
      type: 'object',
      properties: {}
    }
  },
  {
    name: 'bookmark_add',
    description: 'Add a new bookmark for an address. Bookmarks persist across sessions and are shared with the UI.',
    inputSchema: {
      type: 'object',
      properties: {
        address: {
          type: 'string',
          description: 'Memory address to bookmark (hex, e.g., "0x7FF600001000")'
        },
        label: {
          type: 'string',
          description: 'Short label for the bookmark (e.g., "PlayerHealth")'
        },
        notes: {
          type: 'string',
          description: 'Optional detailed notes about this address'
        },
        category: {
          type: 'string',
          description: 'Optional category for grouping (e.g., "Player", "Entity", "Weapons")'
        },
        module: {
          type: 'string',
          description: 'Optional module name for context (e.g., "client.dll")'
        }
      },
      required: ['address']
    }
  },
  {
    name: 'bookmark_remove',
    description: 'Remove a bookmark by index or address.',
    inputSchema: {
      type: 'object',
      properties: {
        index: {
          type: 'integer',
          description: 'Index of the bookmark to remove (from bookmark_list)'
        },
        address: {
          type: 'string',
          description: 'Address of the bookmark to remove (hex). Alternative to index.'
        }
      }
    }
  },
  {
    name: 'bookmark_update',
    description: 'Update an existing bookmark. Only provided fields will be changed.',
    inputSchema: {
      type: 'object',
      properties: {
        index: {
          type: 'integer',
          description: 'Index of the bookmark to update (required)'
        },
        address: {
          type: 'string',
          description: 'New address (hex)'
        },
        label: {
          type: 'string',
          description: 'New label'
        },
        notes: {
          type: 'string',
          description: 'New notes'
        },
        category: {
          type: 'string',
          description: 'New category'
        },
        module: {
          type: 'string',
          description: 'New module name'
        }
      },
      required: ['index']
    }
  },
  // ============================================================================
  // CS2 Consolidated Init (One-Shot)
  // ============================================================================
  {
    name: 'cs2_init',
    description: 'One-shot initialization for CS2 analysis. Combines schema_init, schema_dump, rtti_scan, and entity_init in a single call. Returns complete system state including schema info, RTTI class count, entity system addresses, and local player data. Use this instead of calling individual cs2_schema_* and cs2_entity_* tools separately.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID of Counter-Strike 2'
        },
        force_refresh: {
          type: 'boolean',
          description: 'Force re-dump of schema and RTTI even if cached. Default: false',
          default: false
        }
      },
      required: ['pid']
    }
  },
  // ============================================================================
  // CS2 Schema Query Tools (use cs2_init for initialization)
  // ============================================================================
  {
    name: 'cs2_schema_get_offset',
    description: 'Get the offset for a specific class field. Searches live dumper first, then cache.',
    inputSchema: {
      type: 'object',
      properties: {
        class_name: {
          type: 'string',
          description: 'Class name (e.g., "C_BaseEntity", "CCSPlayerController")'
        },
        field_name: {
          type: 'string',
          description: 'Field name (e.g., "m_iHealth", "m_hPawn")'
        }
      },
      required: ['class_name', 'field_name']
    }
  },
  {
    name: 'cs2_schema_find_class',
    description: 'Find a class by name and return all its fields with offsets. Case-insensitive exact match.',
    inputSchema: {
      type: 'object',
      properties: {
        class_name: {
          type: 'string',
          description: 'Class name to find (e.g., "C_BaseEntity")'
        }
      },
      required: ['class_name']
    }
  },
  {
    name: 'cs2_schema_cache_list',
    description: 'List all cached CS2 schema dumps. Returns scope names, class counts, and cache timestamps.',
    inputSchema: {
      type: 'object',
      properties: {}
    }
  },
  {
    name: 'cs2_schema_cache_query',
    description: 'Search cached CS2 schemas by class name. Case-insensitive partial match across all cached scopes.',
    inputSchema: {
      type: 'object',
      properties: {
        query: {
          type: 'string',
          description: 'Class name to search for (partial match, case-insensitive)'
        },
        scope: {
          type: 'string',
          description: 'Optional scope to search in (e.g., "client.dll"). Searches all if not specified.'
        },
        max_results: {
          type: 'integer',
          description: 'Maximum number of results to return',
          default: 100
        }
      },
      required: ['query']
    }
  },
  {
    name: 'cs2_schema_cache_get',
    description: 'Get full cached schema data for a specific scope. Returns all classes with their fields.',
    inputSchema: {
      type: 'object',
      properties: {
        scope: {
          type: 'string',
          description: 'Scope name (e.g., "client.dll", "all")'
        },
        max_results: {
          type: 'integer',
          description: 'Maximum number of classes to return',
          default: 1000
        }
      },
      required: ['scope']
    }
  },
  {
    name: 'cs2_schema_cache_clear',
    description: 'Clear cached CS2 schema data. Can clear a specific scope or all cached data.',
    inputSchema: {
      type: 'object',
      properties: {
        scope: {
          type: 'string',
          description: 'Scope name to clear (e.g., "client.dll"). Clears all if not specified.'
        }
      }
    }
  },
  // ============================================================================
  // CS2 Entity Tools (RTTI + Schema Bridge) - use cs2_init for initialization
  // ============================================================================
  {
    name: 'cs2_identify',
    description: 'Identify the class of an object at a given address using RTTI. Reads the vtable pointer and parses RTTI to get the class name, then matches it to the schema. Returns RTTI class name, schema class name, and field count.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID of Counter-Strike 2'
        },
        address: {
          type: 'string',
          description: 'Address of the object to identify (hex). The vtable pointer is read from this address.'
        },
        module_base: {
          type: 'string',
          description: 'Optional module base for RTTI resolution (hex). Auto-detected if not provided.'
        }
      },
      required: ['pid', 'address']
    }
  },
  {
    name: 'cs2_read_field',
    description: 'Read a field from an object by class and field name. Uses RTTI to verify the class (optional) and schema to get the offset and type. Automatically interprets the value based on the field type (int, float, bool, vector, handle, etc.).',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID of Counter-Strike 2'
        },
        address: {
          type: 'string',
          description: 'Address of the object to read from (hex)'
        },
        class: {
          type: 'string',
          description: 'Class name to look up the field in (e.g., "CCSPlayerController", "C_BaseEntity"). Optional - auto-detected via RTTI if not provided.'
        },
        field: {
          type: 'string',
          description: 'Field name to read (e.g., "m_iHealth", "m_hPawn")'
        },
        verify_rtti: {
          type: 'boolean',
          description: 'If true, verify the object class matches via RTTI before reading. Default: false',
          default: false
        }
      },
      required: ['pid', 'address', 'field']
    }
  },
  {
    name: 'cs2_inspect',
    description: 'Inspect an object by dumping all its fields with live values. Uses RTTI to identify the class (or takes explicit class name), then reads all schema fields up to max_fields. Returns field names, types, offsets, and interpreted values.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID of Counter-Strike 2'
        },
        address: {
          type: 'string',
          description: 'Address of the object to inspect (hex)'
        },
        class: {
          type: 'string',
          description: 'Optional class name override. If not provided, uses RTTI to identify the class.'
        },
        max_fields: {
          type: 'integer',
          description: 'Maximum number of fields to read. Default: 50',
          default: 50
        }
      },
      required: ['pid', 'address']
    }
  },
  {
    name: 'cs2_get_local_player',
    description: 'Get the local player controller. Requires cs2_init to be called first. Returns the controller address, identified class, and key fields (pawn handle, health, armor, alive status).',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID of Counter-Strike 2'
        }
      },
      required: ['pid']
    }
  },
  {
    name: 'cs2_get_entity',
    description: 'Get an entity by handle or index. Resolves the entity from the entity list and identifies its class via RTTI. Returns the entity address, class name, and basic info.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID of Counter-Strike 2'
        },
        handle: {
          type: 'integer',
          description: 'Entity handle (e.g., from m_hPawn). Extracts index automatically.'
        },
        index: {
          type: 'integer',
          description: 'Direct entity index (alternative to handle). Use either handle or index.'
        }
      },
      required: ['pid']
    }
  },
  {
    name: 'cs2_list_players',
    description: 'List all connected players in CS2 with their controller info. Returns player name, team, health, alive status, entity index, and optionally position and spotted state. Requires cs2_init to be called first.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID of Counter-Strike 2'
        },
        include_position: {
          type: 'boolean',
          description: 'Include player world position (from pawn). Default: false',
          default: false
        },
        include_spotted: {
          type: 'boolean',
          description: 'Include spotted state info (who can see this player on radar). Default: false',
          default: false
        }
      },
      required: ['pid']
    }
  },
  {
    name: 'cs2_get_game_state',
    description: 'Get the current CS2 game state. Detects if player is in menu, loading, match lobby, or in-game. Returns state, local player info if in game, and player count. Requires cs2_init to be called first.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID of Counter-Strike 2'
        }
      },
      required: ['pid']
    }
  },
  // ============================================================================
  // Function Recovery Tools
  // ============================================================================
  {
    name: 'recover_functions',
    description: 'Recover functions from a module using prologue detection, call target following, and exception data (.pdata). Returns function count and summary. Results are cached for fast subsequent queries.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        module_base: {
          type: 'string',
          description: 'Base address of the module to analyze (hex)'
        },
        force_rescan: {
          type: 'boolean',
          description: 'Force re-scan even if cache exists. Default: false',
          default: false
        },
        use_prologues: {
          type: 'boolean',
          description: 'Scan for function prologues. Default: true',
          default: true
        },
        follow_calls: {
          type: 'boolean',
          description: 'Mark CALL targets as functions. Default: true',
          default: true
        },
        use_exception_data: {
          type: 'boolean',
          description: 'Parse .pdata exception directory (x64 PE). Default: true',
          default: true
        },
        max_functions: {
          type: 'integer',
          description: 'Maximum number of functions to recover. Default: 100000',
          default: 100000
        }
      },
      required: ['pid', 'module_base']
    }
  },
  {
    name: 'get_function_at',
    description: 'Get function information at an exact address. Requires recover_functions to be called first for the module.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        address: {
          type: 'string',
          description: 'Address to look up (hex). Must be the exact function entry point.'
        }
      },
      required: ['pid', 'address']
    }
  },
  {
    name: 'get_function_containing',
    description: 'Get the function that contains a given address. Useful for finding what function an instruction belongs to. Requires recover_functions to be called first.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        address: {
          type: 'string',
          description: 'Address to look up (hex). Returns the function containing this address.'
        }
      },
      required: ['pid', 'address']
    }
  },
  {
    name: 'find_function_bounds',
    description: 'Find function start and end addresses given any address within the function. Uses heuristics (prologue detection, int3 padding, ret instructions) without requiring prior function recovery. Returns start/end addresses with confidence level.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        address: {
          type: 'string',
          description: 'Address within the function (hex)'
        },
        max_search_up: {
          type: 'integer',
          description: 'Maximum bytes to search backwards for function start. Default: 4096',
          default: 4096
        },
        max_search_down: {
          type: 'integer',
          description: 'Maximum bytes to search forwards for function end. Default: 8192',
          default: 8192
        }
      },
      required: ['pid', 'address']
    }
  },
  // CFG Analysis tools
  {
    name: 'build_cfg',
    description: 'Build a Control Flow Graph for a function. Returns nodes (basic blocks), edges, loop detection, and layout information for visualization. Use this to understand function structure and control flow.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        address: {
          type: 'string',
          description: 'Function entry point address (hex)'
        },
        max_size: {
          type: 'integer',
          description: 'Maximum bytes to analyze (default: 64KB, max: 1MB)'
        },
        compute_layout: {
          type: 'boolean',
          description: 'Compute x/y layout positions for visualization (default: true)'
        }
      },
      required: ['pid', 'address']
    }
  },
  {
    name: 'get_cfg_node',
    description: 'Get detailed information about a specific CFG node (basic block) including full disassembly and edge details.',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID'
        },
        function_address: {
          type: 'string',
          description: 'Function entry point address (hex)'
        },
        node_address: {
          type: 'string',
          description: 'Address of the basic block to get details for (hex)'
        }
      },
      required: ['pid', 'function_address', 'node_address']
    }
  },
  // Expression evaluation
  {
    name: 'evaluate_expression',
    description: 'Evaluate an address expression. Supports module names (client.dll), offsets (+0x1000), dereference ([addr]), arithmetic (+, -, *, /), and nested expressions. Examples: "client.dll+0x1234", "[client.dll+0x10]", "[[base]+0x20]+0x100".',
    inputSchema: {
      type: 'object',
      properties: {
        pid: {
          type: 'integer',
          description: 'Process ID (required for module resolution and memory reads)'
        },
        expression: {
          type: 'string',
          description: 'Address expression to evaluate. Can use module names, hex offsets, dereference brackets, and arithmetic.'
        }
      },
      required: ['pid', 'expression']
    }
  },
  // Task management tools
  {
    name: 'task_status',
    description: 'Get the status of a background task. Returns task state (pending, running, completed, failed, cancelled), progress (0.0-1.0), status message, and result if completed.',
    inputSchema: {
      type: 'object',
      properties: {
        task_id: {
          type: 'string',
          description: 'Task ID returned from an async operation'
        }
      },
      required: ['task_id']
    }
  },
  {
    name: 'task_cancel',
    description: 'Cancel a running background task. Returns whether cancellation was successful.',
    inputSchema: {
      type: 'object',
      properties: {
        task_id: {
          type: 'string',
          description: 'Task ID to cancel'
        }
      },
      required: ['task_id']
    }
  },
  {
    name: 'task_list',
    description: 'List all background tasks. Optionally filter by state. Returns task summaries and counts.',
    inputSchema: {
      type: 'object',
      properties: {
        state: {
          type: 'string',
          enum: ['pending', 'running', 'completed', 'failed', 'cancelled'],
          description: 'Optional state filter'
        }
      },
      required: []
    }
  },
  {
    name: 'task_cleanup',
    description: 'Remove old completed/failed/cancelled tasks. Default removes tasks older than 5 minutes.',
    inputSchema: {
      type: 'object',
      properties: {
        max_age_seconds: {
          type: 'integer',
          description: 'Maximum age in seconds for tasks to keep. Default: 300 (5 minutes)',
          default: 300
        }
      },
      required: []
    }
  }
];

// ============================================================================
// Tool Execution
// ============================================================================

/**
 * Map MCP tool names to HTTP endpoints
 */
const TOOL_ENDPOINT_MAP = {
  'get_health': { method: 'GET', path: '/health' },
  'get_version': { method: 'GET', path: '/version' },
  'read_memory': { method: 'POST', path: '/tools/read_memory' },
  'write_memory': { method: 'POST', path: '/tools/write_memory' },
  'scan_pattern': { method: 'POST', path: '/tools/scan_pattern' },
  'scan_pattern_async': { method: 'POST', path: '/tools/scan_pattern_async' },
  'scan_strings': { method: 'POST', path: '/tools/scan_strings' },
  'scan_strings_async': { method: 'POST', path: '/tools/scan_strings_async' },
  'disassemble': { method: 'POST', path: '/tools/disassemble' },
  'decompile': { method: 'POST', path: '/tools/decompile' },
  'generate_signature': { method: 'POST', path: '/tools/generate_signature' },
  'memory_snapshot': { method: 'POST', path: '/tools/memory_snapshot' },
  'memory_snapshot_list': { method: 'POST', path: '/tools/memory_snapshot_list' },
  'memory_snapshot_delete': { method: 'POST', path: '/tools/memory_snapshot_delete' },
  'memory_diff': { method: 'POST', path: '/tools/memory_diff' },
  'get_processes': { method: 'GET', path: '/tools/processes' },
  'get_modules': { method: 'POST', path: '/tools/modules' },
  'resolve_pointer': { method: 'POST', path: '/tools/resolve_pointer' },
  'find_xrefs': { method: 'POST', path: '/tools/find_xrefs' },
  'memory_regions': { method: 'POST', path: '/tools/memory_regions' },
  'dump_module': { method: 'POST', path: '/tools/dump_module' },
  // Emulation tools
  'emu_create': { method: 'POST', path: '/tools/emu_create' },
  'emu_destroy': { method: 'POST', path: '/tools/emu_destroy' },
  'emu_map_module': { method: 'POST', path: '/tools/emu_map_module' },
  'emu_map_region': { method: 'POST', path: '/tools/emu_map_region' },
  'emu_set_registers': { method: 'POST', path: '/tools/emu_set_registers' },
  'emu_get_registers': { method: 'POST', path: '/tools/emu_get_registers' },
  'emu_run': { method: 'POST', path: '/tools/emu_run' },
  'emu_run_instructions': { method: 'POST', path: '/tools/emu_run_instructions' },
  'emu_reset': { method: 'POST', path: '/tools/emu_reset' },
  // RTTI Analysis tools
  'rtti_parse_vtable': { method: 'POST', path: '/tools/rtti_parse_vtable' },
  'rtti_scan': { method: 'POST', path: '/tools/rtti_scan' },
  'rtti_scan_module': { method: 'POST', path: '/tools/rtti_scan_module' },
  // RTTI Cache tools
  'rtti_cache_list': { method: 'POST', path: '/tools/rtti_cache_list' },
  'rtti_cache_query': { method: 'POST', path: '/tools/rtti_cache_query' },
  'rtti_cache_get': { method: 'POST', path: '/tools/rtti_cache_get' },
  'rtti_cache_clear': { method: 'POST', path: '/tools/rtti_cache_clear' },
  'read_vtable': { method: 'POST', path: '/tools/read_vtable' },
  // Bookmark tools
  'bookmark_list': { method: 'GET', path: '/tools/bookmarks' },
  'bookmark_add': { method: 'POST', path: '/tools/bookmarks/add' },
  'bookmark_remove': { method: 'POST', path: '/tools/bookmarks/remove' },
  'bookmark_update': { method: 'POST', path: '/tools/bookmarks/update' },
  // CS2 Consolidated init (one-shot)
  'cs2_init': { method: 'POST', path: '/tools/cs2_init' },
  // CS2 Schema query tools
  'cs2_schema_get_offset': { method: 'POST', path: '/tools/cs2_schema_get_offset' },
  'cs2_schema_find_class': { method: 'POST', path: '/tools/cs2_schema_find_class' },
  'cs2_schema_cache_list': { method: 'POST', path: '/tools/cs2_schema_cache_list' },
  'cs2_schema_cache_query': { method: 'POST', path: '/tools/cs2_schema_cache_query' },
  'cs2_schema_cache_get': { method: 'POST', path: '/tools/cs2_schema_cache_get' },
  'cs2_schema_cache_clear': { method: 'POST', path: '/tools/cs2_schema_cache_clear' },
  // CS2 Entity query tools
  'cs2_identify': { method: 'POST', path: '/tools/cs2_identify' },
  'cs2_read_field': { method: 'POST', path: '/tools/cs2_read_field' },
  'cs2_inspect': { method: 'POST', path: '/tools/cs2_inspect' },
  'cs2_get_local_player': { method: 'POST', path: '/tools/cs2_get_local_player' },
  'cs2_get_entity': { method: 'POST', path: '/tools/cs2_get_entity' },
  'cs2_list_players': { method: 'POST', path: '/tools/cs2_list_players' },
  'cs2_get_game_state': { method: 'POST', path: '/tools/cs2_get_game_state' },
  // Function recovery tools
  'recover_functions': { method: 'POST', path: '/tools/recover_functions' },
  'get_function_at': { method: 'POST', path: '/tools/get_function_at' },
  'get_function_containing': { method: 'POST', path: '/tools/get_function_containing' },
  'find_function_bounds': { method: 'POST', path: '/tools/find_function_bounds' },
  // CFG analysis tools
  'build_cfg': { method: 'POST', path: '/tools/build_cfg' },
  'get_cfg_node': { method: 'POST', path: '/tools/get_cfg_node' },
  // Expression evaluation
  'evaluate_expression': { method: 'POST', path: '/tools/evaluate_expression' },
  // Task management tools
  'task_status': { method: 'POST', path: '/tools/task_status' },
  'task_cancel': { method: 'POST', path: '/tools/task_cancel' },
  'task_list': { method: 'POST', path: '/tools/task_list' },
  'task_cleanup': { method: 'POST', path: '/tools/task_cleanup' }
};

/**
 * Execute a tool call
 * @param {string} toolName - Name of the tool to execute
 * @param {object} args - Tool arguments
 * @returns {Promise<object>} Tool execution result
 */
async function executeTool(toolName, args) {
  const endpoint = TOOL_ENDPOINT_MAP[toolName];
  if (!endpoint) {
    throw new Error(`Unknown tool: ${toolName}`);
  }

  // Queue the request to serialize all MCP operations
  // This prevents flooding Orpheus with concurrent requests
  return requestQueue.enqueue(async () => {
    const queueDepth = requestQueue.depth;
    if (queueDepth > 1) {
      log('DEBUG', `Request queued: ${toolName} (depth: ${queueDepth})`);
    }

    log('DEBUG', `Executing tool: ${toolName}`, { args });

    const result = await makeHttpRequest(endpoint.method, endpoint.path, args);

    log('DEBUG', `Tool executed successfully: ${toolName}`, { result });

    return result;
  });
}

// ============================================================================
// JSON-RPC 2.0 Protocol Handler
// ============================================================================

/**
 * Send JSON-RPC 2.0 response
 */
function sendResponse(id, result) {
  const response = {
    jsonrpc: '2.0',
    id: id,
    result: result
  };
  console.log(JSON.stringify(response));
}

/**
 * Send JSON-RPC 2.0 error response
 */
function sendError(id, code, message, data = null) {
  const response = {
    jsonrpc: '2.0',
    id: id,
    error: {
      code: code,
      message: message,
      ...(data && { data })
    }
  };
  console.log(JSON.stringify(response));
}

/**
 * Handle incoming JSON-RPC request
 */
async function handleRequest(request) {
  const { id, method, params } = request;

  try {
    switch (method) {
      case 'initialize':
        log('INFO', 'Client initializing MCP connection');
        sendResponse(id, {
          protocolVersion: '2024-11-05',
          serverInfo: {
            name: 'orpheus',
            version: '1.0.0'
          },
          capabilities: {
            tools: {}
          }
        });
        break;

      case 'tools/list':
        log('INFO', 'Client requesting tool list');
        sendResponse(id, {
          tools: MCP_TOOLS
        });
        break;

      case 'tools/call':
        const toolName = params.name;
        const toolArgs = params.arguments || {};

        log('INFO', `Tool call: ${toolName}`);

        try {
          const result = await executeTool(toolName, toolArgs);

          // Format result as MCP expects: array of content blocks
          sendResponse(id, {
            content: [
              {
                type: 'text',
                text: typeof result === 'string' ? result : JSON.stringify(result, null, 2)
              }
            ]
          });
        } catch (toolError) {
          log('ERROR', `Tool execution failed: ${toolName}`, { error: toolError.message });
          sendError(id, -32603, `Tool execution failed: ${toolError.message}`);
        }
        break;

      case 'notifications/cancelled':
        // Client cancelled a request - just log it
        log('INFO', 'Client cancelled request');
        break;

      default:
        log('WARN', `Unknown method: ${method}`);
        sendError(id, -32601, `Method not found: ${method}`);
    }
  } catch (error) {
    log('ERROR', 'Request handling error', { error: error.message, stack: error.stack });
    sendError(id, -32603, `Internal error: ${error.message}`);
  }
}

// ============================================================================
// Main: stdio JSON-RPC Server
// ============================================================================

log('INFO', 'Orpheus MCP Bridge starting', {
  mcp_url: MCP_URL,
  api_key: API_KEY.substring(0, 12) + '...'
});

// Buffer for accumulating incomplete JSON
let buffer = '';

process.stdin.setEncoding('utf8');

process.stdin.on('data', (chunk) => {
  buffer += chunk;

  // Process complete lines
  const lines = buffer.split('\n');
  buffer = lines.pop() || ''; // Keep incomplete line in buffer

  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed) continue;

    try {
      const request = JSON.parse(trimmed);
      log('DEBUG', 'Received request', { method: request.method, id: request.id });
      handleRequest(request).catch((err) => {
        log('ERROR', 'Unhandled request error', { error: err.message });
      });
    } catch (parseError) {
      log('ERROR', 'JSON parse error', { error: parseError.message, line: trimmed.substring(0, 100) });
      // Send error response with null id since we can't parse the request
      sendError(null, -32700, 'Parse error: Invalid JSON');
    }
  }
});

process.stdin.on('end', () => {
  log('INFO', 'stdin closed, exiting');
  process.exit(0);
});

// Graceful shutdown
process.on('SIGINT', () => {
  log('INFO', 'Received SIGINT, exiting gracefully');
  process.exit(0);
});

process.on('SIGTERM', () => {
  log('INFO', 'Received SIGTERM, exiting gracefully');
  process.exit(0);
});

// Handle uncaught errors
process.on('uncaughtException', (err) => {
  log('ERROR', 'Uncaught exception', { error: err.message, stack: err.stack });
  process.exit(1);
});

process.on('unhandledRejection', (reason, promise) => {
  log('ERROR', 'Unhandled rejection', { reason: reason?.toString() });
  process.exit(1);
});

log('INFO', 'Bridge ready and listening on stdin');
