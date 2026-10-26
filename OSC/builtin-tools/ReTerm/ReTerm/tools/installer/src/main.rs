#![cfg_attr(windows, windows_subsystem = "windows")]

use eframe::egui::{self, Color32, RichText, Stroke, Vec2};
use std::fs::{self, File};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

const INSTALL_DIR: &str = r"C:\Program Files\WebTek\ReTerm";
const BACKGROUND: Color32 = Color32::from_rgb(15, 18, 27);
const PANEL: Color32 = Color32::from_rgb(24, 29, 42);
const BORDER: Color32 = Color32::from_rgb(51, 61, 82);
const TEXT: Color32 = Color32::from_rgb(231, 237, 247);
const MUTED: Color32 = Color32::from_rgb(151, 165, 188);
const ACCENT: Color32 = Color32::from_rgb(105, 220, 184);

const FILES: [(&str, &[u8]); 9] = [
    (
        "ReTerm.exe",
        include_bytes!("../../../target/bundle-build/release/ReTerm.exe"),
    ),
    (
        "ReTermCompMaker.exe",
        include_bytes!("../../../target/bundle-build/release/ReTermCompMaker.exe"),
    ),
    (
        "ReTermCompMakerGui.exe",
        include_bytes!("../../../target/bundle-build/release/ReTermCompMakerGui.exe"),
    ),
    (
        "ReTermModMaker.exe",
        include_bytes!("../../../target/bundle-build/release/ReTermModMaker.exe"),
    ),
    (
        "ReTermModMakerGui.exe",
        include_bytes!("../../../target/bundle-build/release/ReTermModMakerGui.exe"),
    ),
    (
        "ReTermModLauncher.exe",
        include_bytes!("../../../target/bundle-build/release/ReTermModLauncher.exe"),
    ),
    (
        "Nuke.exe",
        include_bytes!("../../../target/bundle-build/release/Nuke.exe"),
    ),
    (
        "Shellpass.exe",
        include_bytes!("../../../target/bundle-build/release/Shellpass.exe"),
    ),
    (
        "Compall.exe",
        include_bytes!("../../../target/bundle-build/release/Compall.exe"),
    ),
];

const WT_PROFILE_GUID: &str = "{6B2A0F1D-7A3E-4A5D-9C81-2E7E4B10A001}";

#[derive(Clone, Copy)]
struct InstallOptions {
    desktop_shortcut: bool,
    start_menu_shortcuts: bool,
    windows_terminal_profile: bool,
}

impl Default for InstallOptions {
    fn default() -> Self {
        Self {
            desktop_shortcut: false,
            start_menu_shortcuts: true,
            windows_terminal_profile: true,
        }
    }
}

struct InstallerApp {
    status: Option<(bool, String)>,
    options: InstallOptions,
}

impl Default for InstallerApp {
    fn default() -> Self {
        Self {
            status: None,
            options: InstallOptions::default(),
        }
    }
}

impl eframe::App for InstallerApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::CentralPanel::default()
            .frame(
                egui::Frame::new()
                    .fill(BACKGROUND)
                    .inner_margin(egui::Margin::same(28)),
            )
            .show(ctx, |ui| {
                ui.set_max_width(640.0);
                ui.vertical(|ui| {
                    ui.label(
                        RichText::new("Install ReTerm")
                            .size(26.0)
                            .strong()
                            .color(TEXT),
                    );
                    ui.add_space(8.0);
                    ui.label(
                        RichText::new(
                            "One installer executable containing ReTerm, its makers, tools, and module launcher.",
                        )
                        .color(MUTED),
                    );
                    ui.add_space(18.0);

                    egui::Frame::new()
                        .fill(PANEL)
                        .stroke(Stroke::new(1.0_f32, BORDER))
                        .corner_radius(10)
                        .inner_margin(egui::Margin::same(18))
                        .show(ui, |ui| {
                            ui.label(RichText::new("Install location").color(MUTED));
                            ui.label(
                                RichText::new(INSTALL_DIR)
                                    .monospace()
                                    .color(TEXT),
                            );
                            ui.add_space(12.0);
                            for (name, _) in FILES {
                                ui.label(RichText::new(format!("• {name}")).color(TEXT));
                            }
                            ui.separator();
                            ui.checkbox(
                                &mut self.options.desktop_shortcut,
                                "Create ReTerm desktop icon",
                            );
                            ui.checkbox(
                                &mut self.options.start_menu_shortcuts,
                                "Create Start Menu shortcuts",
                            );
                            ui.checkbox(
                                &mut self.options.windows_terminal_profile,
                                "Add ReTerm profile to Windows Terminal",
                            );
                        });
                    ui.add_space(18.0);

                    if ui
                        .add(
                            egui::Button::new(
                                RichText::new("Install")
                                    .strong()
                                    .color(Color32::from_rgb(12, 25, 25)),
                            )
                            .fill(ACCENT)
                            .min_size(Vec2::new(150.0, 42.0)),
                        )
                        .clicked()
                    {
                        self.status = Some(match request_elevation(self.options) {
                            Ok(()) => (
                                true,
                                "Administrator approval requested. Complete the Windows prompt to finish installation.".to_owned(),
                            ),
                            Err(error) => (false, error.to_string()),
                        });
                    }

                    if let Some((success, message)) = &self.status {
                        ui.add_space(12.0);
                        ui.label(
                            RichText::new(message).color(if *success {
                                ACCENT
                            } else {
                                Color32::LIGHT_RED
                            }),
                        );
                    }
                });
            });
    }
}

fn main() -> eframe::Result<()> {
    let args = std::env::args_os().skip(1).collect::<Vec<_>>();
    if args.iter().any(|arg| arg == "--install") {
        let result = run_elevated_install(&args);
        show_install_result(&result);
        std::process::exit(i32::from(result.is_err()));
    }

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("ReTerm Setup")
            .with_maximized(true)
            .with_inner_size([580.0, 420.0])
            .with_min_inner_size([520.0, 380.0]),
        ..Default::default()
    };
    eframe::run_native(
        "ReTerm Setup",
        options,
        Box::new(|cc| {
            let mut style = egui::Style::default();
            style.visuals = egui::Visuals::dark();
            style.visuals.panel_fill = BACKGROUND;
            style.visuals.window_fill = PANEL;
            cc.egui_ctx.set_style(style);
            Ok(Box::<InstallerApp>::default())
        }),
    )
}

fn request_elevation(options: InstallOptions) -> io::Result<()> {
    #[cfg(windows)]
    {
        use std::ptr;

        #[link(name = "shell32")]
        unsafe extern "system" {
            fn ShellExecuteW(
                hwnd: *mut std::ffi::c_void,
                operation: *const u16,
                file: *const u16,
                parameters: *const u16,
                directory: *const u16,
                show_command: i32,
            ) -> *mut std::ffi::c_void;
        }

        let executable = std::env::current_exe()?;
        let operation = wide_null("runas");
        let executable = wide_null_os(executable.as_os_str());
        let parameters = wide_null(&elevated_arguments(options)?);
        let result = unsafe {
            ShellExecuteW(
                ptr::null_mut(),
                operation.as_ptr(),
                executable.as_ptr(),
                parameters.as_ptr(),
                ptr::null(),
                1,
            )
        } as isize;
        if result <= 32 {
            return Err(io::Error::other(format!(
                "Windows could not start the elevated installer (error {result})"
            )));
        }
        Ok(())
    }
    #[cfg(not(windows))]
    {
        Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "the ReTerm installer only runs on Windows",
        ))
    }
}

fn elevated_arguments(options: InstallOptions) -> io::Result<String> {
    let mut args = vec!["--install".to_owned()];
    if options.desktop_shortcut {
        args.push("--desktop-shortcut".to_owned());
    }
    if options.start_menu_shortcuts {
        args.push("--start-menu-shortcuts".to_owned());
    }
    if options.windows_terminal_profile {
        args.push("--windows-terminal-profile".to_owned());
    }
    for (flag, variable, suffix) in [
        ("--desktop-dir", "USERPROFILE", "Desktop"),
        ("--appdata-dir", "APPDATA", ""),
        ("--local-appdata-dir", "LOCALAPPDATA", ""),
    ] {
        let base = std::env::var_os(variable).ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::NotFound,
                format!("could not locate {variable} for installation options"),
            )
        })?;
        let path = PathBuf::from(base).join(suffix);
        args.push(flag.to_owned());
        args.push(format!("\"{}\"", path.display()));
    }
    Ok(args.join(" "))
}

fn run_elevated_install(args: &[std::ffi::OsString]) -> io::Result<InstallReport> {
    let mut options = InstallOptions {
        desktop_shortcut: false,
        start_menu_shortcuts: false,
        windows_terminal_profile: false,
    };
    let mut desktop = None;
    let mut appdata = None;
    let mut local_appdata = None;
    let mut iter = args.iter();
    while let Some(arg) = iter.next() {
        match arg.to_string_lossy().as_ref() {
            "--desktop-shortcut" => options.desktop_shortcut = true,
            "--start-menu-shortcuts" => options.start_menu_shortcuts = true,
            "--windows-terminal-profile" => options.windows_terminal_profile = true,
            "--desktop-dir" => desktop = iter.next().map(PathBuf::from),
            "--appdata-dir" => appdata = iter.next().map(PathBuf::from),
            "--local-appdata-dir" => local_appdata = iter.next().map(PathBuf::from),
            _ => {}
        }
    }
    install(
        Path::new(INSTALL_DIR),
        options,
        desktop.as_deref(),
        appdata.as_deref(),
        local_appdata.as_deref(),
    )
}

struct InstallReport {
    warnings: Vec<String>,
}

fn install(
    directory: &Path,
    options: InstallOptions,
    desktop: Option<&Path>,
    appdata: Option<&Path>,
    local_appdata: Option<&Path>,
) -> io::Result<InstallReport> {
    fs::create_dir_all(directory)?;
    for (name, contents) in FILES {
        install_file(directory, name, contents)?;
    }

    let mut warnings = Vec::new();
    if options.desktop_shortcut {
        let result = desktop
            .ok_or_else(|| missing_user_path("desktop"))
            .and_then(|path| {
                create_shortcut(
                    path.join("ReTerm.lnk").as_path(),
                    directory.join("ReTerm.exe").as_path(),
                    "ReTerm",
                )
            });
        if let Err(error) = result {
            warnings.push(format!("Desktop shortcut: {error}"));
        }
    }
    if options.start_menu_shortcuts {
        let result = appdata
            .ok_or_else(|| missing_user_path("roaming application data"))
            .and_then(|path| {
                let folder = path
                    .join("Microsoft")
                    .join("Windows")
                    .join("Start Menu")
                    .join("Programs")
                    .join("ReTerm");
                fs::create_dir_all(&folder)?;
                create_shortcut(
                    &folder.join("ReTerm.lnk"),
                    &directory.join("ReTerm.exe"),
                    "ReTerm shell",
                )?;
                create_shortcut(
                    &folder.join("ReTerm Component Maker.lnk"),
                    &directory.join("ReTermCompMakerGui.exe"),
                    "ReTerm Component Maker",
                )?;
                create_shortcut(
                    &folder.join("ReTerm Component Maker CLI.lnk"),
                    &directory.join("ReTermCompMaker.exe"),
                    "ReTerm Component Maker CLI",
                )?;
                create_shortcut(
                    &folder.join("ReTerm ModMaker.lnk"),
                    &directory.join("ReTermModMakerGui.exe"),
                    "ReTerm ModMaker",
                )?;
                create_shortcut(
                    &folder.join("ReTerm ModMaker CLI.lnk"),
                    &directory.join("ReTermModMaker.exe"),
                    "ReTerm ModMaker CLI",
                )?;
                create_shortcut(
                    &folder.join("ReTerm Module Launcher.lnk"),
                    &directory.join("ReTermModLauncher.exe"),
                    "ReTerm Module Launcher",
                )
            });
        if let Err(error) = result {
            warnings.push(format!("Start Menu shortcuts: {error}"));
        }
    }
    if options.windows_terminal_profile {
        let result = local_appdata
            .ok_or_else(|| missing_user_path("local application data"))
            .and_then(|path| register_windows_terminal_profile(path, directory));
        if let Err(error) = result {
            warnings.push(format!("Windows Terminal profile: {error}"));
        }
    }
    Ok(InstallReport { warnings })
}

fn install_file(directory: &Path, name: &str, contents: &[u8]) -> io::Result<()> {
    let destination = directory.join(name);
    let temporary = directory.join(format!(".{name}.{}.tmp", std::process::id()));
    let mut file = File::create(&temporary)?;
    if let Err(error) = file.write_all(contents).and_then(|()| file.sync_all()) {
        let _ = fs::remove_file(&temporary);
        return Err(error);
    }
    drop(file);
    replace_file(&temporary, &destination)?;
    Ok(())
}

#[cfg(windows)]
fn replace_file(source: &Path, destination: &Path) -> io::Result<()> {
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn MoveFileExW(existing: *const u16, new: *const u16, flags: u32) -> i32;
    }
    let source_wide = wide_null_os(source.as_os_str());
    let destination_wide = wide_null_os(destination.as_os_str());
    let succeeded =
        unsafe { MoveFileExW(source_wide.as_ptr(), destination_wide.as_ptr(), 0x1 | 0x8) };
    if succeeded == 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}

#[cfg(not(windows))]
fn replace_file(source: &Path, destination: &Path) -> io::Result<()> {
    fs::rename(source, destination)
}

fn create_shortcut(path: &Path, target: &Path, description: &str) -> io::Result<()> {
    #[cfg(windows)]
    {
        use windows::core::{Interface, PCWSTR};
        use windows::Win32::System::Com::{
            CoCreateInstance, CoInitializeEx, CoUninitialize, IPersistFile, CLSCTX_INPROC_SERVER,
            COINIT_APARTMENTTHREADED,
        };
        use windows::Win32::UI::Shell::{IShellLinkW, ShellLink};

        struct Apartment;
        impl Drop for Apartment {
            fn drop(&mut self) {
                unsafe { CoUninitialize() };
            }
        }

        unsafe {
            CoInitializeEx(None, COINIT_APARTMENTTHREADED)
                .ok()
                .map_err(windows_error)?;
        }
        let _apartment = Apartment;
        let link: IShellLinkW = unsafe { CoCreateInstance(&ShellLink, None, CLSCTX_INPROC_SERVER) }
            .map_err(windows_error)?;
        let target = wide_null_os(target.as_os_str());
        let description = wide_null(description);
        let working_directory = wide_null_os(
            path.parent()
                .unwrap_or_else(|| Path::new(INSTALL_DIR))
                .as_os_str(),
        );
        unsafe {
            link.SetPath(PCWSTR(target.as_ptr()))
                .map_err(windows_error)?;
            link.SetDescription(PCWSTR(description.as_ptr()))
                .map_err(windows_error)?;
            link.SetWorkingDirectory(PCWSTR(working_directory.as_ptr()))
                .map_err(windows_error)?;
            link.SetIconLocation(PCWSTR(target.as_ptr()), 0)
                .map_err(windows_error)?;
        }
        let persist: IPersistFile = link.cast().map_err(windows_error)?;
        let shortcut_path = wide_null_os(path.as_os_str());
        unsafe {
            persist
                .Save(PCWSTR(shortcut_path.as_ptr()), true)
                .map_err(windows_error)?;
        }
        Ok(())
    }
    #[cfg(not(windows))]
    {
        let _ = (path, target, description);
        Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "Windows shortcuts can only be created on Windows",
        ))
    }
}

#[cfg(windows)]
fn windows_error(error: windows::core::Error) -> io::Error {
    io::Error::other(error.to_string())
}

fn show_install_result(result: &io::Result<InstallReport>) {
    #[cfg(windows)]
    {
        let (title, message, flags) = match result {
            Ok(report) => {
                let mut message = format!("ReTerm was installed to {INSTALL_DIR}.");
                if !report.warnings.is_empty() {
                    message.push_str("\n\nSome optional setup steps could not be completed:");
                    for warning in &report.warnings {
                        message.push_str("\n• ");
                        message.push_str(warning);
                    }
                }
                (
                    "ReTerm Setup",
                    message,
                    if report.warnings.is_empty() {
                        0x40
                    } else {
                        0x30
                    },
                )
            }
            Err(error) => (
                "ReTerm Setup - Installation Failed",
                format!("ReTerm could not be installed:\n{error}"),
                0x10,
            ),
        };
        show_message(title, &message, flags);
    }
    #[cfg(not(windows))]
    if let Err(error) = result {
        eprintln!("ReTerm installation failed: {error}");
    }
}

#[cfg(windows)]
fn show_message(title: &str, message: &str, flags: u32) {
    #[link(name = "user32")]
    unsafe extern "system" {
        fn MessageBoxW(
            hwnd: *mut std::ffi::c_void,
            text: *const u16,
            caption: *const u16,
            kind: u32,
        ) -> i32;
    }
    let message = wide_null(message);
    let title = wide_null(title);
    unsafe {
        MessageBoxW(
            std::ptr::null_mut(),
            message.as_ptr(),
            title.as_ptr(),
            flags,
        );
    }
}

#[cfg(windows)]
fn wide_null(value: &str) -> Vec<u16> {
    value.encode_utf16().chain(Some(0)).collect()
}

#[cfg(windows)]
fn wide_null_os(value: &std::ffi::OsStr) -> Vec<u16> {
    use std::os::windows::ffi::OsStrExt;
    value.encode_wide().chain(Some(0)).collect()
}

fn missing_user_path(label: &str) -> io::Error {
    io::Error::new(
        io::ErrorKind::NotFound,
        format!("could not locate the current user's {label} directory"),
    )
}

fn register_windows_terminal_profile(local_appdata: &Path, install_dir: &Path) -> io::Result<()> {
    let candidates = [
        local_appdata
            .join("Packages/Microsoft.WindowsTerminal_8wekyb3d8bbwe/LocalState/settings.json"),
        local_appdata.join(
            "Packages/Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe/LocalState/settings.json",
        ),
        local_appdata.join(
            "Packages/Microsoft.WindowsTerminalCanary_8wekyb3d8bbwe/LocalState/settings.json",
        ),
        local_appdata.join("Microsoft/Windows Terminal/settings.json"),
        local_appdata.join("Microsoft/Windows Terminal Preview/settings.json"),
    ];
    let settings = candidates
        .iter()
        .find(|path| path.is_file())
        .ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::NotFound,
                "Windows Terminal settings.json was not found",
            )
        })?;
    upsert_terminal_profile(settings, install_dir.join("ReTerm.exe").as_path())
}

fn upsert_terminal_profile(settings: &Path, executable: &Path) -> io::Result<()> {
    let mut contents = fs::read_to_string(settings)?;
    if contents.contains(WT_PROFILE_GUID) {
        return Ok(());
    }
    let root = find_object_start(&contents, 0)
        .ok_or_else(|| invalid_settings("settings root must be an object"))?;
    let profiles = object_member_value(&contents, root, "profiles")
        .ok_or_else(|| invalid_settings("settings must contain a profiles object"))?;
    let list = object_member_value(&contents, profiles, "list")
        .ok_or_else(|| invalid_settings("profiles must contain a list array"))?;
    if contents.as_bytes().get(list) != Some(&b'[') {
        return Err(invalid_settings("profiles.list must be an array"));
    }
    let empty = skip_trivia(&contents, list + 1)
        .is_some_and(|index| contents.as_bytes().get(index) == Some(&b']'));
    let profile = serde_json::json!({
        "guid": WT_PROFILE_GUID,
        "name": "ReTerm",
        "commandline": format!("\"{}\" --shell", executable.display()),
        "startingDirectory": "%USERPROFILE%"
    });
    let formatted = serde_json::to_string_pretty(&profile)
        .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?;
    let mut insertion = String::from("\n        ");
    insertion.push_str(&formatted.replace('\n', "\n        "));
    if !empty {
        insertion.push(',');
    }
    contents.insert_str(list + 1, &insertion);
    atomic_replace_text(settings, &contents)
}

fn object_member_value(text: &str, object_start: usize, target: &str) -> Option<usize> {
    if text.as_bytes().get(object_start) != Some(&b'{') {
        return None;
    }
    let mut cursor = object_start + 1;
    loop {
        cursor = skip_trivia(text, cursor)?;
        match text.as_bytes().get(cursor)? {
            b'}' => return None,
            b'"' => {
                let (key, after_key) = json_string(text, cursor)?;
                let colon = skip_trivia(text, after_key)?;
                if text.as_bytes().get(colon) != Some(&b':') {
                    return None;
                }
                let value = skip_trivia(text, colon + 1)?;
                if key == target {
                    return Some(value);
                }
                cursor = skip_json_value(text, value)?;
                cursor = skip_trivia(text, cursor)?;
                match text.as_bytes().get(cursor)? {
                    b',' => cursor += 1,
                    b'}' => return None,
                    _ => return None,
                }
            }
            _ => return None,
        }
    }
}

fn find_object_start(text: &str, mut cursor: usize) -> Option<usize> {
    cursor = skip_trivia(text, cursor)?;
    (text.as_bytes().get(cursor) == Some(&b'{')).then_some(cursor)
}

fn skip_trivia(text: &str, mut cursor: usize) -> Option<usize> {
    let bytes = text.as_bytes();
    loop {
        while bytes.get(cursor).is_some_and(u8::is_ascii_whitespace) {
            cursor += 1;
        }
        if bytes.get(cursor..cursor + 2) == Some(b"//") {
            cursor += 2;
            while bytes.get(cursor).is_some_and(|byte| *byte != b'\n') {
                cursor += 1;
            }
        } else if bytes.get(cursor..cursor + 2) == Some(b"/*") {
            cursor += 2;
            while bytes.get(cursor..cursor + 2) != Some(b"*/") {
                if cursor + 2 >= bytes.len() {
                    return None;
                }
                cursor += 1;
            }
            cursor += 2;
        } else {
            return Some(cursor);
        }
    }
}

fn json_string(text: &str, start: usize) -> Option<(String, usize)> {
    let bytes = text.as_bytes();
    let mut cursor = start + 1;
    while let Some(byte) = bytes.get(cursor) {
        match byte {
            b'\\' => cursor += 2,
            b'"' => {
                let value = serde_json::from_str(&text[start..=cursor]).ok()?;
                return Some((value, cursor + 1));
            }
            _ => cursor += 1,
        }
    }
    None
}

fn skip_json_value(text: &str, start: usize) -> Option<usize> {
    let bytes = text.as_bytes();
    let first = *bytes.get(start)?;
    if first == b'"' {
        return json_string(text, start).map(|(_, end)| end);
    }
    if first != b'{' && first != b'[' {
        let mut cursor = start;
        while let Some(byte) = bytes.get(cursor) {
            if matches!(byte, b',' | b'}' | b']') {
                return Some(cursor);
            }
            cursor += 1;
        }
        return Some(cursor);
    }
    let mut stack = vec![if first == b'{' { b'}' } else { b']' }];
    let mut cursor = start + 1;
    while cursor < bytes.len() {
        cursor = skip_trivia(text, cursor)?;
        match *bytes.get(cursor)? {
            b'"' => cursor = json_string(text, cursor)?.1,
            b'{' => {
                stack.push(b'}');
                cursor += 1;
            }
            b'[' => {
                stack.push(b']');
                cursor += 1;
            }
            b'}' | b']' => {
                if stack.pop()? != bytes[cursor] {
                    return None;
                }
                cursor += 1;
                if stack.is_empty() {
                    return Some(cursor);
                }
            }
            _ => cursor += 1,
        }
    }
    None
}

fn invalid_settings(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message)
}

fn atomic_replace_text(path: &Path, contents: &str) -> io::Result<()> {
    let temporary = path.with_extension(format!("reterm-{}.tmp", std::process::id()));
    let mut file = File::create(&temporary)?;
    if let Err(error) = file
        .write_all(contents.as_bytes())
        .and_then(|()| file.sync_all())
    {
        let _ = fs::remove_file(&temporary);
        return Err(error);
    }
    drop(file);
    replace_file(&temporary, path)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn installer_writes_all_three_apps_into_target_folder() {
        let target =
            std::env::temp_dir().join(format!("reterm-installer-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&target);
        install(
            &target,
            InstallOptions {
                desktop_shortcut: false,
                start_menu_shortcuts: false,
                windows_terminal_profile: false,
            },
            None,
            None,
            None,
        )
        .unwrap();
        for (name, contents) in FILES {
            assert_eq!(fs::read(target.join(name)).unwrap(), contents);
        }
        fs::remove_dir_all(target).unwrap();
    }

    #[test]
    fn terminal_profile_is_added_without_removing_jsonc_comments() {
        let root = std::env::temp_dir().join(format!(
            "reterm-terminal-settings-test-{}",
            std::process::id()
        ));
        fs::create_dir_all(&root).unwrap();
        let settings = root.join("settings.json");
        fs::write(
            &settings,
            concat!(
                "{\n",
                "  // keep this user comment\n",
                "  \"profiles\": {\n",
                "    \"defaults\": { \"font\": { \"size\": 12 } },\n",
                "    \"list\": [\n",
                "      // keep this profile\n",
                "      { \"guid\": \"{11111111-1111-1111-1111-111111111111}\", \"name\": \"Existing\" },\n",
                "    ]\n",
                "  }\n",
                "}\n"
            ),
        )
        .unwrap();

        upsert_terminal_profile(
            &settings,
            Path::new(INSTALL_DIR).join("ReTerm.exe").as_path(),
        )
        .unwrap();
        let mut contents = fs::read_to_string(&settings).unwrap();
        assert!(contents.contains("// keep this user comment"));
        assert!(contents.contains("// keep this profile"));
        assert!(contents.contains(WT_PROFILE_GUID));
        assert!(contents.contains("\"name\": \"ReTerm\""));
        assert!(upsert_terminal_profile(
            &settings,
            Path::new(INSTALL_DIR).join("ReTerm.exe").as_path()
        )
        .is_ok());
        contents = fs::read_to_string(&settings).unwrap();
        assert_eq!(contents.matches(WT_PROFILE_GUID).count(), 1);
        assert_eq!(
            contents
                .matches("{11111111-1111-1111-1111-111111111111}")
                .count(),
            1
        );
        fs::remove_dir_all(root).unwrap();
    }
}
