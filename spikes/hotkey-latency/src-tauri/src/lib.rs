// Throwaway spike: measures hotkey press -> focused, painted TipTap editor.
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::Instant;

use tauri::{AppHandle, Emitter, Manager, State};
use tauri_plugin_global_shortcut::ShortcutState;

#[derive(Default)]
struct Trials {
    pressed_at: Option<Instant>,
    trial: u32,
    ready_ms: Option<f64>,
}

fn results_file() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("results")
        .join("trials.csv")
}

fn on_hotkey(app: &AppHandle) {
    {
        let state = app.state::<Mutex<Trials>>();
        let mut t = state.lock().unwrap();
        t.pressed_at = Some(Instant::now());
        t.trial += 1;
        t.ready_ms = None;
    }
    if let Some(w) = app.get_webview_window("capture") {
        let _ = w.center();
        let _ = w.show();
        let _ = w.set_focus();
        let _ = w.emit("capture:shown", ());
    }
}

#[tauri::command]
fn report_ready(state: State<Mutex<Trials>>) {
    let mut t = state.lock().unwrap();
    if let (Some(p), None) = (t.pressed_at, t.ready_ms) {
        t.ready_ms = Some(p.elapsed().as_secs_f64() * 1000.0);
    }
}

#[tauri::command]
fn report_hidden(app: AppHandle, state: State<Mutex<Trials>>, text: String) -> Result<(), String> {
    let (trial, ready_ms) = {
        let mut t = state.lock().unwrap();
        t.pressed_at = None;
        (t.trial, t.ready_ms.take())
    };
    if let Some(w) = app.get_webview_window("capture") {
        w.hide().map_err(|e| e.to_string())?;
    }
    let path = results_file();
    fs::create_dir_all(path.parent().unwrap()).map_err(|e| e.to_string())?;
    let new = !path.exists();
    let mut f = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
        .map_err(|e| e.to_string())?;
    if new {
        writeln!(f, "trial,ready_ms,text").map_err(|e| e.to_string())?;
    }
    let ready = ready_ms.map(|m| format!("{m:.2}")).unwrap_or_default();
    let text = format!("\"{}\"", text.replace('"', "\"\""));
    writeln!(f, "{trial},{ready},{text}").map_err(|e| e.to_string())
}

// Diagnostic: every DOM key/focus event the capture page sees, per trial.
#[tauri::command]
fn log_event(state: State<Mutex<Trials>>, kind: String, key: String) -> Result<(), String> {
    let (trial, since) = {
        let t = state.lock().unwrap();
        let since = t.pressed_at.map(|p| p.elapsed().as_secs_f64() * 1000.0);
        (t.trial, since)
    };
    let path = results_file().with_file_name("events.csv");
    let new = !path.exists();
    let mut f = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
        .map_err(|e| e.to_string())?;
    if new {
        writeln!(f, "trial,ms_since_press,kind,key").map_err(|e| e.to_string())?;
    }
    let since = since.map(|m| format!("{m:.2}")).unwrap_or_default();
    writeln!(f, "{trial},{since},{kind},{key}").map_err(|e| e.to_string())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(Mutex::new(Trials::default()))
        .plugin(
            tauri_plugin_global_shortcut::Builder::new()
                .with_shortcuts(["ctrl+alt+j"])
                .expect("valid shortcut")
                .with_handler(|app, _shortcut, event| {
                    if event.state() == ShortcutState::Pressed {
                        on_hotkey(app);
                    }
                })
                .build(),
        )
        .invoke_handler(tauri::generate_handler![report_ready, report_hidden, log_event])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
