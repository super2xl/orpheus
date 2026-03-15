use std::path::PathBuf;

// FFI function type signatures matching orpheus_api.h
type OrpheusInitFn = unsafe extern "C" fn() -> i32;
type OrpheusStartServerFn = unsafe extern "C" fn(port: i32, api_key: *const std::os::raw::c_char) -> i32;
type OrpheusShutdownFn = unsafe extern "C" fn();

/// Holds the loaded DLL and cached shutdown pointer.
/// The library handle must live as long as the application — when this
/// struct drops, orpheus_shutdown() is called and the DLL unloads.
struct OrpheusBackend {
    _lib: libloading::Library,
    shutdown_fn: OrpheusShutdownFn,
}

impl Drop for OrpheusBackend {
    fn drop(&mut self) {
        unsafe { (self.shutdown_fn)(); }
    }
}

fn find_core_dll() -> Option<PathBuf> {
    let dll_name = if cfg!(windows) {
        "orpheus_core.dll"
    } else if cfg!(target_os = "macos") {
        "liborpheus_core.dylib"
    } else {
        "liborpheus_core.so"
    };

    let exe_dir = std::env::current_exe().ok()?.parent()?.to_path_buf();

    // 1. Next to the executable (dev builds, portable)
    let path = exe_dir.join(dll_name);
    if path.exists() {
        return Some(path);
    }

    // 2. Tauri bundles resources into a resources/ subdirectory
    let resource_path = exe_dir.join("resources").join(dll_name);
    if resource_path.exists() {
        return Some(resource_path);
    }

    eprintln!("[orpheus] DLL not found at {:?} or {:?}", path, resource_path);
    None
}

fn init_backend() -> Option<OrpheusBackend> {
    let dll_path = find_core_dll()?;
    eprintln!("[orpheus] Loading core DLL: {:?}", dll_path);

    unsafe {
        let lib = match libloading::Library::new(&dll_path) {
            Ok(l) => l,
            Err(e) => {
                eprintln!("[orpheus] Failed to load DLL: {}", e);
                return None;
            }
        };

        // Resolve function pointers
        let init: libloading::Symbol<OrpheusInitFn> = lib.get(b"orpheus_init").ok()?;
        let start_server: libloading::Symbol<OrpheusStartServerFn> =
            lib.get(b"orpheus_start_server").ok()?;
        let shutdown: libloading::Symbol<OrpheusShutdownFn> =
            lib.get(b"orpheus_shutdown").ok()?;

        // Cache the raw fn pointer before lib moves
        let shutdown_fn: OrpheusShutdownFn = *shutdown;

        // Initialize core (logger, runtime manager, telemetry, OrpheusCore)
        let rc = init();
        if rc != 0 {
            eprintln!("[orpheus] orpheus_init failed (code {})", rc);
            return None;
        }
        eprintln!("[orpheus] Core initialized");

        // Start MCP server on default port, no API key (localhost only)
        let port = 8765i32;
        let rc = start_server(port, std::ptr::null());
        if rc != 0 {
            eprintln!("[orpheus] MCP server failed to start (code {})", rc);
            // Non-fatal: core is up, server just didn't bind
        } else {
            eprintln!("[orpheus] MCP server listening on port {}", port);
        }

        Some(OrpheusBackend {
            _lib: lib,
            shutdown_fn,
        })
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // Load and initialize the C++ core DLL BEFORE creating the window.
    // _backend stays alive for the lifetime of run() — when it drops,
    // orpheus_shutdown() is called and the DLL unloads cleanly.
    let _backend = init_backend();

    if _backend.is_none() {
        eprintln!("[orpheus] WARNING: Running without C++ backend");
    }

    tauri::Builder::default()
        .plugin(tauri_plugin_log::Builder::new().build())
        .run(tauri::generate_context!())
        .expect("error while running tauri application");

    // _backend drops here -> orpheus_shutdown() -> DLL unload
}
