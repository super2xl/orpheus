use std::process::{Command, Child};
use std::sync::Mutex;
use std::path::PathBuf;

struct BackendProcess(Mutex<Option<Child>>);

fn find_backend() -> Option<PathBuf> {
    // Look for orpheus-core.exe next to the Tauri app
    let exe = std::env::current_exe().ok()?;
    let dir = exe.parent()?;

    let candidates = [
        dir.join("orpheus-core.exe"),
        dir.join("orpheus-core"),
        dir.join("../orpheus-core.exe"),
        dir.join("../../build/bin/Release/orpheus.exe"),
    ];

    candidates.into_iter().find(|p| p.exists())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
  tauri::Builder::default()
    .setup(|app| {
      // Auto-launch the C++ backend (MCP server)
      if let Some(backend_path) = find_backend() {
          match Command::new(&backend_path).spawn() {
              Ok(child) => {
                  println!("[orpheus] Backend started (pid: {})", child.id());
                  app.manage(BackendProcess(Mutex::new(Some(child))));
              }
              Err(e) => {
                  eprintln!("[orpheus] Failed to start backend: {}", e);
                  // Continue anyway — user can start it manually
              }
          }
      } else {
          eprintln!("[orpheus] Backend binary not found — start orpheus-core manually");
      }

      if cfg!(debug_assertions) {
        app.handle().plugin(
          tauri_plugin_log::Builder::default()
            .level(log::LevelFilter::Info)
            .build(),
        )?;
      }
      Ok(())
    })
    .on_event(|app, event| {
        // Kill backend when Tauri exits
        if let tauri::RunEvent::Exit = event {
            if let Some(state) = app.try_state::<BackendProcess>() {
                if let Ok(mut guard) = state.0.lock() {
                    if let Some(mut child) = guard.take() {
                        let _ = child.kill();
                        println!("[orpheus] Backend stopped");
                    }
                }
            }
        }
    })
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
}
