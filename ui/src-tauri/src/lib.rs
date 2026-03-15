use std::process::{Command, Child};
use std::sync::Mutex;

struct BackendProcess(Mutex<Option<Child>>);

fn spawn_backend(core_path: &std::path::Path) -> Option<Child> {
    let mut cmd = Command::new(core_path);
    cmd.arg("--auto");

    // Hide console window on Windows
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW
    }

    cmd.spawn().ok()
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
  tauri::Builder::default()
    .setup(|app| {
      // Launch the C++ core silently — bundled in resources
      let core_name = if cfg!(windows) { "orpheus-core.exe" } else { "orpheus-core" };

      // Check next to our exe first, then resource dir
      let exe_dir = std::env::current_exe().ok().and_then(|e| e.parent().map(|p| p.to_path_buf()));
      let core_path = exe_dir.as_ref()
          .map(|d| d.join(core_name))
          .filter(|p| p.exists())
          .or_else(|| app.path().resource_dir().ok().map(|d| d.join(core_name)).filter(|p| p.exists()));

      if let Some(path) = core_path {
          if let Some(child) = spawn_backend(&path) {
              app.manage(BackendProcess(Mutex::new(Some(child))));
          }
      }

      Ok(())
    })
    .on_event(|app, event| {
        if let tauri::RunEvent::Exit = event {
            if let Some(state) = app.try_state::<BackendProcess>() {
                if let Ok(mut guard) = state.0.lock() {
                    if let Some(mut child) = guard.take() {
                        let _ = child.kill();
                    }
                }
            }
        }
    })
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
}
