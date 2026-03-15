use std::path::PathBuf;
use std::io::Write;
use std::sync::OnceLock;

type OrpheusInitFn = unsafe extern "C" fn() -> i32;
type OrpheusStartServerFn = unsafe extern "C" fn(port: i32, api_key: *const std::os::raw::c_char) -> i32;
type OrpheusShutdownFn = unsafe extern "C" fn();

// Store shutdown function globally so we can call it from the exit handler
static SHUTDOWN_FN: OnceLock<OrpheusShutdownFn> = OnceLock::new();
// Keep library alive globally — must not drop before shutdown is called
static CORE_LIB: OnceLock<libloading::Library> = OnceLock::new();

fn log(msg: &str) {
    let path = std::env::current_exe()
        .ok()
        .and_then(|e| e.parent().map(|p| p.join("orpheus-debug.log")))
        .unwrap_or_else(|| PathBuf::from("orpheus-debug.log"));
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
        let _ = writeln!(f, "{}", msg);
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

        if start_server(8765, std::ptr::null()) != 0 {
            log("MCP server failed to start");
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

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    init_backend();

    // Safety net: shutdown when run() returns, regardless of how it exits
    let _guard = ShutdownGuard;

    let app = tauri::Builder::default()
        .build(tauri::generate_context!())
        .expect("error building tauri application");

    app.run(|_handle, event| {
        if let tauri::RunEvent::ExitRequested { .. } = &event {
            do_shutdown();
        }
    });
}
