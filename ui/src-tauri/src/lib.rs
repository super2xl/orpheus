use std::path::PathBuf;
use std::io::Write;

type OrpheusInitFn = unsafe extern "C" fn() -> i32;
type OrpheusStartServerFn = unsafe extern "C" fn(port: i32, api_key: *const std::os::raw::c_char) -> i32;
type OrpheusShutdownFn = unsafe extern "C" fn();

struct OrpheusBackend {
    _lib: libloading::Library,
    shutdown_fn: OrpheusShutdownFn,
}

impl Drop for OrpheusBackend {
    fn drop(&mut self) {
        log("Calling orpheus_shutdown...");
        unsafe { (self.shutdown_fn)(); }
        log("Backend shutdown complete");
    }
}

fn log(msg: &str) {
    let path = std::env::current_exe()
        .ok()
        .and_then(|e| e.parent().map(|p| p.join("orpheus-debug.log")))
        .unwrap_or_else(|| PathBuf::from("orpheus-debug.log"));
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
        let _ = writeln!(f, "[{}] {}", chrono_now(), msg);
    }
}

fn chrono_now() -> String {
    let d = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default();
    format!("{}.{:03}", d.as_secs(), d.subsec_millis())
}

fn find_core_dll() -> Option<PathBuf> {
    let dll_name = if cfg!(windows) { "orpheus_core.dll" } else { "liborpheus_core.so" };
    let exe_dir = std::env::current_exe().ok()?.parent()?.to_path_buf();

    log(&format!("Looking for DLL in: {:?}", exe_dir));

    let path = exe_dir.join(dll_name);
    if path.exists() {
        log(&format!("Found: {:?}", path));
        return Some(path);
    }

    let resource_path = exe_dir.join("resources").join(dll_name);
    if resource_path.exists() {
        log(&format!("Found in resources: {:?}", resource_path));
        return Some(resource_path);
    }

    log(&format!("DLL NOT FOUND at {:?} or {:?}", path, resource_path));
    None
}

fn init_backend() -> Option<OrpheusBackend> {
    log("=== Orpheus Backend Init ===");

    let dll_path = find_core_dll()?;

    unsafe {
        log("Loading DLL...");
        let lib = match libloading::Library::new(&dll_path) {
            Ok(l) => { log("DLL loaded OK"); l }
            Err(e) => { log(&format!("DLL LOAD FAILED: {}", e)); return None; }
        };

        log("Resolving symbols...");
        let init: libloading::Symbol<OrpheusInitFn> = match lib.get(b"orpheus_init") {
            Ok(s) => { log("orpheus_init found"); s }
            Err(e) => { log(&format!("orpheus_init NOT FOUND: {}", e)); return None; }
        };
        let start_server: libloading::Symbol<OrpheusStartServerFn> = match lib.get(b"orpheus_start_server") {
            Ok(s) => { log("orpheus_start_server found"); s }
            Err(e) => { log(&format!("orpheus_start_server NOT FOUND: {}", e)); return None; }
        };
        let shutdown: libloading::Symbol<OrpheusShutdownFn> = match lib.get(b"orpheus_shutdown") {
            Ok(s) => { log("orpheus_shutdown found"); s }
            Err(e) => { log(&format!("orpheus_shutdown NOT FOUND: {}", e)); return None; }
        };

        let shutdown_fn: OrpheusShutdownFn = *shutdown;

        log("Calling orpheus_init...");
        let rc = init();
        log(&format!("orpheus_init returned: {}", rc));
        if rc != 0 {
            return None;
        }

        log("Calling orpheus_start_server(8765)...");
        let rc = start_server(8765, std::ptr::null());
        log(&format!("orpheus_start_server returned: {}", rc));

        log("Backend ready");
        Some(OrpheusBackend { _lib: lib, shutdown_fn })
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    log("=== Tauri Starting ===");
    let _backend = init_backend();

    if _backend.is_none() {
        log("WARNING: Running without backend");
    } else {
        log("Backend initialized, starting Tauri window...");
    }

    tauri::Builder::default()
        .run(tauri::generate_context!())
        .expect("error while running tauri application");

    log("Tauri exiting, backend will drop now...");
}
