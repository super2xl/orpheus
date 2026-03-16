use std::path::PathBuf;
use std::sync::OnceLock;

type OrpheusInitFn = unsafe extern "C" fn() -> i32;
type OrpheusStartServerFn = unsafe extern "C" fn(port: i32, api_key: *const std::os::raw::c_char) -> i32;
type OrpheusShutdownFn = unsafe extern "C" fn();
type OrpheusGetApiKeyFn = unsafe extern "C" fn() -> *const std::os::raw::c_char;

// Store shutdown function globally so we can call it from the exit handler
static SHUTDOWN_FN: OnceLock<OrpheusShutdownFn> = OnceLock::new();
// Keep library alive globally — must not drop before shutdown is called
static CORE_LIB: OnceLock<libloading::Library> = OnceLock::new();
// Store the auto-generated API key so the frontend can retrieve it via Tauri command
static API_KEY: OnceLock<String> = OnceLock::new();

fn log(msg: &str) {
    #[cfg(debug_assertions)]
    {
        use std::io::Write;
        let path = std::env::current_exe()
            .ok()
            .and_then(|e| e.parent().map(|p| p.join("orpheus-debug.log")))
            .unwrap_or_else(|| PathBuf::from("orpheus-debug.log"));
        if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
            let _ = writeln!(f, "{}", msg);
        }
    }
}

fn find_core_dll() -> Option<PathBuf> {
    let dll_name = if cfg!(windows) { "orpheus_core.dll" } else { "liborpheus_core.so" };
    let exe_dir = std::env::current_exe().ok()?.parent()?.to_path_buf();

    let path = exe_dir.join(dll_name);
    if path.exists() { return Some(path); }

    let resource_path = exe_dir.join("resources").join(dll_name);
    if resource_path.exists() { return Some(resource_path); }

    None
}

fn init_backend() -> bool {
    let dll_path = match find_core_dll() {
        Some(p) => p,
        None => { log("DLL not found"); return false; }
    };

    unsafe {
        let lib = match libloading::Library::new(&dll_path) {
            Ok(l) => l,
            Err(e) => { log(&format!("DLL load failed: {}", e)); return false; }
        };

        let init: libloading::Symbol<OrpheusInitFn> = match lib.get(b"orpheus_init") {
            Ok(s) => s,
            Err(_) => { return false; }
        };
        let start_server: libloading::Symbol<OrpheusStartServerFn> = match lib.get(b"orpheus_start_server") {
            Ok(s) => s,
            Err(_) => { return false; }
        };
        let shutdown: libloading::Symbol<OrpheusShutdownFn> = match lib.get(b"orpheus_shutdown") {
            Ok(s) => s,
            Err(_) => { return false; }
        };

        // Cache shutdown fn globally before lib moves
        let _ = SHUTDOWN_FN.set(*shutdown);

        if init() != 0 {
            log("orpheus_init failed");
            return false;
        }

        // Start server with NULL api_key — server auto-generates one
        if start_server(8765, std::ptr::null()) != 0 {
            log("MCP server failed to start");
        } else {
            // Retrieve the auto-generated API key so the frontend can use it
            if let Ok(get_key) = lib.get::<OrpheusGetApiKeyFn>(b"orpheus_get_api_key") {
                let key_ptr = get_key();
                if !key_ptr.is_null() {
                    let key = std::ffi::CStr::from_ptr(key_ptr).to_string_lossy().to_string();
                    let _ = API_KEY.set(key);
                    log("API key retrieved from core");
                }
            }
        }

        // Store lib globally so it stays loaded
        let _ = CORE_LIB.set(lib);

        log("Backend initialized");
        true
    }
}

fn do_shutdown() {
    if let Some(shutdown_fn) = SHUTDOWN_FN.get() {
        log("Calling orpheus_shutdown");
        unsafe { shutdown_fn(); }
        log("Shutdown complete");
    }
}

/// Drop guard that calls shutdown when run() returns, even on force-close.
/// ExitRequested doesn't fire on all exit paths, so this is a safety net.
struct ShutdownGuard;
impl Drop for ShutdownGuard {
    fn drop(&mut self) {
        do_shutdown();
    }
}

/// Tauri command: returns the auto-generated API key to the frontend
#[tauri::command]
fn get_api_key() -> Option<String> {
    API_KEY.get().cloned()
}

#[derive(serde::Serialize, Clone)]
struct McpClientInfo {
    name: String,
    config_path: String,
    detected: bool,
    installed: bool,
}

/// Tauri command: detect installed MCP clients by checking known config paths
#[tauri::command]
fn detect_mcp_clients() -> Vec<McpClientInfo> {
    let mut clients = Vec::new();

    let home = dirs::home_dir().unwrap_or_default();
    let appdata = std::env::var("APPDATA").unwrap_or_default();

    // Claude Desktop
    let claude_config = PathBuf::from(&appdata).join("Claude").join("claude_desktop_config.json");
    clients.push(McpClientInfo {
        name: "Claude Desktop".into(),
        config_path: claude_config.to_string_lossy().into(),
        detected: claude_config.parent().map_or(false, |p| p.exists()),
        installed: false,
    });

    // Cursor
    let cursor_config = home.join(".cursor").join("mcp.json");
    clients.push(McpClientInfo {
        name: "Cursor".into(),
        config_path: cursor_config.to_string_lossy().into(),
        detected: cursor_config.parent().map_or(false, |p| p.exists()),
        installed: false,
    });

    // Claude Code / Claude CLI
    let claude_code_config = home.join(".claude").join("claude_code_config.json");
    clients.push(McpClientInfo {
        name: "Claude Code".into(),
        config_path: claude_code_config.to_string_lossy().into(),
        detected: claude_code_config.parent().map_or(false, |p| p.exists()),
        installed: false,
    });

    // VS Code (Cline extension)
    let vscode_config = home.join(".vscode").join("mcp.json");
    clients.push(McpClientInfo {
        name: "VS Code".into(),
        config_path: vscode_config.to_string_lossy().into(),
        detected: vscode_config.parent().map_or(false, |p| p.exists()),
        installed: false,
    });

    // Windsurf
    let windsurf_config = home.join(".windsurf").join("mcp_config.json");
    clients.push(McpClientInfo {
        name: "Windsurf".into(),
        config_path: windsurf_config.to_string_lossy().into(),
        detected: windsurf_config.parent().map_or(false, |p| p.exists()),
        installed: false,
    });

    // Check which ones already have Orpheus configured
    for client in &mut clients {
        if let Ok(content) = std::fs::read_to_string(&client.config_path) {
            client.installed = content.contains("orpheus");
        }
    }

    clients
}

/// Tauri command: install Orpheus MCP config into a client's config file
#[tauri::command]
fn install_mcp_config(
    config_path: String,
    server_url: String,
    api_key: String,
    bridge_path: String,
) -> Result<String, String> {
    // Ensure parent directory exists
    if let Some(parent) = std::path::Path::new(&config_path).parent() {
        if !parent.exists() {
            std::fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
    }

    // Read existing config or create new
    let mut config: serde_json::Value = if let Ok(content) = std::fs::read_to_string(&config_path) {
        serde_json::from_str(&content).unwrap_or(serde_json::json!({}))
    } else {
        serde_json::json!({})
    };

    // Add/update mcpServers.orpheus entry
    let mcp_servers = config
        .as_object_mut()
        .ok_or("Invalid config format")?
        .entry("mcpServers")
        .or_insert(serde_json::json!({}));

    mcp_servers["orpheus"] = serde_json::json!({
        "command": "node",
        "args": [bridge_path],
        "env": {
            "ORPHEUS_MCP_URL": server_url,
            "ORPHEUS_API_KEY": api_key
        }
    });

    // Write back with pretty formatting
    let formatted = serde_json::to_string_pretty(&config).map_err(|e| e.to_string())?;
    std::fs::write(&config_path, formatted).map_err(|e| e.to_string())?;

    Ok("Configured successfully".into())
}

/// Tauri command: get the path to mcp_bridge.js next to the executable
#[tauri::command]
fn get_bridge_path() -> Result<String, String> {
    let exe_dir = std::env::current_exe()
        .map_err(|e| e.to_string())?
        .parent()
        .ok_or("Cannot determine exe directory")?
        .to_path_buf();

    // Check next to exe first, then in resources/
    let bridge = exe_dir.join("mcp_bridge.js");
    if bridge.exists() {
        return Ok(bridge.to_string_lossy().into());
    }

    let bridge_res = exe_dir.join("resources").join("mcp_bridge.js");
    if bridge_res.exists() {
        return Ok(bridge_res.to_string_lossy().into());
    }

    // Fall back to project root (development mode)
    let project_bridge = exe_dir
        .ancestors()
        .find_map(|p| {
            let candidate = p.join("mcp_bridge.js");
            if candidate.exists() { Some(candidate) } else { None }
        });

    match project_bridge {
        Some(p) => Ok(p.to_string_lossy().into()),
        None => Err("mcp_bridge.js not found".into()),
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    init_backend();

    // Safety net: shutdown when run() returns, regardless of how it exits
    let _guard = ShutdownGuard;

    let app = tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![get_api_key, detect_mcp_clients, install_mcp_config, get_bridge_path])
        .build(tauri::generate_context!())
        .expect("error building tauri application");

    app.run(|_handle, event| {
        if let tauri::RunEvent::ExitRequested { .. } = &event {
            do_shutdown();
        }
    });
}
