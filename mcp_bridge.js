#!/usr/bin/env node

/**
 * Orpheus MCP Bridge
 * stdio-to-HTTP adapter for Model Context Protocol (JSON-RPC 2.0)
 */

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
  // CS2 Schema Tools
  // ============================================================================
  {
    name: 'cs2_schema_init',
    description: 'Initialize CS2 schema dumper by finding SchemaSystem interface. Must be called before other cs2_schema_* tools. Returns available scopes (client.dll, server.dll, etc.).',
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
    name: 'cs2_schema_dump',
    description: 'Dump schema classes from a specific scope or all scopes. Results are cached for fast subsequent queries. Use force_refresh to re-dump.',
    inputSchema: {
      type: 'object',
      properties: {
        scope: {
          type: 'string',
          description: 'Scope name to dump (e.g., "client.dll", "server.dll"). Omit to dump all scopes.'
        },
        force_refresh: {
          type: 'boolean',
          description: 'Force re-dump even if cache exists',
          default: false
        },
        deduplicate: {
          type: 'boolean',
          description: 'When dumping all scopes (no scope specified), deduplicate classes by name across all scopes (like Andromeda). Default: true',
          default: true
        }
      }
    }
  },
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
  // CS2 Entity Tools (RTTI + Schema Bridge)
  // ============================================================================
  {
    name: 'cs2_entity_init',
    description: 'Initialize CS2 entity system by pattern scanning for CGameEntitySystem and LocalPlayerController. Must be called before using cs2_get_local_player or cs2_get_entity. Caches results for subsequent calls.',
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
    description: 'Get the local player controller. Requires cs2_entity_init to be called first. Returns the controller address, identified class, and key fields (pawn handle, health, armor, alive status).',
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
  }
];

// ============================================================================
// Tool Execution
// ============================================================================

/**
 * Map MCP tool names to HTTP endpoints
 */
const TOOL_ENDPOINT_MAP = {
  'read_memory': { method: 'POST', path: '/tools/read_memory' },
  'scan_pattern': { method: 'POST', path: '/tools/scan_pattern' },
  'scan_strings': { method: 'POST', path: '/tools/scan_strings' },
  'disassemble': { method: 'POST', path: '/tools/disassemble' },
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
  // Bookmark tools
  'bookmark_list': { method: 'GET', path: '/tools/bookmarks' },
  'bookmark_add': { method: 'POST', path: '/tools/bookmarks/add' },
  'bookmark_remove': { method: 'POST', path: '/tools/bookmarks/remove' },
  'bookmark_update': { method: 'POST', path: '/tools/bookmarks/update' },
  // CS2 Schema tools
  'cs2_schema_init': { method: 'POST', path: '/tools/cs2_schema_init' },
  'cs2_schema_dump': { method: 'POST', path: '/tools/cs2_schema_dump' },
  'cs2_schema_get_offset': { method: 'POST', path: '/tools/cs2_schema_get_offset' },
  'cs2_schema_find_class': { method: 'POST', path: '/tools/cs2_schema_find_class' },
  'cs2_schema_cache_list': { method: 'POST', path: '/tools/cs2_schema_cache_list' },
  'cs2_schema_cache_query': { method: 'POST', path: '/tools/cs2_schema_cache_query' },
  'cs2_schema_cache_get': { method: 'POST', path: '/tools/cs2_schema_cache_get' },
  'cs2_schema_cache_clear': { method: 'POST', path: '/tools/cs2_schema_cache_clear' },
  // CS2 Entity tools (RTTI + Schema bridge)
  'cs2_entity_init': { method: 'POST', path: '/tools/cs2_entity_init' },
  'cs2_identify': { method: 'POST', path: '/tools/cs2_identify' },
  'cs2_read_field': { method: 'POST', path: '/tools/cs2_read_field' },
  'cs2_inspect': { method: 'POST', path: '/tools/cs2_inspect' },
  'cs2_get_local_player': { method: 'POST', path: '/tools/cs2_get_local_player' },
  'cs2_get_entity': { method: 'POST', path: '/tools/cs2_get_entity' }
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
