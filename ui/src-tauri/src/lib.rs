use std::path::PathBuf;
use std::process::Command;

fn spawn_backend(core_path: &PathBuf) {
    let mut cmd = Command::new(core_path);
    cmd.arg("--auto");

    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x08000000);
    }

    match cmd.spawn() {
        Ok(_) => println!("[orpheus] Backend started"),
        Err(e) => eprintln!("[orpheus] Failed to start backend: {}", e),
    }
}

fn find_backend() -> Option<PathBuf> {
    let core_name = if cfg!(windows) { "orpheus-core.exe" } else { "orpheus-core" };
    let exe_dir = std::env::current_exe().ok()?.parent()?.to_path_buf();

    let candidates = vec![
        exe_dir.join(core_name),
        exe_dir.join("../").join(core_name),
    ];

    candidates.into_iter().find(|p: &PathBuf| p.exists())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .setup(|_app| {
            if let Some(path) = find_backend() {
                spawn_backend(&path);
            }
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
