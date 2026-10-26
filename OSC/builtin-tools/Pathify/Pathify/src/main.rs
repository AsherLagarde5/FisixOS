#[cfg(windows)]
use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, Command};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use clap::{Args, Parser, Subcommand, ValueEnum};
use serde::{Deserialize, Serialize};
use winreg::enums::{
    HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, HKEY_USERS, KEY_READ, KEY_WRITE, REG_EXPAND_SZ,
};
use winreg::{RegKey, RegValue};
use zip::write::FileOptions;
use zip::{CompressionMethod, ZipArchive, ZipWriter};

#[derive(Parser)]
#[command(name = "Pathify", version, about = "Apply named Windows PATH presets")]
struct Cli {
    #[arg(long, global = true, hide = true)]
    gui_confirmed: bool,
    #[command(subcommand)]
    command: Action,
}

static GUI_CONFIRMATION_ALLOWED: AtomicBool = AtomicBool::new(false);

#[derive(Subcommand)]
enum Action {
    #[command(name = "Set", alias = "set")]
    Set(SetArgs),
    #[command(name = "List", alias = "list")]
    List,
    #[command(name = "Init", alias = "init")]
    Init,
    #[command(name = "Restore", alias = "restore")]
    Restore(RestoreArgs),
    #[command(name = "Backups", alias = "backups")]
    Backups,
    #[command(name = "Backup", alias = "backup")]
    Backup(BackupArgs),
    #[command(
        name = "Clean",
        alias = "clean",
        about = "Analyze and safely clean PATH entries"
    )]
    Clean(CleanArgs),
    #[command(name = "Nuke", alias = "nuke", about = "Overwrite and delete one file")]
    Nuke {
        file: PathBuf,
        #[arg(long, value_enum, default_value_t = NukeMode::Shuffle)]
        mode: NukeMode,
    },
    #[command(
        name = "ReReg",
        alias = "rereg",
        about = "Back up Windows registry hives"
    )]
    ReReg,
    #[command(name = "Gui", alias = "gui")]
    Gui,
    #[command(name = "Archive", alias = "archive")]
    Archive {
        #[command(subcommand)]
        command: ArchiveCommand,
    },
    #[command(name = "Preset", alias = "preset")]
    Preset {
        #[command(subcommand)]
        command: PresetCommand,
    },
    #[command(name = "Import", alias = "import")]
    Import(ImportArgs),
    #[command(name = "Export", alias = "export")]
    Export(ExportArgs),
    #[command(name = "Preview", alias = "preview")]
    Preview(PreviewArgs),
    #[command(name = "Validate", alias = "validate")]
    Validate { preset: Option<String> },
    #[command(name = "Doctor", alias = "doctor")]
    Doctor,
    #[command(name = "Launch", alias = "launch")]
    Launch { shell: String },
    #[command(
        name = "Module",
        alias = "module",
        about = "List and run native Pathify modules"
    )]
    Module {
        #[command(subcommand)]
        command: ModuleCommand,
    },
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, ValueEnum)]
enum NukeMode {
    #[default]
    Shuffle,
    Overkill,
}

#[derive(Subcommand)]
enum ModuleCommand {
    #[command(name = "List", alias = "list")]
    List,
    #[command(name = "Run", alias = "run")]
    Run {
        name: String,
        #[arg(allow_hyphen_values = true, trailing_var_arg = true)]
        arguments: Vec<String>,
    },
}

#[derive(Subcommand)]
enum ArchiveCommand {
    #[command(name = "Create", alias = "create")]
    Create {
        folder: Option<PathBuf>,
        #[arg(long, value_enum, default_value_t = ArchiveMode::Max)]
        mode: ArchiveMode,
    },
    #[command(name = "Info", alias = "info")]
    Info {
        archive: PathBuf,
        #[arg(long, value_enum)]
        mode: Option<ArchiveMode>,
    },
    #[command(name = "Restore", alias = "restore")]
    Restore {
        archive: PathBuf,
        destination: PathBuf,
        #[arg(long)]
        shortcut: Option<PathBuf>,
    },
    #[command(name = "Delete", alias = "delete")]
    Delete {
        archive: PathBuf,
        #[arg(long)]
        shortcut: Option<PathBuf>,
    },
    #[command(name = "Menu", alias = "menu")]
    Menu {
        archive: PathBuf,
        original: PathBuf,
        #[arg(long, value_enum)]
        mode: ArchiveMode,
    },
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, ValueEnum)]
enum ArchiveMode {
    Fast,
    Balanced,
    #[default]
    Max,
}

#[derive(Subcommand)]
enum PresetCommand {
    #[command(name = "Create", alias = "create")]
    Create(CreatePresetArgs),
    #[command(name = "Edit", alias = "edit")]
    Edit(EditPresetArgs),
    #[command(name = "Rename", alias = "rename")]
    Rename(RenamePresetArgs),
    #[command(name = "Remove", alias = "remove")]
    Remove(RemovePresetArgs),
}

#[derive(Args)]
struct SetArgs {
    preset: String,
    #[arg(short = 'f', long)]
    force: bool,
    #[arg(short = 's', long)]
    system: bool,
    #[arg(short = 'r', long)]
    restart: bool,
    #[arg(long = "force-restart")]
    force_restart: bool,
    #[arg(long = "restart-system", conflicts_with = "force_restart_system")]
    restart_system: bool,
    #[arg(long = "force-restart-system", conflicts_with = "restart_system")]
    force_restart_system: bool,
    #[arg(long = "restart-explorer")]
    restart_explorer: bool,
    #[arg(long = "restart-display-drivers")]
    restart_display_drivers: bool,
    #[arg(long = "reconnect-internet")]
    reconnect_internet: bool,
    #[arg(short = 'b', long)]
    backup: bool,
}

#[derive(Args)]
struct RestoreArgs {
    #[arg(long, conflicts_with = "backup")]
    latest: bool,
    #[arg(long, value_name = "FILE")]
    backup: Option<String>,
    #[arg(short = 's', long, conflicts_with = "backup")]
    system: bool,
    #[arg(short = 'f', long)]
    force: bool,
}

#[derive(Args)]
struct BackupArgs {
    #[arg(short = 's', long)]
    system: bool,
}

#[derive(Args)]
struct CleanArgs {
    #[arg(long, help = "Apply selected cleanup after showing the analysis")]
    apply: bool,
    #[arg(
        short = 's',
        long,
        conflicts_with = "preset",
        help = "Clean the system-wide PATH"
    )]
    system: bool,
    #[arg(
        long,
        conflicts_with = "system",
        help = "Analyze or clean a saved preset"
    )]
    preset: Option<String>,
    #[arg(
        long,
        conflicts_with = "preset",
        help = "Allow removing PATH entries referenced by saved presets"
    )]
    include_presets: bool,
    #[arg(long, help = "Remove missing, unresolved, or non-directory entries")]
    remove_broken: bool,
    #[arg(
        long,
        help = "Remove later entries resolving to a directory already in PATH"
    )]
    remove_shadowed: bool,
}

#[derive(Args)]
struct CreatePresetArgs {
    name: String,
    #[arg(required = true, num_args = 1..)]
    entries: Vec<String>,
}

#[derive(Args)]
struct EditPresetArgs {
    name: String,
    #[arg(required = true, num_args = 1..)]
    entries: Vec<String>,
    #[arg(short = 'f', long)]
    force: bool,
}

#[derive(Args)]
struct RenamePresetArgs {
    name: String,
    new_name: String,
}

#[derive(Args)]
struct RemovePresetArgs {
    name: String,
    #[arg(short = 'f', long)]
    force: bool,
}

#[derive(Args)]
struct ImportArgs {
    file: PathBuf,
    #[arg(short = 'f', long)]
    force: bool,
}

#[derive(Args)]
struct ExportArgs {
    file: PathBuf,
    #[arg(short = 'f', long)]
    force: bool,
}

#[derive(Args)]
struct PreviewArgs {
    preset: String,
    #[arg(short = 's', long)]
    system: bool,
}

#[derive(Serialize, Deserialize)]
struct PresetFile {
    presets: BTreeMap<String, Vec<String>>,
}

#[derive(Default, Serialize, Deserialize)]
struct ManagedSessions {
    sessions: Vec<ManagedSession>,
}

#[derive(Clone, Serialize, Deserialize)]
struct ManagedSession {
    pid: u32,
    executable: String,
    #[serde(default)]
    creation_time: u64,
}

struct ShellIdentity {
    pid: u32,
    executable: PathBuf,
    creation_time: u64,
}

fn main() {
    let arguments = std::env::args_os()
        .map(|argument| match argument.to_str() {
            Some("-frs") => OsString::from("--force-restart-system"),
            Some("-fr") => OsString::from("--force-restart"),
            Some("-rs") => OsString::from("--restart-system"),
            Some("-rse") => OsString::from("--restart-explorer"),
            Some("-ddr") => OsString::from("--restart-display-drivers"),
            Some("-ri") => OsString::from("--reconnect-internet"),
            Some("-help") => OsString::from("--help"),
            _ => argument,
        })
        .collect::<Vec<_>>();
    if is_george_fell_request(&arguments) {
        println!("george fell in and drowned himself");
        return;
    }
    if is_george_robot_request(&arguments) {
        println!("GEORGE THE ROBOT VACUME GOT VIOLENTLY MURDERED BY THE EVIL ALEXA");
        return;
    }
    if is_george_request(&arguments) {
        println!("thats not a command, trust me bro.");
        return;
    }
    if is_motherboard_request(&arguments) {
        if let Err(error) = run_motherboard_visual() {
            eprintln!("Pathify: {error}");
            std::process::exit(1);
        }
        return;
    }
    match dispatch_native_module(&arguments) {
        Ok(true) => return,
        Ok(false) => {}
        Err(error) => {
            eprintln!("Pathify: {error}");
            std::process::exit(1);
        }
    }
    if let Err(error) = run(Cli::parse_from(arguments)) {
        eprintln!("Pathify: {error}");
        std::process::exit(1);
    }
}

fn dispatch_native_module(arguments: &[OsString]) -> Result<bool, Box<dyn std::error::Error>> {
    let Some(command) = arguments.get(1).and_then(|argument| argument.to_str()) else {
        return Ok(false);
    };
    if is_builtin_command(command) || command.starts_with('-') {
        return Ok(false);
    }

    let modules = discover_native_modules()?;
    let Some(module) = modules
        .iter()
        .find(|module| module.manifest.name.eq_ignore_ascii_case(command))
    else {
        return Ok(false);
    };
    let module_arguments = arguments
        .iter()
        .skip(2)
        .map(|argument| {
            argument
                .to_str()
                .map(str::to_owned)
                .ok_or("module arguments must be valid Unicode")
        })
        .collect::<Result<Vec<_>, _>>()?;
    run_native_module(module, &module_arguments)?;
    Ok(true)
}

fn is_builtin_command(command: &str) -> bool {
    const BUILT_INS: &[&str] = &[
        "set",
        "list",
        "init",
        "restore",
        "backups",
        "backup",
        "clean",
        "nuke",
        "rereg",
        "gui",
        "archive",
        "preset",
        "import",
        "export",
        "preview",
        "validate",
        "doctor",
        "launch",
        "module",
        "help",
        "--help",
        "-h",
        "--version",
        "-v",
    ];
    BUILT_INS.contains(&command.to_ascii_lowercase().as_str())
}

#[derive(Clone, Deserialize)]
#[serde(deny_unknown_fields)]
struct NativeModuleManifest {
    name: String,
    version: String,
    description: String,
    dll: String,
    abi_version: u32,
}

struct NativeModule {
    manifest: NativeModuleManifest,
    dll_path: PathBuf,
}

fn native_module_directories() -> Result<Vec<PathBuf>, Box<dyn std::error::Error>> {
    let executable = std::env::current_exe()?;
    let install_directory = executable
        .parent()
        .ok_or("Could not determine the Pathify executable directory")?;
    let mut directories = vec![install_directory.join("modules")];
    if let Some(app_data) = std::env::var_os("APPDATA") {
        directories.push(PathBuf::from(app_data).join("Pathify").join("modules"));
    }
    Ok(directories)
}

fn discover_native_modules() -> Result<Vec<NativeModule>, Box<dyn std::error::Error>> {
    let mut modules = Vec::new();
    let mut names = BTreeSet::new();
    for root in native_module_directories()? {
        let entries = match fs::read_dir(&root) {
            Ok(entries) => entries,
            Err(error) if error.kind() == io::ErrorKind::NotFound => continue,
            Err(error) => {
                return Err(format!(
                    "Could not read module directory {}: {error}",
                    root.display()
                )
                .into());
            }
        };
        for entry in entries {
            let entry = entry?;
            if !entry.file_type()?.is_dir() {
                continue;
            }
            let directory = entry.path();
            let manifest_path = directory.join("module.json");
            if !manifest_path.is_file() {
                continue;
            }
            let manifest: NativeModuleManifest =
                serde_json::from_reader(fs::File::open(&manifest_path).map_err(|error| {
                    format!(
                        "Could not open module manifest {}: {error}",
                        manifest_path.display()
                    )
                })?)
                .map_err(|error| {
                    format!(
                        "Invalid module manifest {}: {error}",
                        manifest_path.display()
                    )
                })?;
            validate_native_module_manifest(&manifest, &directory)?;
            if !names.insert(manifest.name.to_ascii_lowercase()) {
                return Err(format!(
                    "Duplicate native module '{}'; keep one copy in either the install modules folder or %APPDATA%\\Pathify\\modules.",
                    manifest.name
                )
                .into());
            }
            let dll_path = directory.join(&manifest.dll);
            let dll_metadata = fs::symlink_metadata(&dll_path).map_err(|error| {
                format!(
                    "Module '{}' DLL is unavailable at {}: {error}",
                    manifest.name,
                    dll_path.display()
                )
            })?;
            if !dll_metadata.file_type().is_file() {
                return Err(format!(
                    "Module '{}' DLL must be a regular file: {}",
                    manifest.name,
                    dll_path.display()
                )
                .into());
            }
            let canonical_directory = directory.canonicalize()?;
            let canonical_dll = dll_path.canonicalize()?;
            if !canonical_dll.starts_with(&canonical_directory) {
                return Err(format!(
                    "Module '{}' DLL must remain inside its module folder.",
                    manifest.name
                )
                .into());
            }
            modules.push(NativeModule {
                manifest,
                dll_path: canonical_dll,
            });
        }
    }
    modules.sort_by(|left, right| {
        left.manifest
            .name
            .to_ascii_lowercase()
            .cmp(&right.manifest.name.to_ascii_lowercase())
    });
    Ok(modules)
}

fn validate_native_module_manifest(
    manifest: &NativeModuleManifest,
    directory: &Path,
) -> Result<(), Box<dyn std::error::Error>> {
    let name = &manifest.name;
    let valid_name = !name.is_empty()
        && name.len() <= 48
        && name
            .bytes()
            .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'-')
        && name.as_bytes()[0].is_ascii_lowercase()
        && !is_builtin_command(name);
    if !valid_name {
        return Err(format!(
            "Invalid module name '{}' in {}. Names must start with a lowercase letter, contain only lowercase letters, digits, and hyphens, and not shadow a built-in command.",
            name,
            directory.display()
        )
        .into());
    }
    if directory.file_name().and_then(|value| value.to_str()) != Some(name.as_str()) {
        return Err(format!(
            "Module folder '{}' must match the manifest name '{}'.",
            directory.display(),
            name
        )
        .into());
    }
    if manifest.abi_version != 1 {
        return Err(format!(
            "Module '{}' uses unsupported ABI version {}; supported version is 1.",
            name, manifest.abi_version
        )
        .into());
    }
    if manifest.version.trim().is_empty()
        || manifest.description.trim().is_empty()
        || manifest.version.chars().any(char::is_control)
        || manifest.description.chars().any(char::is_control)
    {
        return Err(format!("Module '{}' must declare a version and description.", name).into());
    }
    if manifest.dll != "module.dll" {
        return Err(format!("Module '{}' DLL must be named module.dll.", name).into());
    }
    Ok(())
}

fn find_native_module(name: &str) -> Result<NativeModule, Box<dyn std::error::Error>> {
    discover_native_modules()?
        .into_iter()
        .find(|module| module.manifest.name.eq_ignore_ascii_case(name))
        .ok_or_else(|| {
            format!("Native module '{name}' was not found. Run `Pathify Module List`.").into()
        })
}

fn list_native_modules() -> Result<(), Box<dyn std::error::Error>> {
    let modules = discover_native_modules()?;
    if modules.is_empty() {
        println!(
            "No native modules installed. See the modules/examples folder for Go and Rust samples."
        );
        return Ok(());
    }
    for module in modules {
        println!(
            "{}\t{}\t{}\t{}",
            module.manifest.name,
            module.manifest.version,
            module.manifest.description,
            module.dll_path.display()
        );
    }
    Ok(())
}

#[cfg(windows)]
fn run_native_module(
    module: &NativeModule,
    arguments: &[String],
) -> Result<(), Box<dyn std::error::Error>> {
    use std::ffi::{CStr, c_char, c_void};
    use std::os::windows::ffi::OsStrExt;

    type AbiVersion = unsafe extern "system" fn() -> u32;
    type ModuleText = unsafe extern "system" fn() -> *const c_char;
    type ModuleRun = unsafe extern "system" fn(*const u8, u32, *mut u8, u32) -> i32;

    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn LoadLibraryW(filename: *const u16) -> *mut c_void;
        fn GetProcAddress(module: *mut c_void, name: *const c_char) -> *mut c_void;
    }

    unsafe fn module_function<T: Copy>(
        library: *mut c_void,
        name: &'static [u8],
        module_name: &str,
    ) -> Result<T, Box<dyn std::error::Error>> {
        let pointer = unsafe { GetProcAddress(library, name.as_ptr().cast()) };
        if pointer.is_null() {
            let export = String::from_utf8_lossy(&name[..name.len().saturating_sub(1)]);
            return Err(
                format!("Module '{module_name}' is missing required export {export}.").into(),
            );
        }
        Ok(unsafe { std::mem::transmute_copy(&pointer) })
    }

    unsafe fn module_text(
        library: *mut c_void,
        name: &'static [u8],
        module_name: &str,
    ) -> Result<String, Box<dyn std::error::Error>> {
        let function: ModuleText = unsafe { module_function(library, name, module_name)? };
        let pointer = unsafe { function() };
        if pointer.is_null() {
            return Err(format!("Module '{module_name}' returned null metadata.").into());
        }
        Ok(unsafe { CStr::from_ptr(pointer) }
            .to_str()
            .map_err(|error| {
                format!("Module '{module_name}' returned invalid UTF-8 metadata: {error}")
            })?
            .to_owned())
    }

    let module_name = module.manifest.name.as_str();
    let path: Vec<u16> = module
        .dll_path
        .as_os_str()
        .encode_wide()
        .chain(Some(0))
        .collect();
    let handle = unsafe { LoadLibraryW(path.as_ptr()) };
    if handle.is_null() {
        let error = io::Error::last_os_error();
        return Err(format!(
            "Could not load module '{module_name}' ({}): {error}",
            module.dll_path.display()
        )
        .into());
    }
    // Keep native modules loaded until process exit; runtimes such as Go cannot be safely unloaded.
    let library = handle;

    let abi: AbiVersion =
        unsafe { module_function(library, b"PathifyModuleAbiVersion\0", module_name)? };
    let actual_abi = unsafe { abi() };
    if actual_abi != module.manifest.abi_version {
        return Err(format!(
            "Module '{module_name}' manifest ABI {} does not match DLL ABI {}.",
            module.manifest.abi_version, actual_abi
        )
        .into());
    }
    let actual_name = unsafe { module_text(library, b"PathifyModuleName\0", module_name)? };
    if !actual_name.eq_ignore_ascii_case(module_name) {
        return Err(format!(
            "Module folder is named '{module_name}', but its DLL identifies itself as '{actual_name}'."
        )
        .into());
    }
    let actual_description =
        unsafe { module_text(library, b"PathifyModuleDescription\0", module_name)? };
    if actual_description != module.manifest.description {
        return Err(
            format!("Module '{module_name}' manifest description does not match the DLL.").into(),
        );
    }

    let request = serde_json::to_vec(arguments)?;
    let request_length =
        u32::try_from(request.len()).map_err(|_| "Module arguments are too large")?;
    const MAX_MODULE_OUTPUT: usize = 4 * 1024 * 1024;
    let mut output = vec![0u8; MAX_MODULE_OUTPUT];
    let run: ModuleRun = unsafe { module_function(library, b"PathifyModuleRun\0", module_name)? };
    let result = unsafe {
        run(
            request.as_ptr(),
            request_length,
            output.as_mut_ptr(),
            output.len() as u32,
        )
    };
    if result < 0 {
        let message = output
            .iter()
            .position(|byte| *byte == 0)
            .map(|length| String::from_utf8_lossy(&output[..length]).into_owned())
            .unwrap_or_else(|| "module reported a failure".to_owned());
        return Err(format!("Module '{module_name}' failed: {message}").into());
    }
    let output_length = result as usize;
    if output_length > output.len() {
        return Err(format!(
            "Module '{module_name}' reported invalid output length {output_length}."
        )
        .into());
    }
    let result = String::from_utf8(output[..output_length].to_vec())
        .map_err(|error| format!("Module '{module_name}' output was not valid UTF-8: {error}"))?;
    if !result.is_empty() {
        print!("{result}");
        if !result.ends_with('\n') {
            println!();
        }
    }
    Ok(())
}

#[cfg(not(windows))]
fn run_native_module(
    module: &NativeModule,
    _arguments: &[String],
) -> Result<(), Box<dyn std::error::Error>> {
    Err(format!(
        "Native Pathify DLL modules are supported only on Windows (module '{}').",
        module.manifest.name
    )
    .into())
}

fn is_george_fell_request(arguments: &[OsString]) -> bool {
    arguments.len() == 3
        && arguments[1]
            .to_str()
            .is_some_and(|argument| argument.eq_ignore_ascii_case("george"))
        && arguments[2]
            .to_str()
            .is_some_and(|argument| argument.eq_ignore_ascii_case("fell"))
}

fn is_george_robot_request(arguments: &[OsString]) -> bool {
    arguments.len() == 3
        && arguments[1]
            .to_str()
            .is_some_and(|argument| argument.eq_ignore_ascii_case("george"))
        && arguments[2]
            .to_str()
            .is_some_and(|argument| argument.eq_ignore_ascii_case("robot"))
}

fn is_george_request(arguments: &[OsString]) -> bool {
    arguments.len() == 2
        && arguments[1]
            .to_str()
            .is_some_and(|argument| argument.eq_ignore_ascii_case("george"))
}

fn is_motherboard_request(arguments: &[OsString]) -> bool {
    const PHRASE: [&str; 6] = ["my", "dog", "just", "ate", "the", "motherboard"];

    arguments.len() == PHRASE.len() + 1
        && arguments
            .iter()
            .skip(1)
            .zip(PHRASE)
            .all(|(argument, expected)| {
                argument
                    .to_str()
                    .is_some_and(|argument| argument.eq_ignore_ascii_case(expected))
            })
}

fn run_motherboard_visual() -> Result<(), Box<dyn std::error::Error>> {
    eprintln!(
        "This starts a slow, windowed moving-graphics effect (not full-screen or flashing), for up to 10 minutes."
    );
    eprintln!("Cancel if you are sensitive to moving visuals.");
    eprintln!("Close the window or press Insert+Esc at any time to stop it.");
    print!("Type Y to continue: ");
    io::stdout().flush()?;
    let mut answer = String::new();
    io::stdin().read_line(&mut answer)?;
    if !answer.trim().eq_ignore_ascii_case("y") {
        println!("Canceled.");
        return Ok(());
    }

    run_motherboard_visual_window()
}

#[cfg(windows)]
const VISUAL_MAX_RUNTIME: Duration = Duration::from_secs(10 * 60);

#[cfg(windows)]
const VISUAL_TIMER_ID: usize = 1;

#[cfg(windows)]
const VISUAL_TIMER_INTERVAL_MS: u32 = 250;

#[cfg(windows)]
const VISUAL_CIRCLE_RADIUS: i32 = 36;

#[cfg(windows)]
struct VisualState {
    window: windows_sys::Win32::Foundation::HWND,
    insert_down: bool,
    escape_down: bool,
    stop_posted: bool,
    x: i32,
    y: i32,
    dx: i32,
    dy: i32,
    started: Instant,
    background: windows_sys::Win32::Graphics::Gdi::HBRUSH,
    circle: windows_sys::Win32::Graphics::Gdi::HBRUSH,
    pen: windows_sys::Win32::Graphics::Gdi::HPEN,
}

#[cfg(windows)]
thread_local! {
    static VISUAL_STATE: RefCell<Option<VisualState>> = const { RefCell::new(None) };
}

#[cfg(windows)]
struct VisualGdiResources {
    background: windows_sys::Win32::Graphics::Gdi::HBRUSH,
    circle: windows_sys::Win32::Graphics::Gdi::HBRUSH,
    pen: windows_sys::Win32::Graphics::Gdi::HPEN,
}

#[cfg(windows)]
impl VisualGdiResources {
    fn new() -> io::Result<Self> {
        use windows_sys::Win32::Graphics::Gdi::{
            CreatePen, CreateSolidBrush, DeleteObject, PS_SOLID,
        };

        let background = unsafe { CreateSolidBrush(rgb(37, 48, 59)) };
        if background.is_null() {
            return Err(io::Error::last_os_error());
        }
        let circle = unsafe { CreateSolidBrush(rgb(151, 190, 174)) };
        if circle.is_null() {
            let error = io::Error::last_os_error();
            unsafe { DeleteObject(background as _) };
            return Err(error);
        }
        let pen = unsafe { CreatePen(PS_SOLID, 2, rgb(104, 140, 129)) };
        if pen.is_null() {
            let error = io::Error::last_os_error();
            unsafe {
                DeleteObject(background as _);
                DeleteObject(circle as _);
            }
            return Err(error);
        }
        Ok(Self {
            background,
            circle,
            pen,
        })
    }
}

#[cfg(windows)]
impl Drop for VisualGdiResources {
    fn drop(&mut self) {
        use windows_sys::Win32::Graphics::Gdi::DeleteObject;

        unsafe {
            DeleteObject(self.background as _);
            DeleteObject(self.circle as _);
            DeleteObject(self.pen as _);
        }
    }
}

#[cfg(windows)]
struct KeyboardHook(windows_sys::Win32::UI::WindowsAndMessaging::HHOOK);

#[cfg(windows)]
impl Drop for KeyboardHook {
    fn drop(&mut self) {
        unsafe {
            windows_sys::Win32::UI::WindowsAndMessaging::UnhookWindowsHookEx(self.0);
        }
    }
}

#[cfg(windows)]
struct RegisteredWindowClass {
    instance: windows_sys::Win32::Foundation::HINSTANCE,
    name: Vec<u16>,
}

#[cfg(windows)]
impl Drop for RegisteredWindowClass {
    fn drop(&mut self) {
        unsafe {
            windows_sys::Win32::UI::WindowsAndMessaging::UnregisterClassW(
                self.name.as_ptr(),
                self.instance,
            );
        }
    }
}

#[cfg(windows)]
fn rgb(red: u8, green: u8, blue: u8) -> u32 {
    u32::from(red) | (u32::from(green) << 8) | (u32::from(blue) << 16)
}

#[cfg(windows)]
fn run_motherboard_visual_window() -> Result<(), Box<dyn std::error::Error>> {
    use std::ptr::{null, null_mut};
    use windows_sys::Win32::Graphics::Gdi::UpdateWindow;
    use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        CS_HREDRAW, CS_VREDRAW, CW_USEDEFAULT, CreateWindowExW, DestroyWindow, DispatchMessageW,
        GetMessageW, IsWindow, MSG, RegisterClassW, SW_SHOW, SetTimer, SetWindowsHookExW,
        ShowWindow, TranslateMessage, WH_KEYBOARD_LL, WM_QUIT, WNDCLASSW, WS_OVERLAPPEDWINDOW,
    };

    let resources = VisualGdiResources::new()?;
    let instance = unsafe { GetModuleHandleW(null()) };
    if instance.is_null() {
        return Err(io::Error::last_os_error().into());
    }

    let initial_state = VisualState {
        window: null_mut(),
        insert_down: false,
        escape_down: false,
        stop_posted: false,
        x: 80,
        y: 100,
        dx: 3,
        dy: 2,
        started: Instant::now(),
        background: resources.background,
        circle: resources.circle,
        pen: resources.pen,
    };
    let thread_id = unsafe { windows_sys::Win32::System::Threading::GetCurrentThreadId() };
    VISUAL_STATE.with(|state| *state.borrow_mut() = Some(initial_state));

    let hook =
        unsafe { SetWindowsHookExW(WH_KEYBOARD_LL, Some(motherboard_keyboard_hook), instance, 0) };
    if hook.is_null() {
        VISUAL_STATE.with(|state| *state.borrow_mut() = None);
        return Err(io::Error::new(
            io::ErrorKind::Other,
            format!(
                "could not register the Insert+Esc emergency stop (thread {thread_id}); no effect was started: {}",
                io::Error::last_os_error()
            ),
        )
        .into());
    }
    let _hook = KeyboardHook(hook);

    let class_name: Vec<u16> = "PathifySlowVisualEffect"
        .encode_utf16()
        .chain(Some(0))
        .collect();
    let title: Vec<u16> = "Pathify slow visual effect - close window or press Insert+Esc"
        .encode_utf16()
        .chain(Some(0))
        .collect();
    let window_class = WNDCLASSW {
        style: CS_HREDRAW | CS_VREDRAW,
        lpfnWndProc: Some(motherboard_window_proc),
        cbClsExtra: 0,
        cbWndExtra: 0,
        hInstance: instance,
        hIcon: null_mut(),
        hCursor: null_mut(),
        hbrBackground: resources.background,
        lpszMenuName: null(),
        lpszClassName: class_name.as_ptr(),
    };
    if unsafe { RegisterClassW(&window_class) } == 0 {
        VISUAL_STATE.with(|state| *state.borrow_mut() = None);
        return Err(io::Error::last_os_error().into());
    }
    let _window_class = RegisteredWindowClass {
        instance,
        name: class_name.clone(),
    };

    let window = unsafe {
        CreateWindowExW(
            0,
            class_name.as_ptr(),
            title.as_ptr(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            900,
            620,
            null_mut(),
            null_mut(),
            instance,
            null(),
        )
    };
    if window.is_null() {
        VISUAL_STATE.with(|state| *state.borrow_mut() = None);
        return Err(io::Error::last_os_error().into());
    }
    VISUAL_STATE.with(|state| {
        if let Some(state) = state.borrow_mut().as_mut() {
            state.window = window;
        }
    });

    if unsafe { SetTimer(window, VISUAL_TIMER_ID, VISUAL_TIMER_INTERVAL_MS, None) } == 0 {
        unsafe { DestroyWindow(window) };
        VISUAL_STATE.with(|state| *state.borrow_mut() = None);
        return Err(io::Error::last_os_error().into());
    }
    unsafe {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }

    let mut message = MSG {
        hwnd: null_mut(),
        message: 0,
        wParam: 0,
        lParam: 0,
        time: 0,
        pt: windows_sys::Win32::Foundation::POINT { x: 0, y: 0 },
    };
    let mut message_error = None;
    loop {
        let result = unsafe { GetMessageW(&mut message, null_mut(), 0, 0) };
        if result == -1 {
            message_error = Some(io::Error::last_os_error());
            break;
        }
        if result == 0 || message.message == WM_QUIT {
            break;
        }
        unsafe {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if unsafe { IsWindow(window) } != 0 {
        unsafe { DestroyWindow(window) };
    }
    VISUAL_STATE.with(|state| *state.borrow_mut() = None);
    if let Some(error) = message_error {
        return Err(error.into());
    }
    println!("Visual effect stopped.");
    Ok(())
}

#[cfg(not(windows))]
fn run_motherboard_visual_window() -> Result<(), Box<dyn std::error::Error>> {
    Err("the visual effect is supported only on Windows".into())
}

#[cfg(windows)]
unsafe extern "system" fn motherboard_keyboard_hook(
    code: i32,
    wparam: windows_sys::Win32::Foundation::WPARAM,
    lparam: windows_sys::Win32::Foundation::LPARAM,
) -> windows_sys::Win32::Foundation::LRESULT {
    use std::ptr::null_mut;
    use windows_sys::Win32::UI::Input::KeyboardAndMouse::{VK_ESCAPE, VK_INSERT};
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        CallNextHookEx, HC_ACTION, KBDLLHOOKSTRUCT, PostMessageW, PostQuitMessage, WM_CLOSE,
        WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP,
    };

    if code == HC_ACTION as i32 && lparam != 0 {
        let key = unsafe { *(lparam as *const KBDLLHOOKSTRUCT) };
        let down = matches!(wparam as u32, WM_KEYDOWN | WM_SYSKEYDOWN);
        let up = matches!(wparam as u32, WM_KEYUP | WM_SYSKEYUP);
        if down || up {
            VISUAL_STATE.with(|state| {
                if let Some(state) = state.borrow_mut().as_mut() {
                    let pressed = down;
                    if key.vkCode == u32::from(VK_INSERT) {
                        state.insert_down = pressed;
                    } else if key.vkCode == u32::from(VK_ESCAPE) {
                        state.escape_down = pressed;
                    }
                    if pressed && state.insert_down && state.escape_down && !state.stop_posted {
                        state.stop_posted = true;
                        if !state.window.is_null() {
                            let posted = unsafe { PostMessageW(state.window, WM_CLOSE, 0, 0) };
                            if posted == 0 {
                                unsafe { PostQuitMessage(0) };
                            }
                        }
                    }
                }
            });
        }
    }

    unsafe { CallNextHookEx(null_mut(), code, wparam, lparam) }
}

#[cfg(windows)]
unsafe extern "system" fn motherboard_window_proc(
    window: windows_sys::Win32::Foundation::HWND,
    message: u32,
    wparam: windows_sys::Win32::Foundation::WPARAM,
    lparam: windows_sys::Win32::Foundation::LPARAM,
) -> windows_sys::Win32::Foundation::LRESULT {
    use std::ptr::null_mut;
    use windows_sys::Win32::Foundation::RECT;
    use windows_sys::Win32::Graphics::Gdi::{
        BeginPaint, Ellipse, EndPaint, FillRect, InvalidateRect, PAINTSTRUCT, SelectObject,
    };
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        DefWindowProcW, GetClientRect, KillTimer, PostMessageW, PostQuitMessage, WM_CLOSE,
        WM_DESTROY, WM_ERASEBKGND, WM_PAINT, WM_TIMER,
    };

    match message {
        WM_PAINT => {
            let mut paint = PAINTSTRUCT {
                hdc: null_mut(),
                fErase: 0,
                rcPaint: RECT {
                    left: 0,
                    top: 0,
                    right: 0,
                    bottom: 0,
                },
                fRestore: 0,
                fIncUpdate: 0,
                rgbReserved: [0; 32],
            };
            let dc = unsafe { BeginPaint(window, &mut paint) };
            VISUAL_STATE.with(|state| {
                if let Some(state) = state.borrow().as_ref() {
                    unsafe {
                        FillRect(dc, &paint.rcPaint, state.background);
                        let old_brush = SelectObject(dc, state.circle as _);
                        let old_pen = SelectObject(dc, state.pen as _);
                        Ellipse(
                            dc,
                            state.x,
                            state.y,
                            state.x + VISUAL_CIRCLE_RADIUS * 2,
                            state.y + VISUAL_CIRCLE_RADIUS * 2,
                        );
                        SelectObject(dc, old_brush);
                        SelectObject(dc, old_pen);
                    }
                }
            });
            unsafe { EndPaint(window, &paint) };
            0
        }
        WM_ERASEBKGND => 1,
        WM_TIMER if wparam == VISUAL_TIMER_ID => {
            let mut close_window = false;
            VISUAL_STATE.with(|state| {
                if let Some(state) = state.borrow_mut().as_mut() {
                    if state.started.elapsed() >= VISUAL_MAX_RUNTIME {
                        close_window = true;
                        return;
                    }
                    let mut bounds = RECT {
                        left: 0,
                        top: 0,
                        right: 0,
                        bottom: 0,
                    };
                    if unsafe { GetClientRect(window, &mut bounds) } == 0 {
                        close_window = true;
                        return;
                    }
                    let old_x = state.x;
                    let old_y = state.y;
                    let max_x = (bounds.right - VISUAL_CIRCLE_RADIUS * 2).max(0);
                    let max_y = (bounds.bottom - VISUAL_CIRCLE_RADIUS * 2).max(0);
                    state.x += state.dx;
                    state.y += state.dy;
                    if state.x < 0 || state.x > max_x {
                        state.dx = -state.dx;
                        state.x = state.x.clamp(0, max_x);
                    }
                    if state.y < 0 || state.y > max_y {
                        state.dy = -state.dy;
                        state.y = state.y.clamp(0, max_y);
                    }
                    let dirty = RECT {
                        left: old_x.min(state.x) - 2,
                        top: old_y.min(state.y) - 2,
                        right: old_x.max(state.x) + VISUAL_CIRCLE_RADIUS * 2 + 2,
                        bottom: old_y.max(state.y) + VISUAL_CIRCLE_RADIUS * 2 + 2,
                    };
                    unsafe { InvalidateRect(window, &dirty, 0) };
                }
            });
            if close_window {
                unsafe { PostMessageW(window, WM_CLOSE, 0, 0) };
            }
            0
        }
        WM_CLOSE => unsafe { DefWindowProcW(window, message, wparam, lparam) },
        WM_DESTROY => {
            unsafe {
                KillTimer(window, VISUAL_TIMER_ID);
                PostQuitMessage(0);
            }
            0
        }
        _ => unsafe { DefWindowProcW(window, message, wparam, lparam) },
    }
}

fn run(cli: Cli) -> Result<(), Box<dyn std::error::Error>> {
    GUI_CONFIRMATION_ALLOWED.store(cli.gui_confirmed, Ordering::Relaxed);
    match cli.command {
        Action::Set(args) => set_preset(args),
        Action::List => list_presets(),
        Action::Init => {
            let path = config_path()?;
            write_default_config(&path)?;
            println!("Created {}", path.display());
            Ok(())
        }
        Action::Restore(args) => restore_latest(args),
        Action::Backups => list_backups(),
        Action::Backup(args) => backup_current_path(args.system),
        Action::Clean(args) => clean_path(args),
        Action::Nuke { file, mode } => nuke_file(&file, mode),
        Action::ReReg => backup_registry(),
        Action::Gui => gui::run_gui(),
        Action::Archive { command } => run_archive_command(command),
        Action::Preset { command } => manage_preset(command),
        Action::Import(args) => import_presets(args),
        Action::Export(args) => export_presets(args),
        Action::Preview(args) => preview_preset(args),
        Action::Validate { preset } => validate_presets(preset.as_deref()),
        Action::Doctor => doctor(),
        Action::Launch { shell } => launch_shell(&shell),
        Action::Module { command } => match command {
            ModuleCommand::List => list_native_modules(),
            ModuleCommand::Run { name, arguments } => {
                let module = find_native_module(&name)?;
                run_native_module(&module, &arguments)
            }
        },
    }
}

#[derive(Debug, Eq, PartialEq)]
struct ArchiveEntrySnapshot {
    relative: PathBuf,
    is_directory: bool,
    length: u64,
    modified: SystemTime,
}

impl ArchiveMode {
    fn compression(self) -> (CompressionMethod, Option<i32>) {
        match self {
            Self::Fast => (CompressionMethod::Stored, None),
            Self::Balanced => (CompressionMethod::Deflated, Some(6)),
            Self::Max => (CompressionMethod::Deflated, Some(9)),
        }
    }

    fn label(self) -> &'static str {
        match self {
            Self::Fast => "Fast (Store)",
            Self::Balanced => "Balanced (Deflate 6)",
            Self::Max => "Max (Deflate 9)",
        }
    }
}

fn run_archive_command(command: ArchiveCommand) -> Result<(), Box<dyn std::error::Error>> {
    match command {
        ArchiveCommand::Create { folder, mode } => match folder {
            Some(folder) => create_folder_archive(&folder, mode),
            None => show_archive_create_picker(),
        },
        ArchiveCommand::Info { archive, mode } => print_archive_info(&archive, mode),
        ArchiveCommand::Restore {
            archive,
            destination,
            shortcut,
        } => restore_folder_archive(&archive, &destination, shortcut.as_deref()),
        ArchiveCommand::Delete { archive, shortcut } => {
            delete_folder_archive(&archive, shortcut.as_deref())
        }
        ArchiveCommand::Menu {
            archive,
            original,
            mode,
        } => show_archive_menu(&archive, &original, mode),
    }
}

fn create_folder_archive(
    folder: &Path,
    mode: ArchiveMode,
) -> Result<(), Box<dyn std::error::Error>> {
    let source = folder.canonicalize()?;
    if !source.is_dir() {
        return Err(format!("Not a folder: {}", source.display()).into());
    }
    let folder_name = source
        .file_name()
        .and_then(|name| name.to_str())
        .filter(|name| !name.is_empty())
        .ok_or("The selected folder must have a valid Windows-compatible name")?;
    let executable = std::env::current_exe()?;
    let install_directory = executable
        .parent()
        .ok_or("Could not determine the Pathify install directory")?
        .canonicalize()?;
    if install_directory.starts_with(&source) {
        return Err(
            "Refusing to archive a folder that contains Pathify's install directory.".into(),
        );
    }
    let archive_directory = install_directory.join("archive");
    if source.starts_with(&archive_directory) {
        return Err("Refusing to archive a folder inside Pathify's archive directory.".into());
    }
    fs::create_dir_all(&archive_directory)?;
    let archive_path = archive_directory.join(format!("{folder_name}.zip"));
    if archive_path.exists() {
        return Err(format!(
            "An archive already exists at {}. Rename or remove it first.",
            archive_path.display()
        )
        .into());
    }
    let shortcut_path = shortcut_path_for(&source);
    if shortcut_path.exists() {
        return Err(format!(
            "A shortcut already exists at {}; leaving the folder unchanged.",
            shortcut_path.display()
        )
        .into());
    }

    let source_entries = scan_archive_source(&source)?;
    preflight_archive_source(&source, &source_entries)?;
    let temporary_archive = unique_sibling(&archive_path, "partial")?;
    let create_result = write_and_verify_archive(
        &source,
        folder_name,
        &source_entries,
        &temporary_archive,
        mode,
    );
    if let Err(error) = create_result {
        let cleanup = remove_file_with_retry(&temporary_archive);
        return Err(format!(
            "ZIP creation failed; the original folder was kept. Partial archive cleanup: {cleanup:?}; cause: {error}"
        )
        .into());
    }

    let current_entries = scan_archive_source(&source)?;
    if current_entries != source_entries {
        let cleanup = remove_file_with_retry(&temporary_archive);
        return Err(format!(
            "The folder changed while it was being archived. The original was kept; retry when it is idle. Partial archive cleanup: {cleanup:?}"
        )
        .into());
    }
    if let Err(error) = preflight_archive_source(&source, &source_entries) {
        let cleanup = remove_file_with_retry(&temporary_archive);
        return Err(format!(
            "The original folder was kept because a file became locked. Partial archive cleanup: {cleanup:?}; cause: {error}"
        )
        .into());
    }
    if let Err(error) = rename_with_retry(&temporary_archive, &archive_path) {
        let cleanup = remove_file_with_retry(&temporary_archive);
        return Err(
            format!("Could not finalize the ZIP; the original folder was kept. Temporary archive cleanup: {cleanup:?}; cause: {error}").into(),
        );
    }

    let temporary_shortcut = unique_sibling(&shortcut_path, "lnk")?;
    if let Err(error) = create_archive_shortcut(&archive_path, &source, mode, &temporary_shortcut) {
        let link_cleanup = remove_file_with_retry(&temporary_shortcut);
        let archive_cleanup = remove_file_with_retry(&archive_path);
        return Err(format!(
            "Could not prepare the restore shortcut; the original folder was kept. Shortcut cleanup: {link_cleanup:?}; archive cleanup: {archive_cleanup:?}; cause: {error}"
        )
        .into());
    }

    let tombstone = unique_sibling(&source, "archive-cleanup")?;
    if let Err(error) = rename_with_retry(&source, &tombstone) {
        let shortcut_cleanup = remove_file_with_retry(&temporary_shortcut);
        let archive_cleanup = remove_file_with_retry(&archive_path);
        return Err(format!(
            "Could not atomically move the original folder out of the way; it was not deleted. Temporary shortcut cleanup: {shortcut_cleanup:?}; archive cleanup: {archive_cleanup:?}; cause: {error}"
        )
        .into());
    }
    if let Err(error) = rename_with_retry(&temporary_shortcut, &shortcut_path) {
        let rollback_result = rename_with_retry(&tombstone, &source);
        let shortcut_cleanup = remove_file_with_retry(&temporary_shortcut);
        let archive_cleanup = remove_file_with_retry(&archive_path);
        return match (rollback_result, shortcut_cleanup, archive_cleanup) {
            (Ok(()), Ok(()), Ok(())) => Err(format!(
                "Could not create the folder shortcut; the original folder was restored: {error}"
            )
            .into()),
            (rollback, shortcut_cleanup, archive_cleanup) => Err(format!(
                "Could not create the folder shortcut ({error}). Rollback: {rollback:?}; temporary shortcut cleanup: {shortcut_cleanup:?}; archive cleanup: {archive_cleanup:?}. Recover the original at {} if needed.",
                tombstone.display()
            )
            .into()),
        };
    }

    if let Err(error) = delete_tree_with_retry(&tombstone) {
        return Err(format!(
            "The verified archive and restore shortcut are ready, but Windows could not fully remove the original. Remaining files are at {}. Retry after closing apps that may be using them. Details: {error}",
            tombstone.display()
        )
        .into());
    }
    println!("Archived {} using {}.", source.display(), mode.label());
    println!("ZIP: {}", archive_path.display());
    println!("Restore shortcut: {}", shortcut_path.display());
    Ok(())
}

fn scan_archive_source(
    root: &Path,
) -> Result<Vec<ArchiveEntrySnapshot>, Box<dyn std::error::Error>> {
    let mut pending = vec![PathBuf::new()];
    let mut entries = Vec::new();
    while let Some(relative_directory) = pending.pop() {
        let directory = root.join(&relative_directory);
        let mut children = fs::read_dir(&directory)?.collect::<Result<Vec<_>, _>>()?;
        children.sort_by_key(|entry| entry.file_name());
        for child in children {
            let name = child.file_name();
            let relative = relative_directory.join(name);
            let metadata = fs::symlink_metadata(child.path())?;
            if metadata.file_type().is_symlink() || is_reparse_point(&metadata) {
                return Err(format!(
                    "Refusing to archive reparse points or symbolic links: {}",
                    child.path().display()
                )
                .into());
            }
            let is_directory = metadata.is_dir();
            if !is_directory && !metadata.is_file() {
                return Err(
                    format!("Unsupported filesystem entry: {}", child.path().display()).into(),
                );
            }
            entries.push(ArchiveEntrySnapshot {
                relative: relative.clone(),
                is_directory,
                length: metadata.len(),
                modified: metadata.modified()?,
            });
            if is_directory {
                pending.push(relative);
            }
        }
    }
    entries.sort_by(|left, right| left.relative.cmp(&right.relative));
    Ok(entries)
}

#[cfg(windows)]
fn is_reparse_point(metadata: &fs::Metadata) -> bool {
    use std::os::windows::fs::MetadataExt;
    metadata.file_attributes() & 0x400 != 0
}

#[cfg(not(windows))]
fn is_reparse_point(_metadata: &fs::Metadata) -> bool {
    false
}

fn preflight_archive_source(
    root: &Path,
    entries: &[ArchiveEntrySnapshot],
) -> Result<(), Box<dyn std::error::Error>> {
    use std::fs::OpenOptions;
    #[cfg(windows)]
    use std::os::windows::fs::OpenOptionsExt;

    let mut paths = Vec::with_capacity(entries.len() + 1);
    paths.push(root.to_path_buf());
    paths.extend(entries.iter().map(|entry| root.join(&entry.relative)));
    for path in paths {
        let mut options = OpenOptions::new();
        options.read(true);
        #[cfg(windows)]
        options.share_mode(0).custom_flags(0x0200_0000);
        if let Err(error) = options.open(&path) {
            return Err(format!(
                "Could not exclusively check {} before deletion. Close apps using this folder and retry: {error}",
                path.display()
            )
            .into());
        }
    }
    Ok(())
}

fn write_and_verify_archive(
    source: &Path,
    root_name: &str,
    entries: &[ArchiveEntrySnapshot],
    archive_path: &Path,
    mode: ArchiveMode,
) -> Result<(), Box<dyn std::error::Error>> {
    let file = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(archive_path)?;
    let mut writer = ZipWriter::new(file);
    let (compression, level) = mode.compression();
    let options = FileOptions::default()
        .compression_method(compression)
        .compression_level(level);
    writer.add_directory(format!("{root_name}/"), options)?;
    for entry in entries {
        let relative_name = entry.relative.to_str().ok_or_else(|| {
            format!(
                "A path cannot be represented in ZIP format: {}",
                entry.relative.display()
            )
        })?;
        let archive_name = format!("{root_name}/{relative_name}");
        if entry.is_directory {
            writer.add_directory(format!("{archive_name}/"), options)?;
        } else {
            writer.start_file(archive_name, options.large_file(true))?;
            let mut input = fs::File::open(source.join(&entry.relative))?;
            io::copy(&mut input, &mut writer)?;
        }
    }
    writer.finish()?.sync_all()?;

    let file = fs::File::open(archive_path)?;
    let mut archive = ZipArchive::new(file)?;
    if archive.len() != entries.len() + 1 {
        return Err("The completed ZIP did not contain the expected entry count.".into());
    }
    let mut sink = io::sink();
    for index in 0..archive.len() {
        let mut item = archive.by_index(index)?;
        io::copy(&mut item, &mut sink)?;
    }
    Ok(())
}

fn shortcut_path_for(folder: &Path) -> PathBuf {
    let mut shortcut = folder.as_os_str().to_os_string();
    shortcut.push(".lnk");
    PathBuf::from(shortcut)
}

fn unique_sibling(path: &Path, suffix: &str) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let parent = path.parent().ok_or("The path has no parent directory")?;
    let name = path.file_name().ok_or("The path has no file name")?;
    for attempt in 0..100 {
        let mut candidate = name.to_os_string();
        candidate.push(format!(
            ".pathify-{}-{attempt}.{suffix}",
            std::process::id()
        ));
        let candidate = parent.join(candidate);
        if !candidate.exists() {
            return Ok(candidate);
        }
    }
    Err(format!(
        "Could not allocate a temporary path beside {}",
        path.display()
    )
    .into())
}

#[cfg(windows)]
fn create_archive_shortcut(
    archive: &Path,
    original: &Path,
    mode: ArchiveMode,
    shortcut: &Path,
) -> Result<(), Box<dyn std::error::Error>> {
    let executable = std::env::current_exe()?;
    let script = r#"
$ErrorActionPreference = 'Stop'
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($env:PATHIFY_LINK_PATH)
$link.TargetPath = $env:PATHIFY_EXECUTABLE
$link.Arguments = 'Archive Menu "' + $env:PATHIFY_ARCHIVE_PATH + '" "' +
    $env:PATHIFY_ORIGINAL_PATH + '" --mode ' + $env:PATHIFY_ARCHIVE_MODE
$link.WorkingDirectory = Split-Path -Parent $env:PATHIFY_EXECUTABLE
$link.Description = 'Pathify folder archive | Compression: ' + $env:PATHIFY_ARCHIVE_MODE_LABEL
$link.IconLocation = $env:PATHIFY_EXECUTABLE + ',0'
$link.Save()
[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($link)
[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
"#;
    run_powershell_script(
        script,
        &[
            ("PATHIFY_LINK_PATH", shortcut.as_os_str()),
            ("PATHIFY_EXECUTABLE", executable.as_os_str()),
            ("PATHIFY_ARCHIVE_PATH", archive.as_os_str()),
            ("PATHIFY_ORIGINAL_PATH", original.as_os_str()),
            (
                "PATHIFY_ARCHIVE_MODE",
                std::ffi::OsStr::new(mode.as_cli_name()),
            ),
            (
                "PATHIFY_ARCHIVE_MODE_LABEL",
                std::ffi::OsStr::new(mode.label()),
            ),
        ],
    )
}

#[cfg(windows)]
fn show_archive_create_picker() -> Result<(), Box<dyn std::error::Error>> {
    let executable = std::env::current_exe()?;
    let script = r#"
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Archive a folder with Pathify'
$form.StartPosition = 'CenterScreen'
$form.Size = New-Object System.Drawing.Size(610, 280)
$form.MinimumSize = New-Object System.Drawing.Size(610, 280)
$form.MaximizeBox = $false
$form.BackColor = [System.Drawing.Color]::FromArgb(29, 28, 35)
$form.ForeColor = [System.Drawing.Color]::FromArgb(239, 237, 245)
$form.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$title = New-Object System.Windows.Forms.Label
$title.Text = 'Archive a folder'
$title.Location = New-Object System.Drawing.Point(24, 18)
$title.Size = New-Object System.Drawing.Size(540, 40)
$title.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 19)
$title.ForeColor = [System.Drawing.Color]::FromArgb(183, 145, 255)
$form.Controls.Add($title)
$path = New-Object System.Windows.Forms.TextBox
$path.Location = New-Object System.Drawing.Point(26, 76)
$path.Size = New-Object System.Drawing.Size(420, 30)
$path.BackColor = [System.Drawing.Color]::FromArgb(43, 41, 51)
$path.ForeColor = [System.Drawing.Color]::White
$form.Controls.Add($path)
$browse = New-Object System.Windows.Forms.Button
$browse.Text = 'Choose folder...'
$browse.Location = New-Object System.Drawing.Point(458, 72)
$browse.Size = New-Object System.Drawing.Size(112, 38)
$browse.FlatStyle = 'Flat'
$browse.BackColor = [System.Drawing.Color]::FromArgb(66, 51, 96)
$browse.ForeColor = [System.Drawing.Color]::White
$browse.Add_Click({
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Choose the folder to compress and archive.'
    $dialog.ShowNewFolderButton = $false
    if ($dialog.ShowDialog($form) -eq 'OK') { $path.Text = $dialog.SelectedPath }
})
$form.Controls.Add($browse)
$modeLabel = New-Object System.Windows.Forms.Label
$modeLabel.Text = 'Compression mode'
$modeLabel.Location = New-Object System.Drawing.Point(26, 129)
$modeLabel.Size = New-Object System.Drawing.Size(170, 28)
$form.Controls.Add($modeLabel)
$mode = New-Object System.Windows.Forms.ComboBox
$mode.DropDownStyle = 'DropDownList'
[void]$mode.Items.AddRange([object[]]@('Max - Deflate level 9','Balanced - Deflate level 6','Fast - Store without compression'))
$mode.SelectedIndex = 0
$mode.Location = New-Object System.Drawing.Point(200, 125)
$mode.Size = New-Object System.Drawing.Size(370, 32)
$mode.BackColor = [System.Drawing.Color]::FromArgb(43, 41, 51)
$mode.ForeColor = [System.Drawing.Color]::White
$form.Controls.Add($mode)
$start = New-Object System.Windows.Forms.Button
$start.Text = 'Create archive'
$start.Location = New-Object System.Drawing.Point(408, 185)
$start.Size = New-Object System.Drawing.Size(162, 42)
$start.FlatStyle = 'Flat'
$start.BackColor = [System.Drawing.Color]::FromArgb(132, 88, 229)
$start.ForeColor = [System.Drawing.Color]::White
$start.Add_Click({
    if (-not (Test-Path -LiteralPath $path.Text -PathType Container)) {
        [System.Windows.Forms.MessageBox]::Show('Choose an existing folder first.','Pathify archive','OK','Warning') | Out-Null
        return
    }
    $modeName = switch ($mode.SelectedIndex) { 0 { 'max' } 1 { 'balanced' } 2 { 'fast' } }
    $arguments = '"' + $path.Text.Replace('"','\"') + '" --mode ' + $modeName
    try {
        $process = Start-Process -FilePath $env:PATHIFY_EXECUTABLE -ArgumentList ('Archive Create ' + $arguments) -Wait -PassThru
        if ($process.ExitCode -ne 0) {
            [System.Windows.Forms.MessageBox]::Show("Pathify could not archive this folder (exit code $($process.ExitCode)). Run `pathify Archive Create` from a terminal for details.",'Pathify archive','OK','Error') | Out-Null
            return
        }
        $form.Close()
    } catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message,'Pathify archive','OK','Error') | Out-Null
    }
})
$form.Controls.Add($start)
[void]$form.ShowDialog()
"#;
    run_powershell_script(script, &[("PATHIFY_EXECUTABLE", executable.as_os_str())])
}

impl ArchiveMode {
    fn as_cli_name(self) -> &'static str {
        match self {
            Self::Fast => "fast",
            Self::Balanced => "balanced",
            Self::Max => "max",
        }
    }
}

fn remove_file_with_retry(path: &Path) -> io::Result<()> {
    if !path.exists() {
        return Ok(());
    }
    let mut last_error = None;
    for attempt in 0..5 {
        match fs::remove_file(path) {
            Ok(()) => return Ok(()),
            Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(()),
            Err(error) => last_error = Some(error),
        }
        std::thread::sleep(Duration::from_millis(150 * (1 << attempt)));
    }
    Err(last_error.unwrap_or_else(|| io::Error::other("File removal failed")))
}

fn rename_with_retry(source: &Path, destination: &Path) -> io::Result<()> {
    let mut last_error = None;
    for attempt in 0..5 {
        match fs::rename(source, destination) {
            Ok(()) => return Ok(()),
            Err(error) if error.kind() == io::ErrorKind::NotFound => return Err(error),
            Err(error) => last_error = Some(error),
        }
        std::thread::sleep(Duration::from_millis(150 * (1 << attempt)));
    }
    Err(last_error.unwrap_or_else(|| io::Error::other("Path rename failed")))
}

fn delete_tree_with_retry(root: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let mut remaining = Vec::new();
    let mut last_error = None;
    for attempt in 0..5 {
        remaining.clear();
        if let Err(error) = collect_removal_paths(root, &mut remaining) {
            last_error = Some(format!("{}: {error}", root.display()));
            std::thread::sleep(Duration::from_millis(200 * (1 << attempt)));
            continue;
        }
        let mut pending_error = None;
        for path in remaining.iter().filter(|path| path.is_file()) {
            if let Err(error) = fs::remove_file(path) {
                if error.kind() != io::ErrorKind::NotFound {
                    pending_error = Some(format!("{}: {error}", path.display()));
                }
            }
        }
        for path in remaining.iter().filter(|path| path.is_dir()).rev() {
            if let Err(error) = fs::remove_dir(path) {
                if error.kind() != io::ErrorKind::NotFound {
                    pending_error = Some(format!("{}: {error}", path.display()));
                }
            }
        }
        if !root.exists() {
            return Ok(());
        }
        last_error = pending_error;
        std::thread::sleep(Duration::from_millis(200 * (1 << attempt)));
    }
    Err(last_error
        .unwrap_or_else(|| format!("Could not remove {}", root.display()))
        .into())
}

fn collect_removal_paths(
    root: &Path,
    paths: &mut Vec<PathBuf>,
) -> Result<(), Box<dyn std::error::Error>> {
    if !root.exists() {
        return Ok(());
    }
    let mut pending = vec![root.to_path_buf()];
    while let Some(path) = pending.pop() {
        let metadata = fs::symlink_metadata(&path)?;
        if metadata.is_dir() && !metadata.file_type().is_symlink() && !is_reparse_point(&metadata) {
            paths.push(path.clone());
            for entry in fs::read_dir(path)? {
                pending.push(entry?.path());
            }
        } else {
            paths.push(path);
        }
    }
    Ok(())
}

fn print_archive_info(
    archive_path: &Path,
    mode: Option<ArchiveMode>,
) -> Result<(), Box<dyn std::error::Error>> {
    let file = fs::File::open(archive_path)?;
    let mut archive = ZipArchive::new(file)?;
    let mut compressed_size = 0u64;
    let mut expanded_size = 0u64;
    let mut files = 0usize;
    for index in 0..archive.len() {
        let entry = archive.by_index(index)?;
        compressed_size += entry.compressed_size();
        expanded_size += entry.size();
        if !entry.is_dir() {
            files += 1;
        }
    }
    println!("Archive: {}", archive_path.display());
    println!("Files: {files}");
    println!("Expanded size: {} bytes", expanded_size);
    println!("Compressed file data: {compressed_size} bytes");
    println!("ZIP size: {} bytes", fs::metadata(archive_path)?.len());
    if let Some(mode) = mode {
        println!("Shortcut compression mode: {}", mode.label());
    } else {
        println!("Shortcut compression mode: not available from ZIP metadata");
    }
    Ok(())
}

fn restore_folder_archive(
    archive_path: &Path,
    destination: &Path,
    shortcut: Option<&Path>,
) -> Result<(), Box<dyn std::error::Error>> {
    let archive_path = archive_path.canonicalize()?;
    let root_name = archive_path
        .file_stem()
        .and_then(|name| name.to_str())
        .ok_or("The ZIP filename must have a valid folder name")?;
    let destination = destination.to_path_buf();
    let parent = destination
        .parent()
        .ok_or("The restore destination must have a parent directory")?;
    fs::create_dir_all(parent)?;
    if destination.exists() {
        return Err(format!(
            "The restore destination already exists: {}. Choose a new location or move it first.",
            destination.display()
        )
        .into());
    }
    let temporary = unique_sibling(&destination, "restore")?;
    fs::create_dir(&temporary)?;
    let extract_result = extract_archive_safely(&archive_path, root_name, &temporary);
    if let Err(error) = extract_result {
        if let Err(cleanup_error) = delete_tree_with_retry(&temporary) {
            return Err(format!(
                "Could not safely extract the archive ({error}) or remove its partial files at {} ({cleanup_error})",
                temporary.display()
            )
            .into());
        }
        return Err(error);
    }
    if let Err(error) = fs::rename(&temporary, &destination) {
        if let Err(cleanup_error) = delete_tree_with_retry(&temporary) {
            return Err(format!(
                "Could not move the restored folder into place ({error}) or remove the staging folder at {} ({cleanup_error}); the archive is intact",
                temporary.display()
            )
            .into());
        }
        return Err(format!(
            "Could not move the restored folder into place; the archive is intact: {error}"
        )
        .into());
    }
    if let Some(shortcut) = shortcut {
        if shortcut.exists() {
            remove_file_with_retry(shortcut).map_err(|error| {
                format!(
                    "Restored the folder to {}, but could not remove its shortcut {}: {error}",
                    destination.display(),
                    shortcut.display()
                )
            })?;
        }
    }
    println!(
        "Restored {} to {}.",
        archive_path.display(),
        destination.display()
    );
    Ok(())
}

fn extract_archive_safely(
    archive_path: &Path,
    root_name: &str,
    destination: &Path,
) -> Result<(), Box<dyn std::error::Error>> {
    let mut archive = ZipArchive::new(fs::File::open(archive_path)?)?;
    let mut seen = HashSet::new();
    let mut sink = io::sink();
    let mut found_root = false;
    for index in 0..archive.len() {
        let mut entry = archive.by_index(index)?;
        let enclosed = entry
            .enclosed_name()
            .ok_or_else(|| format!("Unsafe path in ZIP entry: {}", entry.name()))?;
        let mut components = enclosed.components();
        let Some(std::path::Component::Normal(root)) = components.next() else {
            return Err(format!("Unsafe ZIP entry: {}", entry.name()).into());
        };
        if root != std::ffi::OsStr::new(root_name) {
            return Err(format!("Unexpected top-level ZIP entry: {}", entry.name()).into());
        }
        let mut relative = PathBuf::new();
        for component in components {
            let std::path::Component::Normal(component) = component else {
                return Err(format!("Unsafe ZIP entry: {}", entry.name()).into());
            };
            relative.push(component);
        }
        if relative.as_os_str().is_empty() {
            if !entry.is_dir() {
                return Err("The ZIP root entry is not a directory.".into());
            }
            found_root = true;
            io::copy(&mut entry, &mut sink)?;
            continue;
        }
        if !seen.insert(relative.to_string_lossy().to_lowercase()) {
            return Err(format!("Duplicate ZIP entry: {}", entry.name()).into());
        }
        let output = destination.join(&relative);
        if !output.starts_with(destination) {
            return Err(format!("Unsafe ZIP entry: {}", entry.name()).into());
        }
        if entry.is_dir() {
            fs::create_dir_all(&output)?;
            io::copy(&mut entry, &mut sink)?;
        } else {
            let parent = output.parent().ok_or("ZIP entry has no parent directory")?;
            fs::create_dir_all(parent)?;
            let mut file = fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(&output)?;
            let copied = io::copy(&mut entry, &mut file)?;
            if copied != entry.size() {
                return Err(format!("Incomplete ZIP entry: {}", entry.name()).into());
            }
            file.sync_all()?;
        }
    }
    if !found_root {
        return Err("The ZIP archive does not contain a folder root entry.".into());
    }
    Ok(())
}

fn delete_folder_archive(
    archive_path: &Path,
    shortcut: Option<&Path>,
) -> Result<(), Box<dyn std::error::Error>> {
    let archive_path = archive_path.canonicalize()?;
    if archive_path
        .extension()
        .is_none_or(|extension| !extension.eq_ignore_ascii_case("zip"))
    {
        return Err("Only ZIP archives can be deleted with this command.".into());
    }
    let archive = ZipArchive::new(fs::File::open(&archive_path)?)?;
    drop(archive);
    if let Some(shortcut) = shortcut {
        remove_file_with_retry(shortcut).map_err(|error| {
            format!(
                "Could not remove the archive shortcut {}: {error}",
                shortcut.display()
            )
        })?;
    }
    remove_file_with_retry(&archive_path).map_err(|error| {
        format!(
            "Could not delete the ZIP {} (the shortcut, if any, has already been removed): {error}",
            archive_path.display()
        )
    })?;
    println!("Deleted archive {}.", archive_path.display());
    Ok(())
}

#[cfg(windows)]
fn show_archive_menu(
    archive: &Path,
    original: &Path,
    mode: ArchiveMode,
) -> Result<(), Box<dyn std::error::Error>> {
    let archive = archive.canonicalize()?;
    if !archive.is_file() {
        return Err(format!("Archive not found: {}", archive.display()).into());
    }
    let shortcut = shortcut_path_for(original);
    let executable = std::env::current_exe()?;
    let script = r####"
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$exe = $env:PATHIFY_EXECUTABLE
$archive = $env:PATHIFY_ARCHIVE_PATH
$original = $env:PATHIFY_ORIGINAL_PATH
$shortcut = $env:PATHIFY_SHORTCUT_PATH
$mode = $env:PATHIFY_ARCHIVE_MODE
$modeLabel = $env:PATHIFY_ARCHIVE_MODE_LABEL
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Pathify Folder Archive'
$form.StartPosition = 'CenterScreen'
$form.Size = New-Object System.Drawing.Size(510, 370)
$form.MinimumSize = New-Object System.Drawing.Size(510, 370)
$form.MaximizeBox = $false
$form.BackColor = [System.Drawing.Color]::FromArgb(29, 28, 35)
$form.ForeColor = [System.Drawing.Color]::FromArgb(239, 237, 245)
$form.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$title = New-Object System.Windows.Forms.Label
$title.Text = 'Folder archived'
$title.Location = New-Object System.Drawing.Point(24, 20)
$title.Size = New-Object System.Drawing.Size(440, 36)
$title.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 19)
$title.ForeColor = [System.Drawing.Color]::FromArgb(183, 145, 255)
$form.Controls.Add($title)
$folderLabel = New-Object System.Windows.Forms.Label
$folderLabel.Text = $original
$folderLabel.Location = New-Object System.Drawing.Point(26, 63)
$folderLabel.Size = New-Object System.Drawing.Size(440, 45)
$folderLabel.ForeColor = [System.Drawing.Color]::FromArgb(190, 187, 201)
$folderLabel.AutoEllipsis = $true
$form.Controls.Add($folderLabel)
$modeInfo = New-Object System.Windows.Forms.Label
$modeInfo.Text = "Compression: $modeLabel"
$modeInfo.Location = New-Object System.Drawing.Point(26, 105)
$modeInfo.Size = New-Object System.Drawing.Size(440, 28)
$modeInfo.ForeColor = [System.Drawing.Color]::FromArgb(183, 145, 255)
$form.Controls.Add($modeInfo)
$status = New-Object System.Windows.Forms.Label
$status.Text = ''
$status.Location = New-Object System.Drawing.Point(26, 288)
$status.Size = New-Object System.Drawing.Size(440, 24)
$status.ForeColor = [System.Drawing.Color]::FromArgb(190, 187, 201)
$status.AutoEllipsis = $true
$form.Controls.Add($status)
function Add-MenuButton([string]$label, [int]$top, [scriptblock]$action) {
    $button = New-Object System.Windows.Forms.Button
    $button.Text = $label
    $button.Location = New-Object System.Drawing.Point(26, $top)
    $button.Size = New-Object System.Drawing.Size(440, 38)
    $button.FlatStyle = 'Flat'
    $button.FlatAppearance.BorderColor = [System.Drawing.Color]::FromArgb(91, 71, 128)
    $button.FlatAppearance.MouseOverBackColor = [System.Drawing.Color]::FromArgb(67, 54, 91)
    $button.BackColor = [System.Drawing.Color]::FromArgb(44, 42, 53)
    $button.ForeColor = [System.Drawing.Color]::White
    $button.Cursor = [System.Windows.Forms.Cursors]::Hand
    $button.Add_Click($action.GetNewClosure())
    $form.Controls.Add($button)
}
function Invoke-Pathify([string[]]$arguments) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $exe
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $quoted = foreach ($argument in $arguments) {
        '"' + ([string]$argument).Replace('"', '\"') + '"'
    }
    $start.Arguments = [string]::Join(' ', [string[]]$quoted)
    $process = [System.Diagnostics.Process]::Start($start)
    $output = $process.StandardOutput.ReadToEnd()
    $errorText = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw (($errorText + "`r`n" + $output).Trim()) }
    return $output.Trim()
}
Add-MenuButton 'Unzip + restore folder' 145 {
    try {
        $result = Invoke-Pathify @('Archive','Restore',$archive,$original,'--shortcut',$shortcut)
        $form.Close()
        [System.Windows.Forms.MessageBox]::Show($result, 'Folder restored') | Out-Null
    } catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Pathify archive', 'OK', 'Error') | Out-Null
    }
}
Add-MenuButton 'View archive info' 190 {
    try {
        $result = Invoke-Pathify @('Archive','Info',$archive,'--mode',$mode)
        [System.Windows.Forms.MessageBox]::Show($result, 'Archive info') | Out-Null
    } catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Pathify archive', 'OK', 'Error') | Out-Null
    }
}
Add-MenuButton 'Delete archive' 235 {
    if ([System.Windows.Forms.MessageBox]::Show('Permanently delete this ZIP archive and its shortcut?','Delete archive','YesNo','Warning') -ne 'Yes') { return }
    try {
        $result = Invoke-Pathify @('Archive','Delete',$archive,'--shortcut',$shortcut)
        $form.Close()
        [System.Windows.Forms.MessageBox]::Show($result, 'Archive deleted') | Out-Null
    } catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Pathify archive', 'OK', 'Error') | Out-Null
    }
}
Add-MenuButton 'Restore to new location' 280 {
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Choose the parent folder for the restored copy.'
    $dialog.ShowNewFolderButton = $true
    if ($dialog.ShowDialog($form) -ne 'OK') { return }
    $destination = Join-Path $dialog.SelectedPath (Split-Path -Leaf $original)
    try {
        $result = Invoke-Pathify @('Archive','Restore',$archive,$destination)
        [System.Windows.Forms.MessageBox]::Show($result, 'Folder restored') | Out-Null
    } catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Pathify archive', 'OK', 'Error') | Out-Null
    }
}
[void]$form.ShowDialog()
"####;
    run_powershell_script(
        script,
        &[
            ("PATHIFY_EXECUTABLE", executable.as_os_str()),
            ("PATHIFY_ARCHIVE_PATH", archive.as_os_str()),
            ("PATHIFY_ORIGINAL_PATH", original.as_os_str()),
            ("PATHIFY_SHORTCUT_PATH", shortcut.as_os_str()),
            (
                "PATHIFY_ARCHIVE_MODE",
                std::ffi::OsStr::new(mode.as_cli_name()),
            ),
            (
                "PATHIFY_ARCHIVE_MODE_LABEL",
                std::ffi::OsStr::new(mode.label()),
            ),
        ],
    )
}

#[cfg(windows)]
fn run_powershell_script(
    script: &str,
    environment: &[(&str, &std::ffi::OsStr)],
) -> Result<(), Box<dyn std::error::Error>> {
    let windows_directory =
        std::env::var_os("WINDIR").ok_or("WINDIR is not set; PowerShell is unavailable")?;
    let powershell = PathBuf::from(windows_directory)
        .join("System32")
        .join("WindowsPowerShell")
        .join("v1.0")
        .join("powershell.exe");
    if !powershell.is_file() {
        return Err(format!("Windows PowerShell was not found: {}", powershell.display()).into());
    }
    let script_path = unique_sibling(
        &std::env::temp_dir().join(format!("Pathify-Archive-{}.ps1", std::process::id())),
        "ps1",
    )?;
    let mut file = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&script_path)?;
    let write_result = file
        .write_all(b"\xEF\xBB\xBF")
        .and_then(|()| file.write_all(script.as_bytes()))
        .and_then(|()| file.flush());
    if let Err(error) = write_result {
        drop(file);
        let cleanup_result = remove_file_with_retry(&script_path);
        if let Err(cleanup_error) = cleanup_result {
            return Err(format!(
                "Failed to write the PowerShell helper ({error}) and remove its temporary file ({cleanup_error})"
            )
            .into());
        }
        return Err(error.into());
    }
    drop(file);

    use std::os::windows::process::CommandExt;
    let mut command = Command::new(powershell);
    command
        .args([
            "-NoLogo",
            "-NoProfile",
            "-STA",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
        ])
        .arg(&script_path)
        .creation_flags(0x08000000);
    for (key, value) in environment {
        command.env(key, value);
    }
    let process_result = command.status();
    let cleanup_result = remove_file_with_retry(&script_path);
    let status = match process_result {
        Ok(status) => status,
        Err(error) => {
            if let Err(cleanup_error) = cleanup_result {
                return Err(format!(
                    "PowerShell failed to start ({error}) and its temporary script could not be removed ({cleanup_error})"
                )
                .into());
            }
            return Err(error.into());
        }
    };
    cleanup_result?;
    if !status.success() {
        return Err(format!("PowerShell helper exited with status {status}").into());
    }
    Ok(())
}

#[cfg(not(windows))]
fn create_archive_shortcut(
    _archive: &Path,
    _original: &Path,
    _mode: ArchiveMode,
    _shortcut: &Path,
) -> Result<(), Box<dyn std::error::Error>> {
    Err("folder archive shortcuts are supported only on Windows".into())
}

#[cfg(not(windows))]
fn show_archive_create_picker() -> Result<(), Box<dyn std::error::Error>> {
    Err("folder archive selection is supported only on Windows".into())
}

#[cfg(not(windows))]
fn show_archive_menu(
    _archive: &Path,
    _original: &Path,
    _mode: ArchiveMode,
) -> Result<(), Box<dyn std::error::Error>> {
    Err("folder archive menus are supported only on Windows".into())
}

fn config_path() -> Result<PathBuf, Box<dyn std::error::Error>> {
    let app_data = std::env::var_os("APPDATA")
        .ok_or("APPDATA is not set; Pathify must run in a Windows user session")?;
    Ok(PathBuf::from(app_data).join("Pathify").join("presets.json"))
}

fn load_presets() -> Result<(PathBuf, PresetFile), Box<dyn std::error::Error>> {
    let path = config_path()?;
    if !path.exists() {
        write_default_config(&path)?;
    }
    let file = fs::File::open(&path)?;
    Ok((path, serde_json::from_reader(file)?))
}

fn write_default_config(path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    if path.exists() {
        return Err(format!("{} already exists; leaving it unchanged", path.display()).into());
    }
    let mut presets = BTreeMap::new();
    presets.insert(
        "Example".to_owned(),
        vec![
            "%SystemRoot%\\System32".to_owned(),
            "%SystemRoot%".to_owned(),
            "%SystemRoot%\\System32\\Wbem".to_owned(),
            "%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\".to_owned(),
        ],
    );
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    write_json_atomic(path, &PresetFile { presets })
}

fn list_presets() -> Result<(), Box<dyn std::error::Error>> {
    let (path, presets) = load_presets()?;
    println!("Presets in {}:", path.display());
    for (name, entries) in presets.presets {
        println!("\n{name}");
        for entry in entries {
            println!("  {entry}");
        }
    }
    Ok(())
}

fn preview_preset(args: PreviewArgs) -> Result<(), Box<dyn std::error::Error>> {
    let (_, presets) = load_presets()?;
    let (name, entries) = presets
        .presets
        .iter()
        .find(|(name, _)| name.eq_ignore_ascii_case(&args.preset))
        .ok_or_else(|| format!("preset '{}' was not found", args.preset))?;
    validate_entries(entries)?;

    let current_path = read_path(args.system)?;
    let proposed_path = entries.join(";");
    let scope = if args.system { "system" } else { "user" };
    println!("Preset: {name}");
    print_path_preview(scope, current_path.as_deref().unwrap_or(""), &proposed_path);
    Ok(())
}

fn validate_presets(preset_name: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
    let (_, presets) = load_presets()?;
    let selected: Vec<(String, Vec<String>)> = if let Some(preset_name) = preset_name {
        let (name, entries) = presets
            .presets
            .iter()
            .find(|(name, _)| name.eq_ignore_ascii_case(preset_name))
            .ok_or_else(|| format!("preset '{preset_name}' was not found"))?;
        vec![(name.clone(), entries.clone())]
    } else {
        presets.presets.into_iter().collect()
    };

    if selected.is_empty() {
        return Err("there are no presets to validate".into());
    }

    let mut warnings = 0;
    for (name, entries) in selected {
        println!("Checking preset '{name}':");
        if let Err(error) = validate_entries(&entries) {
            println!("  ERROR: {error}");
            warnings += 1;
            continue;
        }

        let mut seen = BTreeSet::new();
        for entry in entries {
            if !seen.insert(entry.to_ascii_lowercase()) {
                println!("  WARNING duplicate entry: {entry}");
                warnings += 1;
            }
            let expanded = expand_path_entry(&entry)?;
            if has_unexpanded_variable(&expanded) {
                println!("  WARNING unresolved variable: {entry}");
                warnings += 1;
            } else if !Path::new(&expanded).is_dir() {
                println!("  WARNING directory not found: {entry} -> {expanded}");
                warnings += 1;
            } else {
                println!("  OK {entry}");
            }
        }
    }

    if warnings > 0 {
        return Err(format!("validation found {warnings} issue(s)").into());
    }
    println!("All selected presets are valid.");
    Ok(())
}

fn has_unexpanded_variable(value: &str) -> bool {
    let mut percent_positions = value.match_indices('%').map(|(index, _)| index);
    while let Some(start) = percent_positions.next() {
        if percent_positions.next().is_some() && start + 1 < value.len() {
            return true;
        }
    }
    false
}

#[cfg(windows)]
fn expand_path_entry(value: &str) -> Result<String, Box<dyn std::error::Error>> {
    use windows_sys::Win32::System::Environment::ExpandEnvironmentStringsW;

    let source: Vec<u16> = value.encode_utf16().chain(Some(0)).collect();
    let required = unsafe { ExpandEnvironmentStringsW(source.as_ptr(), std::ptr::null_mut(), 0) };
    if required == 0 {
        return Err(io::Error::last_os_error().into());
    }
    let mut expanded = vec![0u16; required as usize];
    let written =
        unsafe { ExpandEnvironmentStringsW(source.as_ptr(), expanded.as_mut_ptr(), required) };
    if written == 0 {
        return Err(io::Error::last_os_error().into());
    }
    expanded.truncate(written.saturating_sub(1) as usize);
    Ok(String::from_utf16_lossy(&expanded))
}

#[cfg(not(windows))]
fn expand_path_entry(value: &str) -> Result<String, Box<dyn std::error::Error>> {
    Ok(value.to_owned())
}

fn doctor() -> Result<(), Box<dyn std::error::Error>> {
    println!("Pathify {}", env!("CARGO_PKG_VERSION"));
    let config = config_path()?;
    if config.is_file() {
        match serde_json::from_reader::<_, PresetFile>(fs::File::open(&config)?) {
            Ok(presets) => println!("Preset config: {} presets", presets.presets.len()),
            Err(error) => println!("Preset config: invalid ({error})"),
        }
    } else {
        println!("Preset config: missing ({})", config.display());
    }

    for (scope, system) in [("user", false), ("system", true)] {
        match read_path(system) {
            Ok(Some(value)) => println!("{scope} PATH: {} entries", path_entry_count(&value)),
            Ok(None) => println!("{scope} PATH: not set"),
            Err(error) => println!("{scope} PATH: unavailable ({error})"),
        }
    }

    let backup_count = match fs::read_dir(backup_directory()?) {
        Ok(entries) => entries
            .filter_map(Result::ok)
            .filter_map(|entry| entry.file_name().into_string().ok())
            .filter(|name| parse_backup_filename(name).is_some())
            .count(),
        Err(error) if error.kind() == io::ErrorKind::NotFound => 0,
        Err(error) => return Err(error.into()),
    };
    println!("PATH backups: {backup_count}");

    match load_managed_sessions() {
        Ok(sessions) => println!("Managed shell sessions: {}", sessions.sessions.len()),
        Err(error) => println!("Managed shell sessions: unavailable ({error})"),
    }
    Ok(())
}

fn path_entry_count(value: &str) -> usize {
    value.split(';').filter(|entry| !entry.is_empty()).count()
}

fn manage_preset(command: PresetCommand) -> Result<(), Box<dyn std::error::Error>> {
    match command {
        PresetCommand::Create(args) => create_preset(args),
        PresetCommand::Edit(args) => edit_preset(args),
        PresetCommand::Rename(args) => rename_preset(args),
        PresetCommand::Remove(args) => remove_preset(args),
    }
}

fn create_preset(args: CreatePresetArgs) -> Result<(), Box<dyn std::error::Error>> {
    validate_preset_name(&args.name)?;
    validate_entries(&args.entries)?;
    let (path, mut file) = load_presets()?;
    if find_preset_key(&file.presets, &args.name).is_some() {
        return Err(format!("preset '{}' already exists", args.name).into());
    }
    file.presets.insert(args.name.clone(), args.entries);
    save_presets(&path, &file)?;
    println!("Created preset '{}'.", args.name);
    Ok(())
}

fn edit_preset(args: EditPresetArgs) -> Result<(), Box<dyn std::error::Error>> {
    validate_entries(&args.entries)?;
    let (path, mut file) = load_presets()?;
    let key = find_preset_key(&file.presets, &args.name)
        .cloned()
        .ok_or_else(|| format!("preset '{}' was not found", args.name))?;
    println!("Replace all entries in preset '{key}'?");
    for entry in &args.entries {
        println!("  {entry}");
    }
    if !args.force && !confirm("Save these preset entries? [y/N] ")? {
        println!("No changes made.");
        return Ok(());
    }
    file.presets.insert(key.clone(), args.entries);
    save_presets(&path, &file)?;
    println!("Updated preset '{key}'.");
    Ok(())
}

fn rename_preset(args: RenamePresetArgs) -> Result<(), Box<dyn std::error::Error>> {
    validate_preset_name(&args.new_name)?;
    let (path, mut file) = load_presets()?;
    let old_key = find_preset_key(&file.presets, &args.name)
        .cloned()
        .ok_or_else(|| format!("preset '{}' was not found", args.name))?;
    if find_preset_key(&file.presets, &args.new_name).is_some_and(|existing| existing != &old_key) {
        return Err(format!("preset '{}' already exists", args.new_name).into());
    }
    let entries = file.presets.remove(&old_key).expect("preset key exists");
    file.presets.insert(args.new_name.clone(), entries);
    save_presets(&path, &file)?;
    println!("Renamed preset '{old_key}' to '{}'.", args.new_name);
    Ok(())
}

fn remove_preset(args: RemovePresetArgs) -> Result<(), Box<dyn std::error::Error>> {
    let (path, mut file) = load_presets()?;
    let key = find_preset_key(&file.presets, &args.name)
        .cloned()
        .ok_or_else(|| format!("preset '{}' was not found", args.name))?;
    if !args.force && !confirm(&format!("Remove preset '{key}'? [y/N] "))? {
        println!("No changes made.");
        return Ok(());
    }
    file.presets.remove(&key);
    save_presets(&path, &file)?;
    println!("Removed preset '{key}'.");
    Ok(())
}

fn import_presets(args: ImportArgs) -> Result<(), Box<dyn std::error::Error>> {
    let imported: PresetFile = serde_json::from_reader(fs::File::open(&args.file)?)?;
    validate_preset_file(&imported)?;
    let (path, mut existing) = load_presets()?;
    let conflicts: Vec<String> = imported
        .presets
        .keys()
        .filter_map(|name| find_preset_key(&existing.presets, name).cloned())
        .collect();

    if !conflicts.is_empty() {
        println!("Import will replace these presets:");
        for name in &conflicts {
            println!("  {name}");
        }
        if !args.force && !confirm("Continue with import? [y/N] ")? {
            println!("No changes made.");
            return Ok(());
        }
    }

    let mut added = 0;
    let mut replaced = 0;
    for (name, entries) in imported.presets {
        let existing_key = find_preset_key(&existing.presets, &name).cloned();
        let destination_name = existing_key.unwrap_or_else(|| {
            added += 1;
            name
        });
        if existing.presets.contains_key(&destination_name) {
            replaced += 1;
        }
        existing.presets.insert(destination_name, entries);
    }
    save_presets(&path, &existing)?;
    println!("Imported {added} new and replaced {replaced} existing presets.");
    Ok(())
}

fn export_presets(args: ExportArgs) -> Result<(), Box<dyn std::error::Error>> {
    let (_, presets) = load_presets()?;
    if args.file.exists() && !args.force && !confirm("Overwrite the export file? [y/N] ")? {
        println!("No changes made.");
        return Ok(());
    }
    if let Some(parent) = args
        .file
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        fs::create_dir_all(parent)?;
    }
    write_json_atomic(&args.file, &presets)?;
    println!("Exported presets to {}.", args.file.display());
    Ok(())
}

fn save_presets(path: &Path, presets: &PresetFile) -> Result<(), Box<dyn std::error::Error>> {
    write_json_atomic(path, presets)
}

fn write_json_atomic<T: Serialize>(
    path: &Path,
    value: &T,
) -> Result<(), Box<dyn std::error::Error>> {
    if let Some(parent) = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        fs::create_dir_all(parent)?;
    }
    let timestamp = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
    let temporary_path =
        path.with_file_name(format!(".pathify-{}-{timestamp}.tmp", std::process::id()));
    let mut temporary_file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&temporary_path)?;

    let write_result = (|| -> Result<(), Box<dyn std::error::Error>> {
        serde_json::to_writer_pretty(&mut temporary_file, value)?;
        temporary_file.flush()?;
        temporary_file.sync_all()?;
        Ok(())
    })();
    if let Err(error) = write_result {
        drop(temporary_file);
        let _ = fs::remove_file(&temporary_path);
        return Err(error);
    }
    drop(temporary_file);

    if let Err(error) = replace_file(&temporary_path, path) {
        let _ = fs::remove_file(&temporary_path);
        return Err(error.into());
    }
    Ok(())
}

#[cfg(windows)]
fn replace_file(source: &Path, destination: &Path) -> io::Result<()> {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Storage::FileSystem::{
        MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH, MoveFileExW,
    };

    let source: Vec<u16> = source.as_os_str().encode_wide().chain(Some(0)).collect();
    let destination: Vec<u16> = destination
        .as_os_str()
        .encode_wide()
        .chain(Some(0))
        .collect();
    let result = unsafe {
        MoveFileExW(
            source.as_ptr(),
            destination.as_ptr(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
        )
    };
    if result == 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

#[cfg(not(windows))]
fn replace_file(source: &Path, destination: &Path) -> io::Result<()> {
    fs::rename(source, destination)
}

fn find_preset_key<'a>(
    presets: &'a BTreeMap<String, Vec<String>>,
    name: &str,
) -> Option<&'a String> {
    presets
        .keys()
        .find(|existing| existing.eq_ignore_ascii_case(name))
}

fn validate_preset_name(name: &str) -> Result<(), Box<dyn std::error::Error>> {
    if name.trim().is_empty() || name.chars().any(char::is_control) {
        return Err("preset names cannot be empty or contain control characters".into());
    }
    Ok(())
}

fn validate_preset_file(file: &PresetFile) -> Result<(), Box<dyn std::error::Error>> {
    let mut names = BTreeSet::new();
    for (name, entries) in &file.presets {
        validate_preset_name(name)?;
        validate_entries(entries)?;
        if !names.insert(name.to_ascii_lowercase()) {
            return Err(format!("duplicate preset name ignoring case: {name}").into());
        }
    }
    if file.presets.is_empty() {
        return Err("preset file contains no presets".into());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::SystemTime;

    fn test_directory(name: &str) -> PathBuf {
        let timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system clock is after Unix epoch")
            .as_nanos();
        std::env::temp_dir().join(format!("pathify-{name}-{}-{timestamp}", std::process::id()))
    }

    #[test]
    fn archive_modes_use_the_requested_zip_compression() {
        assert_eq!(
            ArchiveMode::Fast.compression(),
            (CompressionMethod::Stored, None)
        );
        assert_eq!(
            ArchiveMode::Balanced.compression(),
            (CompressionMethod::Deflated, Some(6))
        );
        assert_eq!(
            ArchiveMode::Max.compression(),
            (CompressionMethod::Deflated, Some(9))
        );
        assert_eq!(ArchiveMode::Max.label(), "Max (Deflate 9)");
    }

    #[test]
    fn native_module_manifests_require_safe_unique_cli_names_and_the_fixed_dll_name() {
        let directory = Path::new("hello-go");
        let valid = NativeModuleManifest {
            name: "hello-go".to_owned(),
            version: "1.0.0".to_owned(),
            description: "A Go module".to_owned(),
            dll: "module.dll".to_owned(),
            abi_version: 1,
        };
        assert!(validate_native_module_manifest(&valid, directory).is_ok());

        let mut reserved = valid.clone();
        reserved.name = "archive".to_owned();
        assert!(validate_native_module_manifest(&reserved, Path::new("archive")).is_err());

        let mut unsafe_name = valid.clone();
        unsafe_name.name = "../outside".to_owned();
        assert!(validate_native_module_manifest(&unsafe_name, Path::new("../outside")).is_err());

        let mut unsafe_dll = valid.clone();
        unsafe_dll.dll = "..\\outside.dll".to_owned();
        assert!(validate_native_module_manifest(&unsafe_dll, directory).is_err());

        let mut unsupported_abi = valid;
        unsupported_abi.abi_version = 2;
        assert!(validate_native_module_manifest(&unsupported_abi, directory).is_err());
    }

    #[test]
    fn archive_round_trip_keeps_nested_and_empty_directories() {
        let root = test_directory("round-trip");
        let source = root.join("Project");
        let zip_path = root.join("Project.zip");
        let restored = root.join("Restored");
        fs::create_dir_all(source.join("nested\\empty")).expect("create source folders");
        fs::write(source.join("large.txt"), "compress-me-".repeat(20_000))
            .expect("write source file");
        fs::write(source.join("nested\\file.txt"), "nested contents").expect("write nested file");

        let result = (|| {
            let entries = scan_archive_source(&source)?;
            write_and_verify_archive(&source, "Project", &entries, &zip_path, ArchiveMode::Max)?;
            extract_archive_safely(&zip_path, "Project", &restored)?;
            assert_eq!(
                fs::read(source.join("large.txt"))?,
                fs::read(restored.join("large.txt"))?
            );
            assert_eq!(
                fs::read(source.join("nested\\file.txt"))?,
                fs::read(restored.join("nested\\file.txt"))?
            );
            assert!(restored.join("nested\\empty").is_dir());
            Ok::<(), Box<dyn std::error::Error>>(())
        })();
        let cleanup = delete_tree_with_retry(&root);
        result.expect("archive round trip succeeds");
        cleanup.expect("remove test directory");
    }

    #[test]
    fn archive_write_failure_does_not_remove_source() {
        let root = test_directory("write-failure");
        let source = root.join("Source");
        fs::create_dir_all(&source).expect("create source directory");
        fs::write(source.join("important.txt"), "keep me").expect("write source file");
        let entries = scan_archive_source(&source).expect("scan source");
        let invalid_archive_path = root.join("missing-parent").join("Source.zip");

        assert!(
            write_and_verify_archive(
                &source,
                "Source",
                &entries,
                &invalid_archive_path,
                ArchiveMode::Max
            )
            .is_err()
        );
        assert_eq!(
            fs::read_to_string(source.join("important.txt")).expect("read original"),
            "keep me"
        );
        delete_tree_with_retry(&root).expect("remove test directory");
    }

    #[cfg(windows)]
    #[test]
    fn archive_preflight_detects_an_exclusively_locked_file() {
        use std::os::windows::fs::OpenOptionsExt;

        let root = test_directory("locked-file");
        let source = root.join("Source");
        fs::create_dir_all(&source).expect("create source directory");
        let file_path = source.join("locked.txt");
        fs::write(&file_path, "locked").expect("write source file");
        let entries = scan_archive_source(&source).expect("scan source");
        let locked = fs::OpenOptions::new()
            .read(true)
            .share_mode(0)
            .open(&file_path)
            .expect("open file exclusively");

        assert!(preflight_archive_source(&source, &entries).is_err());
        drop(locked);
        preflight_archive_source(&source, &entries).expect("preflight after unlock");
        delete_tree_with_retry(&root).expect("remove test directory");
    }

    #[test]
    fn archive_extraction_rejects_parent_traversal() {
        let root = test_directory("zip-slip");
        fs::create_dir_all(&root).expect("create test folder");
        let zip_path = root.join("Unsafe.zip");
        let file = fs::File::create(&zip_path).expect("create test ZIP");
        let mut writer = ZipWriter::new(file);
        writer
            .start_file("Unsafe/../escape.txt", FileOptions::default())
            .expect("write unsafe entry");
        writer.write_all(b"outside").expect("write unsafe content");
        writer.finish().expect("finish test ZIP");
        let destination = root.join("restored");
        fs::create_dir(&destination).expect("create destination");

        let result = extract_archive_safely(&zip_path, "Unsafe", &destination);
        assert!(result.is_err());
        assert!(!root.join("escape.txt").exists());
        delete_tree_with_retry(&root).expect("remove test directory");
    }

    #[test]
    fn recognizes_george_fell_command_only() {
        assert!(is_george_fell_request(&[
            OsString::from("pathify"),
            OsString::from("george"),
            OsString::from("fell"),
        ]));
        assert!(is_george_fell_request(&[
            OsString::from("pathify"),
            OsString::from("George"),
            OsString::from("FELL"),
        ]));
        assert!(!is_george_fell_request(&[
            OsString::from("pathify"),
            OsString::from("george"),
            OsString::from("fell"),
            OsString::from("extra"),
        ]));
    }

    #[test]
    fn recognizes_george_robot_command_only() {
        assert!(is_george_robot_request(&[
            OsString::from("pathify"),
            OsString::from("george"),
            OsString::from("robot"),
        ]));
        assert!(is_george_robot_request(&[
            OsString::from("pathify"),
            OsString::from("George"),
            OsString::from("ROBOT"),
        ]));
        assert!(!is_george_robot_request(&[
            OsString::from("pathify"),
            OsString::from("george"),
            OsString::from("robot"),
            OsString::from("extra"),
        ]));
    }

    #[test]
    fn recognizes_george_command_only() {
        assert!(is_george_request(&[
            OsString::from("pathify"),
            OsString::from("george"),
        ]));
        assert!(is_george_request(&[
            OsString::from("pathify"),
            OsString::from("GEORGE"),
        ]));
        assert!(!is_george_request(&[
            OsString::from("pathify"),
            OsString::from("george"),
            OsString::from("fell"),
        ]));
    }

    #[test]
    fn recognizes_motherboard_phrase_only() {
        assert!(is_motherboard_request(&[
            OsString::from("pathify"),
            OsString::from("my"),
            OsString::from("dog"),
            OsString::from("just"),
            OsString::from("ate"),
            OsString::from("the"),
            OsString::from("motherboard"),
        ]));
        assert!(is_motherboard_request(&[
            OsString::from("pathify"),
            OsString::from("MY"),
            OsString::from("DOG"),
            OsString::from("JUST"),
            OsString::from("ATE"),
            OsString::from("THE"),
            OsString::from("MOTHERBOARD"),
        ]));
        assert!(!is_motherboard_request(&[
            OsString::from("pathify"),
            OsString::from("my"),
            OsString::from("dog"),
            OsString::from("just"),
            OsString::from("ate"),
            OsString::from("the"),
            OsString::from("motherboard"),
            OsString::from("extra"),
        ]));
        assert!(!is_motherboard_request(&[
            OsString::from("pathify"),
            OsString::from("my"),
            OsString::from("dog"),
            OsString::from("ate"),
            OsString::from("the"),
            OsString::from("motherboard"),
        ]));
    }

    #[test]
    fn path_diff_reports_duplicate_entries() {
        let current = ["C:\\Tools", "C:\\Tools", "C:\\Old"];
        let proposed = ["c:\\tools", "D:\\New"];
        let (added, removed) = path_differences(&current, &proposed);

        assert_eq!(added, ["D:\\New"]);
        assert_eq!(removed, ["C:\\Tools", "C:\\Old"]);
    }

    #[test]
    fn path_cleaner_removes_duplicates_without_changing_first_entries_or_order() {
        let entries = [
            "C:\\Tools",
            "c:/tools/",
            "",
            "%SystemRoot%\\System32",
            "\"C:\\Tools\\\"",
            "D:\\Apps",
            "   ",
        ];

        let (cleaned, duplicates) =
            deduplicate_path_entries(&entries).expect("expand PATH entries");

        assert_eq!(
            cleaned,
            ["C:\\Tools", "", "%SystemRoot%\\System32", "D:\\Apps", "   "]
        );
        assert_eq!(duplicates, ["c:/tools/", "\"C:\\Tools\\\""]);
    }

    #[test]
    fn path_cleaner_expands_environment_variables_and_preserves_drive_roots() {
        let system_root = std::env::var("SystemRoot").expect("SystemRoot environment variable");
        let expanded_system_path = format!("{system_root}\\System32");
        let entries = [
            "C:\\",
            "c:/",
            "%SystemRoot%\\System32",
            expanded_system_path.as_str(),
        ];

        let (cleaned, duplicates) =
            deduplicate_path_entries(&entries).expect("expand PATH entries");

        assert_eq!(cleaned, ["C:\\", "%SystemRoot%\\System32"]);
        assert_eq!(duplicates, ["c:/", expanded_system_path.as_str()]);
    }

    #[test]
    fn path_cleaner_reports_broken_paths_and_respects_preset_protection() {
        let existing = std::env::temp_dir().to_string_lossy().into_owned();
        let duplicate = format!("{}\\", existing.replace('/', "\\"));
        let missing = std::env::temp_dir()
            .join(format!(
                "pathify-missing-{}",
                SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .expect("system clock")
                    .as_nanos()
            ))
            .to_string_lossy()
            .into_owned();
        let entries = [existing.as_str(), duplicate.as_str(), missing.as_str()];

        let unprotected = analyze_path_entries(&entries, &HashSet::new(), false, false)
            .expect("analyze existing and missing paths");
        assert_eq!(unprotected.duplicates, [duplicate.as_str()]);
        assert_eq!(unprotected.broken.len(), 1);
        assert_eq!(unprotected.cleaned, [existing.as_str(), missing.as_str()]);

        let protected = HashSet::from([
            normalize_path_entry(&existing).unwrap(),
            normalize_path_entry(&missing).unwrap(),
        ]);
        let protected_report = analyze_path_entries(&entries, &protected, true, false)
            .expect("analyze protected paths");
        assert_eq!(
            protected_report.cleaned,
            [existing.as_str(), duplicate.as_str(), missing.as_str()]
        );
        assert_eq!(protected_report.protected_duplicates, [duplicate.as_str()]);
        assert_eq!(protected_report.protected_broken, [missing.as_str()]);
    }

    #[test]
    fn clean_command_defaults_to_preview_and_supports_system_apply() {
        let parsed = Cli::try_parse_from(["pathify", "Clean"]).unwrap();
        let Cli {
            command: Action::Clean(args),
            ..
        } = parsed
        else {
            panic!("expected clean command");
        };
        assert!(!args.apply);
        assert!(!args.system);
        assert!(args.preset.is_none());
        assert!(!args.include_presets);
        assert!(!args.remove_broken);
        assert!(!args.remove_shadowed);

        let parsed = Cli::try_parse_from(["pathify", "Clean", "--apply", "--system"]).unwrap();
        let Cli {
            command: Action::Clean(args),
            ..
        } = parsed
        else {
            panic!("expected clean command");
        };
        assert!(args.apply);
        assert!(args.system);

        let parsed = Cli::try_parse_from([
            "pathify",
            "Clean",
            "--preset",
            "Example",
            "--apply",
            "--remove-broken",
            "--remove-shadowed",
        ])
        .unwrap();
        let Cli {
            command: Action::Clean(args),
            ..
        } = parsed
        else {
            panic!("expected clean command");
        };
        assert_eq!(args.preset.as_deref(), Some("Example"));
        assert!(args.apply);
        assert!(!args.include_presets);
        assert!(args.remove_broken);
        assert!(args.remove_shadowed);
        let parsed = Cli::try_parse_from(["pathify", "Clean", "--include-presets"]).unwrap();
        let Cli {
            command: Action::Clean(args),
            ..
        } = parsed
        else {
            panic!("expected clean command");
        };
        assert!(args.include_presets);
        assert!(
            Cli::try_parse_from(["pathify", "Clean", "--preset", "Example", "--system"]).is_err()
        );
        assert!(
            Cli::try_parse_from([
                "pathify",
                "Clean",
                "--preset",
                "Example",
                "--include-presets"
            ])
            .is_err()
        );
    }

    #[test]
    fn nuke_command_requires_one_file_argument() {
        let parsed = Cli::try_parse_from(["pathify", "Nuke", "sample.bin"]).unwrap();
        let Cli {
            command: Action::Nuke { file, mode },
            ..
        } = parsed
        else {
            panic!("expected Nuke command");
        };
        assert_eq!(file, PathBuf::from("sample.bin"));
        assert_eq!(mode, NukeMode::Shuffle);
        let parsed =
            Cli::try_parse_from(["pathify", "Nuke", "sample.bin", "--gui-confirmed"]).unwrap();
        assert!(parsed.gui_confirmed);
        let parsed =
            Cli::try_parse_from(["pathify", "Nuke", "sample.bin", "--mode", "overkill"]).unwrap();
        let Cli {
            command: Action::Nuke { mode, .. },
            ..
        } = parsed
        else {
            panic!("expected Nuke command");
        };
        assert_eq!(mode, NukeMode::Overkill);
        assert!(Cli::try_parse_from(["pathify", "Nuke"]).is_err());
        assert!(Cli::try_parse_from(["pathify", "Nuke", "one.bin", "two.bin"]).is_err());
        assert!(matches!(
            Cli::try_parse_from(["pathify", "ReReg"]).unwrap().command,
            Action::ReReg
        ));
    }

    #[test]
    fn nuke_shuffle_preserves_bytes_and_both_modes_delete_only_regular_files() {
        let mut bytes = (0..=255).collect::<Vec<u8>>();
        let mut original = bytes.clone();
        shuffle_bytes(&mut bytes).expect("shuffle bytes");
        bytes.sort_unstable();
        original.sort_unstable();
        assert_eq!(bytes, original);

        let unique = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system clock")
            .as_nanos();
        let file = std::env::temp_dir().join(format!("pathify-nuke-test-{unique}.bin"));
        fs::write(&file, b"test data to shuffle").expect("create test file");
        shuffle_file_then_delete(&file, NukeMode::Shuffle)
            .expect("shuffle and delete regular file");
        assert!(!file.exists());

        let overkill_file =
            std::env::temp_dir().join(format!("pathify-nuke-overkill-test-{unique}.bin"));
        fs::write(&overkill_file, b"test data for seven passes").expect("create test file");
        shuffle_file_then_delete(&overkill_file, NukeMode::Overkill)
            .expect("overwrite and delete regular file");
        assert!(!overkill_file.exists());

        let directory = std::env::temp_dir().join(format!("pathify-nuke-test-dir-{unique}"));
        fs::create_dir(&directory).expect("create test directory");
        assert!(shuffle_file_then_delete(&directory, NukeMode::Shuffle).is_err());
        assert!(directory.is_dir());
        fs::remove_dir(directory).expect("remove test directory");
    }

    #[test]
    fn nuke_protected_path_check_matches_only_the_protected_tree() {
        assert!(path_is_within(
            Path::new(r"C:\Windows\System32\cmd.exe"),
            Path::new(r"c:\windows")
        ));
        assert!(path_is_within(
            Path::new(r"C:\Program Files\Pathify\Pathify.exe"),
            Path::new(r"C:\Program Files")
        ));
        assert!(!path_is_within(
            Path::new(r"C:\WindowsBackup\file.txt"),
            Path::new(r"C:\Windows")
        ));
        assert!(!path_is_within(
            Path::new(r"C:\Users\Admin\file.txt"),
            Path::new(r"C:\Windows")
        ));
    }

    #[cfg(windows)]
    #[test]
    fn nuke_refuses_files_with_multiple_hard_links() {
        let unique = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system clock")
            .as_nanos();
        let file = std::env::temp_dir().join(format!("pathify-nuke-hardlink-{unique}.bin"));
        let hard_link =
            std::env::temp_dir().join(format!("pathify-nuke-hardlink-alias-{unique}.bin"));
        fs::write(&file, b"leave hard-linked content intact").expect("create test file");
        fs::hard_link(&file, &hard_link).expect("create hard link");
        let opened = open_nuke_file(&file).expect("open test file");
        assert!(has_multiple_hard_links(&opened).expect("query link count"));
        drop(opened);
        assert!(shuffle_file_then_delete(&file, NukeMode::Overkill).is_err());
        assert_eq!(
            fs::read(&file).expect("read original"),
            b"leave hard-linked content intact"
        );
        fs::remove_file(file).expect("remove test file");
        fs::remove_file(hard_link).expect("remove hard link");
    }

    #[test]
    fn backup_filename_identifies_scope_and_timestamp() {
        assert_eq!(
            parse_backup_filename("system-path-123.json"),
            Some((true, 123))
        );
        assert_eq!(
            parse_backup_filename("user-path-456.txt"),
            Some((false, 456))
        );
        assert_eq!(parse_backup_filename("other-456.txt"), None);
    }

    #[test]
    fn backup_filename_supports_json_and_legacy_text() {
        assert_eq!(
            parse_backup_filename("user-path-456.json"),
            Some((false, 456))
        );
        assert_eq!(
            parse_backup_filename("system-path-789.txt"),
            Some((true, 789))
        );
        assert_eq!(parse_backup_filename("user-path-not-a-number.json"), None);
    }

    #[test]
    fn path_backup_json_keeps_scope_and_path() {
        let backup = PathBackup {
            scope: "user".to_owned(),
            path: "C:\\Windows;C:\\Tools".to_owned(),
        };
        let json = serde_json::to_string(&backup).unwrap();
        let decoded: PathBackup = serde_json::from_str(&json).unwrap();
        assert_eq!(decoded.scope, "user");
        assert_eq!(decoded.path, "C:\\Windows;C:\\Tools");
    }

    #[test]
    fn latest_backup_selection_is_limited_to_requested_scope() {
        let backups = vec![
            (false, 200, PathBuf::from("user-path-200.txt")),
            (true, 300, PathBuf::from("system-path-300.txt")),
            (false, 100, PathBuf::from("user-path-100.txt")),
            (true, 250, PathBuf::from("system-path-250.txt")),
        ];

        assert_eq!(
            newest_backup_in_scope(backups.clone(), false),
            Some((200, PathBuf::from("user-path-200.txt")))
        );
        assert_eq!(
            newest_backup_in_scope(backups, true),
            Some((300, PathBuf::from("system-path-300.txt")))
        );
    }

    #[test]
    fn latest_backup_selection_returns_none_when_scope_has_no_backups() {
        let backups = vec![(false, 200, PathBuf::from("user-path-200.txt"))];

        assert_eq!(newest_backup_in_scope(backups, true), None);
    }

    #[test]
    fn restore_latest_accepts_system_scope_but_named_backup_does_not() {
        let Cli {
            command: Action::Restore(default_restore),
            ..
        } = Cli::try_parse_from(["pathify", "Restore"]).unwrap()
        else {
            panic!("expected restore command");
        };
        assert!(!default_restore.latest);
        assert!(default_restore.backup.is_none());

        let Cli {
            command: Action::Restore(user_restore),
            ..
        } = Cli::try_parse_from(["pathify", "Restore", "--latest"]).unwrap()
        else {
            panic!("expected restore command");
        };
        assert!(!user_restore.system);

        let Cli {
            command: Action::Restore(system_restore),
            ..
        } = Cli::try_parse_from(["pathify", "Restore", "--latest", "--system"]).unwrap()
        else {
            panic!("expected restore command");
        };
        assert!(system_restore.system);

        assert!(
            Cli::try_parse_from([
                "pathify",
                "Restore",
                "--backup",
                "user-path-123.txt",
                "--system",
            ])
            .is_err()
        );
    }

    #[test]
    fn imported_presets_reject_case_insensitive_duplicates() {
        let file = PresetFile {
            presets: BTreeMap::from([
                ("Tools".to_owned(), vec!["C:\\Tools".to_owned()]),
                ("tools".to_owned(), vec!["D:\\Tools".to_owned()]),
            ]),
        };

        assert!(validate_preset_file(&file).is_err());
    }

    #[test]
    fn managed_session_requires_matching_process_identity() {
        let session = ManagedSession {
            pid: 42,
            executable: "C:\\Windows\\System32\\cmd.exe".to_owned(),
            creation_time: 1234,
        };
        let matching = ShellIdentity {
            pid: 42,
            executable: PathBuf::from("c:\\windows\\system32\\CMD.exe"),
            creation_time: 1234,
        };
        let reused_pid = ShellIdentity {
            pid: 42,
            executable: PathBuf::from("C:\\Windows\\System32\\cmd.exe"),
            creation_time: 5678,
        };

        assert!(identity_matches_session(&matching, &session));
        assert!(!identity_matches_session(&reused_pid, &session));
    }
}

fn set_preset(args: SetArgs) -> Result<(), Box<dyn std::error::Error>> {
    let (_, presets) = load_presets()?;
    let (name, entries) = presets
        .presets
        .iter()
        .find(|(name, _)| name.eq_ignore_ascii_case(&args.preset))
        .ok_or_else(|| format!("preset '{}' was not found; run `pathify list`", args.preset))?;

    validate_entries(entries)?;
    let new_path = entries.join(";");
    let scope = if args.system { "system" } else { "user" };
    if args.system {
        eprintln!("\n*** WARNING: SYSTEM-WIDE PATH REPLACEMENT ***");
        eprintln!(
            "This replaces the complete machine PATH. Omitting Windows system directories can break commands and applications."
        );
        eprintln!("Back up first with `-b` (example: `Pathify Set -s -b <Preset>`).\n");
    }
    if args.restart || args.force_restart {
        eprintln!(
            "\n*** WARNING: restarting managed shells closes the existing processes. Unsaved work can be lost, and the replacement starts as a fresh shell without the original arguments, working directory, or interactive state. ***\n"
        );
    }
    let old_path = read_path(args.system)?;
    print_path_preview(scope, old_path.as_deref().unwrap_or(""), &new_path);
    println!("Preset: {name}");
    if !args.force && !confirm("Replace the current PATH? [y/N] ")? {
        println!("No changes made.");
        return Ok(());
    }
    let restart_shells_requested = if args.restart || args.force_restart {
        let prompt = if args.force_restart {
            "Close and force-restart eligible managed shells? Unsaved work can be lost. [y/N] "
        } else {
            "Close and relaunch eligible managed shells? Unsaved work can be lost. [y/N] "
        };
        if confirm(prompt)? {
            true
        } else {
            println!("Managed shell restart canceled.");
            false
        }
    } else {
        false
    };

    if args.backup {
        save_backup(scope, old_path.as_deref().unwrap_or(""))?;
    }
    write_path(args.system, &new_path)?;
    broadcast_environment_change();
    println!("Updated {scope} PATH. Existing processes keep their old environment.");

    if restart_shells_requested {
        let result = restart_shells(&new_path, args.force_restart)?;
        println!(
            "Shell restart: {} restarted, {} left running.",
            result.restarted, result.left_running
        );
        if result.left_running > 0 {
            eprintln!("Some shells could not be restarted; close them manually if needed.");
        }
    }
    if args.restart_explorer {
        restart_explorer()?;
    }
    if args.restart_display_drivers {
        restart_display_drivers()?;
    }
    if args.reconnect_internet {
        reconnect_internet()?;
    }
    if args.restart_system {
        restart_windows(false)?;
    }
    if args.force_restart_system {
        restart_windows(true)?;
    }
    Ok(())
}

fn restart_windows(force: bool) -> Result<(), Box<dyn std::error::Error>> {
    if force {
        eprintln!("WARNING: a forced restart closes applications and may discard unsaved work.");
    } else {
        eprintln!("Windows restart requested. Save open work before continuing.");
    }
    let prompt = if force {
        "Force-restart Windows in 30 seconds? [y/N] "
    } else {
        "Restart Windows in 30 seconds? [y/N] "
    };
    if !confirm(prompt)? {
        println!("PATH updated; system restart canceled.");
        return Ok(());
    }

    let mut arguments = vec!["/r", "/t", "30"];
    if force {
        arguments.push("/f");
    }
    let status = Command::new(system32_executable("shutdown.exe")?)
        .args(arguments)
        .status()?;
    if !status.success() {
        return Err(format!("shutdown.exe failed with status {status}").into());
    }
    println!("Windows is scheduled to restart in 30 seconds. Run `shutdown /a` to cancel.");
    Ok(())
}

fn system32_executable(
    relative_path: impl AsRef<Path>,
) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let system_root = std::env::var_os("SystemRoot").ok_or("SystemRoot is not set")?;
    Ok(PathBuf::from(system_root)
        .join("System32")
        .join(relative_path))
}

fn powershell_executable() -> Result<PathBuf, Box<dyn std::error::Error>> {
    Ok(system32_executable(
        Path::new("WindowsPowerShell")
            .join("v1.0")
            .join("powershell.exe"),
    )?)
}

fn restart_explorer() -> Result<(), Box<dyn std::error::Error>> {
    eprintln!(
        "WARNING: restarting Explorer closes its windows and briefly removes the desktop/taskbar."
    );
    if !confirm("Kill and restart explorer.exe? [y/N] ")? {
        println!("Explorer restart canceled.");
        return Ok(());
    }

    let system_root = std::env::var_os("SystemRoot").ok_or("SystemRoot is not set")?;
    let taskkill = PathBuf::from(&system_root)
        .join("System32")
        .join("taskkill.exe");
    let status = Command::new(taskkill)
        .args(["/IM", "explorer.exe", "/F"])
        .status()?;
    if !status.success() {
        return Err(format!("taskkill.exe failed with status {status}").into());
    }

    Command::new(PathBuf::from(system_root).join("explorer.exe")).spawn()?;
    println!("Explorer was restarted.");
    Ok(())
}

fn query_powershell_strings(
    script: &str,
    operation: &str,
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let output = Command::new(powershell_executable()?)
        .args([
            "-NoLogo",
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            script,
        ])
        .output()?;
    if !output.status.success() {
        let details = String::from_utf8_lossy(&output.stderr);
        return Err(format!("{operation} query failed: {details}").into());
    }
    let stdout = String::from_utf8(output.stdout)?;
    let output = stdout.trim();
    if output.is_empty() || output == "null" {
        return Ok(Vec::new());
    }
    let value: serde_json::Value = serde_json::from_str(output)?;
    match value {
        serde_json::Value::String(value) => Ok(vec![value]),
        serde_json::Value::Array(values) => values
            .into_iter()
            .map(|value| {
                value
                    .as_str()
                    .map(str::to_owned)
                    .ok_or_else(|| format!("{operation} query returned a non-string value").into())
            })
            .collect(),
        _ => Err(format!("{operation} query returned an unexpected JSON value").into()),
    }
}

fn query_display_device_ids() -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let script = r#"$ErrorActionPreference = [System.Management.Automation.ActionPreference]::Stop; [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); $ids = @(Get-PnpDevice -Class Display -PresentOnly -ErrorAction Stop | ForEach-Object -Process { $_.InstanceId }); ConvertTo-Json -InputObject $ids -Compress"#;
    query_powershell_strings(script, "display device")
}

fn restart_display_drivers() -> Result<(), Box<dyn std::error::Error>> {
    let devices = query_display_device_ids()?;
    if devices.is_empty() {
        println!("No present Display-class devices were found.");
        return Ok(());
    }

    eprintln!(
        "WARNING: restarting display devices can blank or flicker every screen and requires administrator rights."
    );
    println!("Display devices to restart:");
    for device in &devices {
        println!("  {device}");
    }
    if !confirm("Restart all listed display devices? [y/N] ")? {
        println!("Display-driver restart canceled.");
        return Ok(());
    }

    let mut failures = Vec::new();
    for device in &devices {
        let status = Command::new(system32_executable("pnputil.exe")?)
            .args(["/restart-device", device])
            .status()?;
        if status.success() {
            println!("Restarted {device}");
        } else {
            failures.push(device.as_str());
            eprintln!("Could not restart {device} (status {status}).");
        }
    }
    if !failures.is_empty() {
        return Err(format!(
            "{} display device(s) could not be restarted",
            failures.len()
        )
        .into());
    }
    Ok(())
}

fn query_active_physical_adapters() -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let script = r#"$ErrorActionPreference = [System.Management.Automation.ActionPreference]::Stop; [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); $names = @(Get-NetAdapter -Physical -ErrorAction Stop | Where-Object -Property Status -EQ Up | ForEach-Object -MemberName Name); ConvertTo-Json -InputObject $names -Compress"#;
    query_powershell_strings(script, "network adapter")
}

fn reconnect_internet() -> Result<(), Box<dyn std::error::Error>> {
    let adapters = query_active_physical_adapters()?;
    if adapters.is_empty() {
        println!("No active physical network adapters were found.");
        return Ok(());
    }

    eprintln!(
        "WARNING: restarting network adapters disconnects internet and may end remote sessions; administrator rights are required."
    );
    println!("Active physical adapters to restart:");
    for adapter in &adapters {
        println!("  {adapter}");
    }
    if !confirm("Disconnect and reconnect these adapters? [y/N] ")? {
        println!("Network restart canceled.");
        return Ok(());
    }

    let script = r#"$ErrorActionPreference = [System.Management.Automation.ActionPreference]::Stop; $adapters = @(Get-NetAdapter -Physical -ErrorAction Stop | Where-Object -Property Status -EQ Up); $adapters | Restart-NetAdapter -Confirm:$false -ErrorAction Stop"#;
    let status = Command::new(powershell_executable()?)
        .args([
            "-NoLogo",
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            script,
        ])
        .status()?;
    if !status.success() {
        return Err(format!("Restart-NetAdapter failed with status {status}").into());
    }
    println!("Active physical network adapters were restarted.");
    Ok(())
}

fn restore_latest(args: RestoreArgs) -> Result<(), Box<dyn std::error::Error>> {
    let (system, backup_path, value) = match args.backup {
        Some(file_name) => backup_by_name(&file_name)?,
        None => latest_backup(args.system)?,
    };
    let scope = if system { "system" } else { "user" };
    if system {
        eprintln!("\n*** WARNING: RESTORING THE SYSTEM-WIDE PATH ***");
        eprintln!(
            "This replaces the complete machine PATH. The current value will be saved as an undo backup.\n"
        );
    }
    let current_path = read_path(system)?;
    print_path_preview(scope, current_path.as_deref().unwrap_or(""), &value);
    println!(
        "Restore {scope} PATH from {}?",
        backup_path
            .file_name()
            .unwrap_or_default()
            .to_string_lossy()
    );
    if !args.force && !confirm("Replace the current PATH? [y/N] ")? {
        println!("No changes made.");
        return Ok(());
    }

    save_backup(scope, current_path.as_deref().unwrap_or(""))?;
    write_path(system, &value)?;
    broadcast_environment_change();
    println!("Restored {scope} PATH. The previous value was saved as a new backup.");
    Ok(())
}

fn backup_current_path(system: bool) -> Result<(), Box<dyn std::error::Error>> {
    let path = read_path(system)?;
    let scope = if system { "system" } else { "user" };
    let backup = save_backup(scope, path.as_deref().unwrap_or(""))?;
    println!("Saved the current {scope} PATH to {}.", backup.display());
    Ok(())
}

fn nuke_file(path: &Path, mode: NukeMode) -> Result<(), Box<dyn std::error::Error>> {
    let target = validate_nuke_target(path)?;
    let metadata = fs::metadata(&target)?;
    eprintln!(
        "WARNING: Nuke overwrites and deletes this file; recovery may still be possible on SSDs, snapshots, journals, backups, or synced copies."
    );
    eprintln!("Files with multiple hard links are refused.");
    match mode {
        NukeMode::Shuffle => eprintln!("Mode: shuffle the bytes once, then delete."),
        NukeMode::Overkill => eprintln!("Mode: seven random overwrite passes, then delete."),
    }
    if !GUI_CONFIRMATION_ALLOWED.load(Ordering::Relaxed) {
        print!(
            "Type NUKE to continue with '{}' ({} bytes): ",
            target.display(),
            metadata.len()
        );
        io::stdout().flush()?;
        let mut answer = String::new();
        io::stdin().read_line(&mut answer)?;
        if answer.trim() != "NUKE" {
            println!("No changes made.");
            return Ok(());
        }
    }

    shuffle_file_then_delete(&target, mode)?;
    println!("Overwrote and deleted '{}'.", target.display());
    Ok(())
}

fn validate_nuke_target(path: &Path) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let metadata = fs::symlink_metadata(path)?;
    if metadata.file_type().is_symlink() || !metadata.is_file() {
        return Err(
            "Nuke only accepts a regular file; directories and symbolic links are refused.".into(),
        );
    }
    let canonical = path.canonicalize()?;
    if is_protected_nuke_path(&canonical) {
        return Err(format!(
            "Refusing to overwrite a file inside a protected system folder: {}",
            canonical.display()
        )
        .into());
    }
    Ok(canonical)
}

fn is_protected_nuke_path(path: &Path) -> bool {
    let protected_roots = [
        "SystemRoot",
        "windir",
        "ProgramFiles",
        "ProgramFiles(x86)",
        "ProgramW6432",
        "ProgramData",
    ];
    protected_roots
        .iter()
        .filter_map(std::env::var_os)
        .map(PathBuf::from)
        .filter_map(|root| root.canonicalize().ok())
        .any(|root| path_is_within(path, &root))
}

fn path_is_within(path: &Path, root: &Path) -> bool {
    let path_components = path.components().collect::<Vec<_>>();
    let root_components = root.components().collect::<Vec<_>>();
    path_components.len() >= root_components.len()
        && path_components
            .iter()
            .zip(root_components.iter())
            .all(|(path_part, root_part)| {
                path_part
                    .as_os_str()
                    .to_string_lossy()
                    .eq_ignore_ascii_case(&root_part.as_os_str().to_string_lossy())
            })
}

fn shuffle_file_then_delete(path: &Path, mode: NukeMode) -> Result<(), Box<dyn std::error::Error>> {
    let target = validate_nuke_target(path)?;
    let mut file = open_nuke_file(&target)?;
    let file_metadata = file.metadata()?;
    let length = file_metadata.len();
    if !file_metadata.is_file() {
        return Err("The selected target is no longer a regular file.".into());
    }
    if has_multiple_hard_links(&file)? {
        return Err(
            "Nuke refuses files with multiple hard links to avoid changing another path.".into(),
        );
    }

    match mode {
        NukeMode::Shuffle => {
            let capacity = usize::try_from(length)
                .map_err(|_| "The file is too large to shuffle in memory on this system.")?;
            let read_limit = length
                .checked_add(1)
                .ok_or("The file is too large to process safely.")?;
            let mut bytes = Vec::new();
            bytes.try_reserve_exact(capacity)?;
            (&mut file).take(read_limit).read_to_end(&mut bytes)?;
            if bytes.len() != capacity {
                return Err(
                    "The file changed while Nuke was reading it; no data was written.".into(),
                );
            }
            shuffle_bytes(&mut bytes)?;
            file.seek(SeekFrom::Start(0))?;
            file.write_all(&bytes)?;
            file.sync_all()?;
        }
        NukeMode::Overkill => {
            let mut random_bytes = [0u8; 64 * 1024];
            for pass in 1..=7 {
                println!("Overkill pass {pass}/7...");
                file.seek(SeekFrom::Start(0))?;
                let mut remaining = length;
                while remaining > 0 {
                    let chunk_length = usize::try_from(remaining.min(random_bytes.len() as u64))?;
                    fill_secure_random(&mut random_bytes[..chunk_length])?;
                    file.write_all(&random_bytes[..chunk_length])?;
                    remaining -= chunk_length as u64;
                }
                file.sync_all()?;
            }
        }
    }

    file.set_len(0)?;
    file.sync_all()?;
    drop(file);
    fs::remove_file(&target).map_err(|error| {
        format!(
            "The file was overwritten and truncated, but deleting '{}' failed: {error}",
            target.display()
        )
    })?;
    Ok(())
}

#[cfg(windows)]
fn open_nuke_file(path: &Path) -> io::Result<fs::File> {
    use std::os::windows::fs::OpenOptionsExt;

    let mut options = OpenOptions::new();
    options.read(true).write(true).share_mode(0);
    options.open(path)
}

#[cfg(not(windows))]
fn open_nuke_file(path: &Path) -> io::Result<fs::File> {
    OpenOptions::new().read(true).write(true).open(path)
}

#[cfg(windows)]
fn has_multiple_hard_links(file: &fs::File) -> io::Result<bool> {
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::Storage::FileSystem::{
        FILE_STANDARD_INFO, FileStandardInfo, GetFileInformationByHandleEx,
    };

    let mut info: FILE_STANDARD_INFO = unsafe { std::mem::zeroed() };
    let result = unsafe {
        GetFileInformationByHandleEx(
            file.as_raw_handle(),
            FileStandardInfo,
            (&mut info as *mut FILE_STANDARD_INFO).cast(),
            std::mem::size_of::<FILE_STANDARD_INFO>() as u32,
        )
    };
    if result == 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(info.NumberOfLinks > 1)
}

#[cfg(not(windows))]
fn has_multiple_hard_links(_file: &fs::File) -> io::Result<bool> {
    Ok(false)
}

fn shuffle_bytes(bytes: &mut [u8]) -> Result<(), Box<dyn std::error::Error>> {
    let mut random_buffer = [0u8; 4096];
    let mut random_offset = random_buffer.len();

    for index in (1..bytes.len()).rev() {
        let bound = u64::try_from(index + 1)?;
        let rejection_threshold = bound.wrapping_neg() % bound;
        let selected = loop {
            if random_offset + std::mem::size_of::<u64>() > random_buffer.len() {
                fill_secure_random(&mut random_buffer)?;
                random_offset = 0;
            }
            let value = u64::from_ne_bytes(
                random_buffer[random_offset..random_offset + 8]
                    .try_into()
                    .expect("eight-byte random value"),
            );
            random_offset += 8;
            if value >= rejection_threshold {
                break (value % bound) as usize;
            }
        };
        bytes.swap(index, selected);
    }
    Ok(())
}

#[cfg(windows)]
fn fill_secure_random(buffer: &mut [u8]) -> Result<(), Box<dyn std::error::Error>> {
    use windows_sys::Win32::Security::Cryptography::{
        BCRYPT_USE_SYSTEM_PREFERRED_RNG, BCryptGenRandom,
    };

    let length = u32::try_from(buffer.len())?;
    let status = unsafe {
        BCryptGenRandom(
            std::ptr::null_mut(),
            buffer.as_mut_ptr(),
            length,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG,
        )
    };
    if status < 0 {
        return Err(
            format!("Windows secure random generator failed with status {status:#x}.").into(),
        );
    }
    Ok(())
}

#[cfg(not(windows))]
fn fill_secure_random(buffer: &mut [u8]) -> Result<(), Box<dyn std::error::Error>> {
    fs::File::open("/dev/urandom")?.read_exact(buffer)?;
    Ok(())
}

fn backup_registry() -> Result<(), Box<dyn std::error::Error>> {
    #[cfg(windows)]
    {
        println!("ReReg creates sensitive binary copies of registry hives.");
        println!("The backup can contain credential material and should be kept private.");
        println!("It requires administrator permission and is not an atomic system snapshot.");
        if !GUI_CONFIRMATION_ALLOWED.load(Ordering::Relaxed) {
            print!("Type BACKUP REGISTRY to continue: ");
            io::stdout().flush()?;
            let mut answer = String::new();
            io::stdin().read_line(&mut answer)?;
            if answer.trim() != "BACKUP REGISTRY" {
                println!("No changes made.");
                return Ok(());
            }
        }
        save_registry_snapshot()
    }
    #[cfg(not(windows))]
    {
        Err("ReReg is supported only on Windows.".into())
    }
}

#[cfg(windows)]
fn save_registry_snapshot() -> Result<(), Box<dyn std::error::Error>> {
    let app_data = std::env::var_os("APPDATA")
        .ok_or("APPDATA is not set; cannot select the registry backup directory")?;
    let timestamp = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
    let directory = PathBuf::from(app_data)
        .join("Pathify")
        .join("registry-backups")
        .join(format!("registry-{timestamp}"));
    fs::create_dir_all(&directory)?;

    let mut hives = vec![
        ("HKLM\\SYSTEM".to_owned(), "HKLM_SYSTEM.hiv".to_owned()),
        ("HKLM\\SOFTWARE".to_owned(), "HKLM_SOFTWARE.hiv".to_owned()),
        ("HKLM\\SAM".to_owned(), "HKLM_SAM.hiv".to_owned()),
        ("HKLM\\SECURITY".to_owned(), "HKLM_SECURITY.hiv".to_owned()),
    ];
    for key in RegKey::predef(HKEY_USERS).enum_keys() {
        let key = key?;
        if key == ".DEFAULT" || key.starts_with("S-1-") {
            let filename = format!("HKU_{}.hiv", key.replace(['\\', '/', ':'], "_"));
            hives.push((format!("HKU\\{key}"), filename));
        }
    }

    let reg_exe = system32_executable("reg.exe")?;
    for (key, filename) in &hives {
        let output = Command::new(&reg_exe)
            .args(["save", key])
            .arg(directory.join(filename))
            .arg("/y")
            .output()?;
        if !output.status.success() {
            let detail = String::from_utf8_lossy(&output.stderr).trim().to_owned();
            let stdout = String::from_utf8_lossy(&output.stdout).trim().to_owned();
            let reason = if !detail.is_empty() { detail } else { stdout };
            return Err(format!(
                "Could not save registry hive {key} ({reason}). Any completed hive copies remain in {}.",
                directory.display()
            )
            .into());
        }
    }
    println!(
        "Saved {} registry hives to {}.",
        hives.len(),
        directory.display()
    );
    println!("HKLM\\HARDWARE is volatile and is not included.");
    Ok(())
}

fn clean_path(args: CleanArgs) -> Result<(), Box<dyn std::error::Error>> {
    let (scope, current, preset_path, preset_file, protected_entries) =
        if let Some(preset_name) = args.preset.as_deref() {
            let (path, file) = load_presets()?;
            let actual_name = find_preset_key(&file.presets, preset_name)
                .ok_or_else(|| format!("preset '{preset_name}' was not found"))?
                .to_owned();
            let entries = file.presets[&actual_name].join(";");
            (
                format!("preset '{actual_name}'"),
                entries,
                Some(path),
                Some(file),
                HashSet::new(),
            )
        } else {
            let scope = if args.system { "system" } else { "user" };
            let current = read_path(args.system)?.unwrap_or_default();
            let protected_entries = if args.include_presets {
                HashSet::new()
            } else {
                let (_, presets) = load_presets()?;
                presets
                    .presets
                    .values()
                    .flatten()
                    .map(|entry| normalize_path_entry(entry))
                    .collect::<Result<HashSet<_>, _>>()?
            };
            (scope.to_owned(), current, None, None, protected_entries)
        };

    let entries: Vec<&str> = current.split(';').collect();
    let report = analyze_path_entries(
        &entries,
        &protected_entries,
        args.remove_broken,
        args.remove_shadowed,
    )?;

    println!("{scope} scan:");
    for duplicate in &report.duplicates {
        let protected = report.protected_duplicates.contains(duplicate);
        println!(
            "  {} duplicate: {}",
            if protected {
                "Preset-protected"
            } else {
                "Near"
            },
            duplicate
        );
    }
    for (entry, detail) in &report.broken {
        let protected = report.protected_broken.contains(entry);
        println!(
            "  Broken {}: {} ({detail})",
            if protected {
                "entry (protected by preset)"
            } else if args.remove_broken {
                "entry (would remove)"
            } else {
                "entry (kept)"
            },
            entry
        );
    }
    for (entry, first) in &report.shadowed {
        let protected = report.protected_shadowed.contains(entry);
        println!(
            "  Shadowed {}: {} resolves to the same directory as {}",
            if protected {
                "entry (protected by preset)"
            } else if args.remove_shadowed {
                "entry (would remove)"
            } else {
                "entry (kept)"
            },
            entry,
            first
        );
    }

    if report.duplicates.is_empty() && report.broken.is_empty() && report.shadowed.is_empty() {
        println!("No duplicate, broken, or shadowed entries found.");
    }
    let removed_count = entries.len().saturating_sub(report.cleaned.len());
    println!(
        "Summary: {} near duplicate(s), {} broken entr{}, {} shadowed entr{}; {} entr{} selected for removal.",
        report.duplicates.len(),
        report.broken.len(),
        if report.broken.len() == 1 { "y" } else { "ies" },
        report.shadowed.len(),
        if report.shadowed.len() == 1 {
            "y"
        } else {
            "ies"
        },
        removed_count,
        if removed_count == 1 { "y" } else { "ies" }
    );
    if !args.apply {
        println!("Preview only. Use --apply to review and confirm the selected cleanup.");
        return Ok(());
    }
    if removed_count == 0 {
        println!("No changes needed; no backup or write was made.");
        return Ok(());
    }
    if args.system {
        eprintln!("\n*** WARNING: CLEANING THE SYSTEM-WIDE PATH ***");
        eprintln!("The current system PATH will be saved as an undo backup.\n");
    }
    if !confirm(&format!("Back up and clean {scope}? [y/N] "))? {
        println!("No changes made.");
        return Ok(());
    }

    if let (Some(path), Some(mut file), Some(preset_name)) =
        (preset_path, preset_file, args.preset.as_deref())
    {
        let actual_name = find_preset_key(&file.presets, preset_name)
            .ok_or_else(|| format!("preset '{preset_name}' was not found"))?
            .to_owned();
        let cleaned = report.cleaned;
        let new_entries = cleaned
            .iter()
            .filter(|entry| !entry.is_empty())
            .cloned()
            .collect::<Vec<_>>();
        validate_entries(&new_entries)?;
        let backup = backup_preset_file(&path)?;
        file.presets.insert(actual_name.clone(), new_entries);
        save_presets(&path, &file)?;
        println!(
            "Cleaned preset '{actual_name}'. Preset backup: {}",
            backup.display()
        );
    } else {
        save_backup(if args.system { "system" } else { "user" }, &current)?;
        write_path(args.system, &report.cleaned.join(";"))?;
        broadcast_environment_change();
        println!("Cleaned {scope}. The original PATH was backed up.");
    }
    Ok(())
}

struct PathCleanReport {
    cleaned: Vec<String>,
    duplicates: Vec<String>,
    protected_duplicates: Vec<String>,
    broken: Vec<(String, String)>,
    protected_broken: Vec<String>,
    shadowed: Vec<(String, String)>,
    protected_shadowed: Vec<String>,
}

fn analyze_path_entries(
    entries: &[&str],
    protected_entries: &HashSet<String>,
    remove_broken: bool,
    remove_shadowed: bool,
) -> Result<PathCleanReport, Box<dyn std::error::Error>> {
    let mut seen = HashSet::new();
    let mut first_resolved: BTreeMap<String, String> = BTreeMap::new();
    let mut cleaned = Vec::with_capacity(entries.len());
    let mut duplicates = Vec::new();
    let mut protected_duplicates = Vec::new();
    let mut broken = Vec::new();
    let mut protected_broken = Vec::new();
    let mut shadowed = Vec::new();
    let mut protected_shadowed = Vec::new();

    for entry in entries {
        if entry.trim().is_empty() {
            cleaned.push((*entry).to_owned());
            continue;
        }

        let normalized = normalize_path_entry(entry)?;
        let expanded = expand_path_entry(
            entry
                .trim()
                .strip_prefix('"')
                .and_then(|value| value.strip_suffix('"'))
                .unwrap_or(entry.trim()),
        )?;
        let protected = protected_entries.contains(&normalized);
        let expanded_path = Path::new(&expanded);
        let unresolved = has_unexpanded_variable(&expanded);
        let broken_start = broken.len();
        let canonical = if unresolved {
            broken.push((
                (*entry).to_owned(),
                "contains an unresolved environment variable".to_owned(),
            ));
            None
        } else {
            match fs::metadata(expanded_path) {
                Ok(metadata) if metadata.is_dir() => Some(fs::canonicalize(expanded_path)?),
                Ok(_) => {
                    broken.push(((*entry).to_owned(), "path is not a directory".to_owned()));
                    None
                }
                Err(error) if error.kind() == io::ErrorKind::NotFound => {
                    broken.push(((*entry).to_owned(), "directory does not exist".to_owned()));
                    None
                }
                Err(error) => {
                    return Err(format!("Could not inspect PATH entry '{entry}': {error}").into());
                }
            }
        };
        if protected && broken.len() > broken_start {
            protected_broken.push((*entry).to_owned());
        }
        let broken_entry = canonical.is_none();
        if broken_entry && remove_broken && !protected {
            continue;
        }

        let duplicate = !seen.insert(normalized.clone());
        if duplicate {
            duplicates.push((*entry).to_owned());
            if protected {
                protected_duplicates.push((*entry).to_owned());
                cleaned.push((*entry).to_owned());
                continue;
            }
            continue;
        }

        if let Some(canonical) = canonical {
            let canonical_key = normalize_path_entry(&canonical.to_string_lossy())?;
            if canonical_key != normalized {
                if let Some(first) = first_resolved.get(&canonical_key) {
                    shadowed.push(((*entry).to_owned(), first.clone()));
                    if protected {
                        protected_shadowed.push((*entry).to_owned());
                    }
                    if remove_shadowed && !protected {
                        continue;
                    }
                } else {
                    first_resolved.insert(canonical_key, (*entry).to_owned());
                }
            } else {
                first_resolved
                    .entry(canonical_key)
                    .or_insert_with(|| (*entry).to_owned());
            }
        }
        cleaned.push((*entry).to_owned());
    }

    Ok(PathCleanReport {
        cleaned,
        duplicates,
        protected_duplicates,
        broken,
        protected_broken,
        shadowed,
        protected_shadowed,
    })
}

fn normalize_path_entry(entry: &str) -> Result<String, Box<dyn std::error::Error>> {
    let trimmed = entry.trim();
    let unquoted = trimmed
        .strip_prefix('"')
        .and_then(|value| value.strip_suffix('"'))
        .unwrap_or(trimmed);
    let expanded = expand_path_entry(unquoted)?;
    let mut normalized = expanded.replace('/', "\\");
    if normalized.len() > 3
        || !normalized
            .as_bytes()
            .get(1)
            .is_some_and(|byte| *byte == b':')
    {
        while normalized.ends_with('\\') {
            normalized.pop();
        }
    }
    Ok(normalized.to_lowercase())
}

#[cfg(test)]
fn deduplicate_path_entries(
    entries: &[&str],
) -> Result<(Vec<String>, Vec<String>), Box<dyn std::error::Error>> {
    let mut seen = HashSet::new();
    let mut cleaned = Vec::with_capacity(entries.len());
    let mut duplicates = Vec::new();
    for entry in entries {
        if entry.trim().is_empty() || seen.insert(normalize_path_entry(entry)?) {
            cleaned.push((*entry).to_owned());
        } else {
            duplicates.push((*entry).to_owned());
        }
    }
    Ok((cleaned, duplicates))
}

fn backup_preset_file(path: &Path) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let timestamp = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
    let backup = path.with_file_name(format!("presets-{timestamp}.json"));
    fs::copy(path, &backup)?;
    Ok(backup)
}

fn print_path_preview(scope: &str, current: &str, proposed: &str) {
    let current_entries: Vec<&str> = current
        .split(';')
        .filter(|entry| !entry.is_empty())
        .collect();
    let proposed_entries: Vec<&str> = proposed
        .split(';')
        .filter(|entry| !entry.is_empty())
        .collect();
    let (added, removed) = path_differences(&current_entries, &proposed_entries);

    println!("{scope} PATH preview:");
    println!("  Current:");
    print_entries(&current_entries);
    println!("  Proposed:");
    print_entries(&proposed_entries);
    println!("  Complete proposed value:\n    {proposed}");
    println!("  Changes:");

    for entry in &added {
        println!("    + {entry}");
    }
    for entry in &removed {
        println!("    - {entry}");
    }
    if added.is_empty() && removed.is_empty() {
        println!("    (no changes)");
    }
}

fn path_differences(current: &[&str], proposed: &[&str]) -> (Vec<String>, Vec<String>) {
    let mut current_counts = BTreeMap::new();
    for entry in current {
        *current_counts
            .entry(entry.to_ascii_lowercase())
            .or_insert(0usize) += 1;
    }

    let mut added = Vec::new();
    for entry in proposed {
        let count = current_counts
            .entry(entry.to_ascii_lowercase())
            .or_insert(0);
        if *count == 0 {
            added.push((*entry).to_owned());
        } else {
            *count -= 1;
        }
    }

    let mut proposed_counts = BTreeMap::new();
    for entry in proposed {
        *proposed_counts
            .entry(entry.to_ascii_lowercase())
            .or_insert(0usize) += 1;
    }
    let mut removed = Vec::new();
    for entry in current {
        let count = proposed_counts
            .entry(entry.to_ascii_lowercase())
            .or_insert(0);
        if *count == 0 {
            removed.push((*entry).to_owned());
        } else {
            *count -= 1;
        }
    }
    (added, removed)
}

fn print_entries(entries: &[&str]) {
    if entries.is_empty() {
        println!("    (empty)");
        return;
    }
    for entry in entries {
        println!("    {entry}");
    }
}

fn latest_backup(system: bool) -> Result<(bool, PathBuf, String), Box<dyn std::error::Error>> {
    let directory = backup_directory()?;
    let entries = match fs::read_dir(&directory) {
        Ok(entries) => entries,
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            return Err("no PATH backups found; create one with `Pathify Backup`".into());
        }
        Err(error) => return Err(error.into()),
    };

    let mut backups = Vec::new();
    for entry in entries {
        let entry = entry?;
        if !entry.file_type()?.is_file() {
            continue;
        }
        let Some(file_name) = entry.file_name().to_str().map(str::to_owned) else {
            continue;
        };
        let Some((system, timestamp)) = parse_backup_filename(&file_name) else {
            continue;
        };
        backups.push((system, timestamp, entry.path()));
    }

    let Some((_, path)) = newest_backup_in_scope(backups, system) else {
        let scope = if system { "system" } else { "user" };
        return Err(format!(
            "no {scope} PATH backups found in %APPDATA%\\Pathify\\backups; run `Pathify Backups` to list available backups"
        )
        .into());
    };
    let value = read_backup_value(&path, system)?;
    Ok((system, path, value))
}

fn newest_backup_in_scope(
    backups: impl IntoIterator<Item = (bool, u128, PathBuf)>,
    system: bool,
) -> Option<(u128, PathBuf)> {
    backups
        .into_iter()
        .filter(|(backup_system, _, _)| *backup_system == system)
        .max_by_key(|(_, timestamp, _)| *timestamp)
        .map(|(_, timestamp, path)| (timestamp, path))
}

fn backup_by_name(file_name: &str) -> Result<(bool, PathBuf, String), Box<dyn std::error::Error>> {
    let Some((system, _)) = parse_backup_filename(file_name) else {
        return Err("backup must be a listed user-path-* or system-path-* filename".into());
    };
    let path = backup_directory()?.join(file_name);
    if !path.is_file() {
        return Err(format!("backup not found: {file_name}").into());
    }
    let value = read_backup_value(&path, system)?;
    Ok((system, path, value))
}

fn list_backups() -> Result<(), Box<dyn std::error::Error>> {
    let directory = backup_directory()?;
    let entries = match fs::read_dir(directory) {
        Ok(entries) => entries,
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            println!("No PATH backups found.");
            return Ok(());
        }
        Err(error) => return Err(error.into()),
    };

    let mut backups = Vec::new();
    for entry in entries {
        let entry = entry?;
        if !entry.file_type()?.is_file() {
            continue;
        }
        let Some(file_name) = entry.file_name().to_str().map(str::to_owned) else {
            continue;
        };
        if let Some((system, timestamp)) = parse_backup_filename(&file_name) {
            backups.push((timestamp, system, file_name));
        }
    }
    backups.sort_by(|left, right| right.0.cmp(&left.0));

    if backups.is_empty() {
        println!("No PATH backups found.");
        return Ok(());
    }
    for (_, system, file_name) in backups {
        let scope = if system { "system" } else { "user" };
        println!("{scope}\t{file_name}");
    }
    Ok(())
}

fn parse_backup_filename(file_name: &str) -> Option<(bool, u128)> {
    let (system, timestamp) = if let Some(timestamp) = file_name.strip_prefix("system-path-") {
        (true, timestamp)
    } else if let Some(timestamp) = file_name.strip_prefix("user-path-") {
        (false, timestamp)
    } else {
        return None;
    };
    let timestamp = timestamp
        .strip_suffix(".json")
        .or_else(|| timestamp.strip_suffix(".txt"))?;
    Some((system, timestamp.parse().ok()?))
}

fn validate_entries(entries: &[String]) -> Result<(), Box<dyn std::error::Error>> {
    if entries.is_empty() {
        return Err("a preset must contain at least one PATH entry".into());
    }
    for entry in entries {
        if entry.trim().is_empty() || entry.contains(';') || entry.contains('\0') {
            return Err(format!("invalid PATH entry: {entry:?}").into());
        }
    }
    Ok(())
}

fn confirm(prompt: &str) -> Result<bool, Box<dyn std::error::Error>> {
    if GUI_CONFIRMATION_ALLOWED.load(Ordering::Relaxed) {
        return Ok(true);
    }
    print!("{prompt}");
    io::stdout().flush()?;
    let mut answer = String::new();
    io::stdin().read_line(&mut answer)?;
    Ok(matches!(
        answer.trim().to_ascii_lowercase().as_str(),
        "y" | "yes"
    ))
}

fn path_key(system: bool, access: u32) -> Result<RegKey, Box<dyn std::error::Error>> {
    let (root, subkey) = if system {
        (
            RegKey::predef(HKEY_LOCAL_MACHINE),
            "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        )
    } else {
        (RegKey::predef(HKEY_CURRENT_USER), "Environment")
    };
    Ok(root.open_subkey_with_flags(subkey, access)?)
}

fn read_path(system: bool) -> Result<Option<String>, Box<dyn std::error::Error>> {
    let key = path_key(system, KEY_READ)?;
    match key.get_raw_value("Path") {
        Ok(value) => {
            let words: Vec<u16> = value
                .bytes
                .chunks_exact(2)
                .map(|chunk| u16::from_le_bytes([chunk[0], chunk[1]]))
                .take_while(|word| *word != 0)
                .collect();
            Ok(Some(String::from_utf16_lossy(&words)))
        }
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(error.into()),
    }
}

fn write_path(system: bool, value: &str) -> Result<(), Box<dyn std::error::Error>> {
    let key = path_key(system, KEY_WRITE)?;
    let bytes = value
        .encode_utf16()
        .chain(std::iter::once(0))
        .flat_map(u16::to_le_bytes)
        .collect();
    key.set_raw_value(
        "Path",
        &RegValue {
            bytes,
            vtype: REG_EXPAND_SZ,
        },
    )?;
    Ok(())
}

#[derive(Serialize, Deserialize)]
struct PathBackup {
    scope: String,
    path: String,
}

fn save_backup(scope: &str, value: &str) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let directory = backup_directory()?;
    fs::create_dir_all(&directory)?;
    let timestamp = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
    let path = directory.join(format!("{scope}-path-{timestamp}.json"));
    write_json_atomic(
        &path,
        &PathBackup {
            scope: scope.to_owned(),
            path: value.to_owned(),
        },
    )?;
    Ok(path)
}

fn read_backup_value(
    path: &Path,
    expected_system: bool,
) -> Result<String, Box<dyn std::error::Error>> {
    match path.extension().and_then(|extension| extension.to_str()) {
        Some("json") => {
            let backup: PathBackup = serde_json::from_reader(fs::File::open(path)?)?;
            let expected_scope = if expected_system { "system" } else { "user" };
            if backup.scope != expected_scope {
                return Err(format!(
                    "backup scope in {} does not match its filename",
                    path.display()
                )
                .into());
            }
            Ok(backup.path)
        }
        Some("txt") => Ok(fs::read_to_string(path)?),
        _ => Err(format!("unsupported PATH backup format: {}", path.display()).into()),
    }
}

fn backup_directory() -> Result<PathBuf, Box<dyn std::error::Error>> {
    let app_data =
        std::env::var_os("APPDATA").ok_or("APPDATA is not set; cannot access PATH backups")?;
    Ok(PathBuf::from(app_data).join("Pathify").join("backups"))
}

fn broadcast_environment_change() {
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        HWND_BROADCAST, SMTO_ABORTIFHUNG, SendMessageTimeoutW, WM_SETTINGCHANGE,
    };

    let environment: Vec<u16> = "Environment".encode_utf16().chain(Some(0)).collect();
    let mut result = 0;
    unsafe {
        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            environment.as_ptr() as isize,
            SMTO_ABORTIFHUNG,
            5000,
            &mut result,
        );
    }
}

struct RestartResult {
    restarted: usize,
    left_running: usize,
}

fn sessions_path() -> Result<PathBuf, Box<dyn std::error::Error>> {
    let app_data = std::env::var_os("APPDATA")
        .ok_or("APPDATA is not set; Pathify must run in a Windows user session")?;
    Ok(PathBuf::from(app_data)
        .join("Pathify")
        .join("managed-sessions.json"))
}

fn load_managed_sessions() -> Result<ManagedSessions, Box<dyn std::error::Error>> {
    let path = sessions_path()?;
    if !path.exists() {
        return Ok(ManagedSessions::default());
    }
    Ok(serde_json::from_reader(fs::File::open(path)?)?)
}

fn save_managed_sessions(sessions: &ManagedSessions) -> Result<(), Box<dyn std::error::Error>> {
    let path = sessions_path()?;
    write_json_atomic(&path, sessions)
}

#[cfg(windows)]
fn launch_shell(shell: &str) -> Result<(), Box<dyn std::error::Error>> {
    let executable = match shell.to_ascii_lowercase().as_str() {
        "powershell" | "powershell.exe" => "powershell.exe",
        "pwsh" | "pwsh.exe" => "pwsh.exe",
        "cmd" | "cmd.exe" => "cmd.exe",
        _ => return Err("supported shells: powershell, pwsh, cmd".into()),
    };
    let mut child = spawn_managed_shell(executable, None)?;
    let session = match managed_session_from_child(&mut child) {
        Ok(session) => session,
        Err(error) => {
            let _ = child.kill();
            return Err(error.into());
        }
    };
    let mut sessions = load_managed_sessions()?;
    sessions
        .sessions
        .retain(|session| session.pid != child.id());
    sessions.sessions.push(session);
    save_managed_sessions(&sessions)?;
    println!("Started managed {executable} session (PID {}).", child.id());
    Ok(())
}

#[cfg(not(windows))]
fn launch_shell(_shell: &str) -> Result<(), Box<dyn std::error::Error>> {
    Err("managed shell launch is supported only on Windows".into())
}

#[cfg(windows)]
fn spawn_managed_shell(executable: &str, path: Option<&str>) -> io::Result<Child> {
    use std::os::windows::process::CommandExt;

    let mut command = Command::new(executable);
    if let Some(path) = path {
        command.env("PATH", path);
    }
    command.creation_flags(0x0000_0200).spawn()
}

#[cfg(not(windows))]
fn spawn_managed_shell(_executable: &str, _path: Option<&str>) -> io::Result<Child> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "managed shell launch is supported only on Windows",
    ))
}

#[cfg(windows)]
fn managed_session_from_child(child: &mut Child) -> io::Result<ManagedSession> {
    let identity = query_shell_identity(child.id())?;
    Ok(ManagedSession {
        pid: identity.pid,
        executable: identity.executable.to_string_lossy().into_owned(),
        creation_time: identity.creation_time,
    })
}

#[cfg(not(windows))]
fn managed_session_from_child(_child: &mut Child) -> io::Result<ManagedSession> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "managed shell tracking is supported only on Windows",
    ))
}

#[cfg(windows)]
fn process_identity(
    process: windows_sys::Win32::Foundation::HANDLE,
    pid: u32,
) -> io::Result<ShellIdentity> {
    use windows_sys::Win32::Foundation::FILETIME;
    use windows_sys::Win32::System::Threading::{GetProcessTimes, QueryFullProcessImageNameW};

    let mut creation = FILETIME {
        dwLowDateTime: 0,
        dwHighDateTime: 0,
    };
    let mut exit = FILETIME {
        dwLowDateTime: 0,
        dwHighDateTime: 0,
    };
    let mut kernel = FILETIME {
        dwLowDateTime: 0,
        dwHighDateTime: 0,
    };
    let mut user = FILETIME {
        dwLowDateTime: 0,
        dwHighDateTime: 0,
    };
    if unsafe { GetProcessTimes(process, &mut creation, &mut exit, &mut kernel, &mut user) } == 0 {
        return Err(io::Error::last_os_error());
    }

    let mut image = [0u16; 32768];
    let mut length = image.len() as u32;
    if unsafe { QueryFullProcessImageNameW(process, 0, image.as_mut_ptr(), &mut length) } == 0 {
        return Err(io::Error::last_os_error());
    }

    Ok(ShellIdentity {
        pid,
        executable: PathBuf::from(String::from_utf16_lossy(&image[..length as usize])),
        creation_time: (u64::from(creation.dwHighDateTime) << 32)
            | u64::from(creation.dwLowDateTime),
    })
}

#[cfg(windows)]
fn query_shell_identity(pid: u32) -> io::Result<ShellIdentity> {
    use windows_sys::Win32::Foundation::CloseHandle;
    use windows_sys::Win32::System::Threading::{OpenProcess, PROCESS_QUERY_LIMITED_INFORMATION};

    let process = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid) };
    if process.is_null() {
        return Err(io::Error::last_os_error());
    }
    let identity = process_identity(process, pid);
    unsafe { CloseHandle(process) };
    identity
}

#[cfg(not(windows))]
fn query_shell_identity(_pid: u32) -> io::Result<ShellIdentity> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "managed shell identity is supported only on Windows",
    ))
}

fn identity_matches_session(identity: &ShellIdentity, session: &ManagedSession) -> bool {
    identity.pid == session.pid
        && identity.creation_time == session.creation_time
        && identity
            .executable
            .to_string_lossy()
            .eq_ignore_ascii_case(&session.executable)
}

fn restart_shells(path: &str, force: bool) -> Result<RestartResult, Box<dyn std::error::Error>> {
    let managed = load_managed_sessions()?;
    let running = find_shells()?;
    let mut result = RestartResult {
        restarted: 0,
        left_running: 0,
    };
    let mut remaining = Vec::new();

    for session in managed.sessions {
        let Some(identity) = running.iter().find(|process| process.pid == session.pid) else {
            continue;
        };
        if !identity_matches_session(identity, &session) {
            result.left_running += 1;
            continue;
        }
        let executable = identity.executable.clone();
        let closed = close_shell(&session, force);
        if !closed {
            result.left_running += 1;
            remaining.push(session);
            continue;
        }
        match spawn_managed_shell(executable.to_string_lossy().as_ref(), Some(path)) {
            Ok(mut child) => match managed_session_from_child(&mut child) {
                Ok(new_session) => {
                    result.restarted += 1;
                    remaining.push(new_session);
                }
                Err(error) => {
                    let _ = child.kill();
                    eprintln!("Could not verify restarted shell identity: {error}");
                    result.left_running += 1;
                }
            },
            Err(error) => {
                eprintln!("Could not restart {}: {error}", executable.display());
                result.left_running += 1;
            }
        }
    }
    save_managed_sessions(&ManagedSessions {
        sessions: remaining,
    })?;
    Ok(result)
}

#[cfg(windows)]
fn find_shells() -> Result<Vec<ShellIdentity>, Box<dyn std::error::Error>> {
    use windows_sys::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
    use windows_sys::Win32::System::Diagnostics::ToolHelp::{
        CreateToolhelp32Snapshot, PROCESSENTRY32W, Process32FirstW, Process32NextW,
        TH32CS_SNAPPROCESS,
    };

    let snapshot = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
    if snapshot == INVALID_HANDLE_VALUE {
        return Err(io::Error::last_os_error().into());
    }
    let mut entry = PROCESSENTRY32W {
        dwSize: std::mem::size_of::<PROCESSENTRY32W>() as u32,
        cntUsage: 0,
        th32ProcessID: 0,
        th32DefaultHeapID: 0,
        th32ModuleID: 0,
        cntThreads: 0,
        th32ParentProcessID: 0,
        pcPriClassBase: 0,
        dwFlags: 0,
        szExeFile: [0; 260],
    };
    let mut shells = Vec::new();
    let mut has_entry = unsafe { Process32FirstW(snapshot, &mut entry) } != 0;
    while has_entry {
        let exe_name = String::from_utf16_lossy(
            &entry.szExeFile[..entry
                .szExeFile
                .iter()
                .position(|value| *value == 0)
                .unwrap_or(260)],
        )
        .to_ascii_lowercase();
        if matches!(exe_name.as_str(), "powershell.exe" | "pwsh.exe" | "cmd.exe") {
            if let Ok(identity) = query_shell_identity(entry.th32ProcessID) {
                if identity
                    .executable
                    .file_name()
                    .is_some_and(|name| name.eq_ignore_ascii_case(&exe_name))
                {
                    shells.push(identity);
                }
            }
        }
        has_entry = unsafe { Process32NextW(snapshot, &mut entry) } != 0;
    }
    unsafe { CloseHandle(snapshot) };
    Ok(shells)
}

#[cfg(not(windows))]
fn find_shells() -> Result<Vec<ShellIdentity>, Box<dyn std::error::Error>> {
    Err("shell restart is supported only on Windows".into())
}

#[cfg(windows)]
fn close_shell(session: &ManagedSession, force: bool) -> bool {
    use windows_sys::Win32::Foundation::{CloseHandle, WAIT_OBJECT_0};
    use windows_sys::Win32::System::Console::{CTRL_BREAK_EVENT, GenerateConsoleCtrlEvent};
    use windows_sys::Win32::System::Threading::{
        OpenProcess, PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_TERMINATE, TerminateProcess,
        WaitForSingleObject,
    };
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        EnumWindows, GetWindowThreadProcessId, PostMessageW, WM_CLOSE,
    };

    unsafe extern "system" fn post_close(
        hwnd: windows_sys::Win32::Foundation::HWND,
        target_pid: isize,
    ) -> i32 {
        let mut window_pid = 0;
        unsafe { GetWindowThreadProcessId(hwnd, &mut window_pid) };
        if window_pid == target_pid as u32 {
            unsafe { PostMessageW(hwnd, WM_CLOSE, 0, 0) };
        }
        1
    }

    let process = unsafe {
        OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | 0x0010_0000,
            0,
            session.pid,
        )
    };
    if process.is_null() {
        return false;
    }
    let identity = process_identity(process, session.pid);
    if !identity.is_ok_and(|identity| identity_matches_session(&identity, session)) {
        unsafe { CloseHandle(process) };
        return false;
    }

    if force {
        if unsafe { TerminateProcess(process, 1) } == 0 {
            unsafe { CloseHandle(process) };
            return false;
        }
    } else {
        unsafe { GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, session.pid) };
        unsafe { EnumWindows(Some(post_close), session.pid as isize) };
    }
    let wait = unsafe { WaitForSingleObject(process, if force { 5000 } else { 10000 }) };
    unsafe { CloseHandle(process) };
    wait == WAIT_OBJECT_0
}

#[cfg(not(windows))]
fn close_shell(_session: &ManagedSession, _force: bool) -> bool {
    false
}

mod gui {
    use super::*;

    #[cfg(windows)]
    pub fn run_gui() -> Result<(), Box<dyn std::error::Error>> {
        let executable = std::env::current_exe()?;
        let native_gui = executable.with_file_name("PathifyGui.exe");
        if native_gui.is_file() {
            let status = Command::new(&native_gui).status()?;
            if !status.success() {
                return Err(format!("Pathify GUI exited with status {status}").into());
            }
            return Ok(());
        }

        let script = r####"
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$guiHelpers = @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace PathifyGui {
    public static class Dpi {
        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool SetProcessDpiAwarenessContext(IntPtr context);
        [DllImport("user32.dll")]
        private static extern bool SetProcessDPIAware();
        public static void Enable() {
            try {
                if (!SetProcessDpiAwarenessContext(new IntPtr(-4))) SetProcessDPIAware();
            }
            catch (EntryPointNotFoundException) {
                SetProcessDPIAware();
            }
        }
    }
    public sealed class ShadowPanel : Panel {
        public int Alpha { get; set; }
        public ShadowPanel() {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                ControlStyles.OptimizedDoubleBuffer | ControlStyles.SupportsTransparentBackColor, true);
            BackColor = Color.Transparent;
        }
        protected override CreateParams CreateParams {
            get {
                CreateParams cp = base.CreateParams;
                cp.ExStyle |= 0x20 | 0x08000000;
                return cp;
            }
        }
        protected override void WndProc(ref Message m) {
            if (m.Msg == 0x84) { m.Result = new IntPtr(-1); return; }
            base.WndProc(ref m);
        }
        protected override void OnPaintBackground(PaintEventArgs e) { }
        protected override void OnPaint(PaintEventArgs e) {
            base.OnPaint(e);
            if (Width < 2 || Height < 2 || Alpha <= 0) return;
            using (LinearGradientBrush brush = new LinearGradientBrush(
                new Rectangle(0, 0, Width, Height),
                Color.FromArgb(Alpha, 5, 3, 12), Color.FromArgb(0, 5, 3, 12), 0f)) {
                e.Graphics.FillRectangle(brush, ClientRectangle);
            }
        }
    }
}
'@
$guiReferences = @('System.Drawing.dll', 'System.Windows.Forms.dll')
Add-Type -TypeDefinition $guiHelpers -ReferencedAssemblies $guiReferences
[PathifyGui.Dpi]::Enable()
[System.Windows.Forms.Application]::EnableVisualStyles()
$exe = $env:PATHIFY_EXE_PATH
$bg = [System.Drawing.Color]::FromArgb(27, 28, 34)
$panel = [System.Drawing.Color]::FromArgb(36, 37, 45)
$panel2 = [System.Drawing.Color]::FromArgb(45, 46, 56)
$purple = [System.Drawing.Color]::FromArgb(160, 112, 255)
$purpleDeep = [System.Drawing.Color]::FromArgb(91, 62, 145)
$textColor = [System.Drawing.Color]::FromArgb(235, 233, 242)
$muted = [System.Drawing.Color]::FromArgb(157, 157, 173)
$script:touchMode = $false
$script:activePage = 'Overview'
$script:targetY = 86
$script:navButtons = @{}
$script:pageMotion = @()
$script:menuTargetOpen = $true
$script:menuShadowTarget = 105
function Set-CapsuleButton([System.Windows.Forms.Button]$Button) {
    $updateRegion = {
        if ($Button.Width -lt 2 -or $Button.Height -lt 2) { return }
        $diameter = [Math]::Min($Button.Height, $Button.Width)
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $path.AddArc(0, 0, $diameter, $diameter, 90, 180)
        $path.AddArc(($Button.Width - $diameter), 0, $diameter, $diameter, 270, 180)
        $path.CloseFigure()
        $oldRegion = $Button.Region
        $Button.Region = New-Object System.Drawing.Region($path)
        $path.Dispose()
        if ($oldRegion) { $oldRegion.Dispose() }
    }.GetNewClosure()
    $Button.Add_SizeChanged($updateRegion)
    & $updateRegion
}
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Pathify'
$form.StartPosition = 'CenterScreen'
$form.Size = New-Object System.Drawing.Size(1100, 760)
$form.MinimumSize = New-Object System.Drawing.Size(760, 560)
$form.BackColor = $bg
$form.ForeColor = $textColor
$form.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$form.AutoScaleMode = [System.Windows.Forms.AutoScaleMode]::Dpi
$form.AutoScaleDimensions = New-Object System.Drawing.SizeF(96, 96)
$form.KeyPreview = $true
$form.ShowInTaskbar = $true
$workArea = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
$form.Width = [Math]::Min(($workArea.Width - 80), [Math]::Max(1100, [int]($workArea.Width * 0.78)))
$form.Height = [Math]::Min(($workArea.Height - 80), [Math]::Max(760, [int]($workArea.Height * 0.82)))
$sidebar = New-Object System.Windows.Forms.Panel
$sidebar.Dock = 'Left'
$sidebar.Width = 248
$sidebar.BackColor = $panel
$form.Controls.Add($sidebar)
$brand = New-Object System.Windows.Forms.Label
$brand.Text = 'PATHIFY'
$brand.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 20)
$brand.ForeColor = $purple
$brand.Location = New-Object System.Drawing.Point(22, 20)
$brand.Size = New-Object System.Drawing.Size(170, 34)
$sidebar.Controls.Add($brand)
$subbrand = New-Object System.Windows.Forms.Label
$subbrand.Text = 'PATH management'
$subbrand.ForeColor = $muted
$subbrand.Location = New-Object System.Drawing.Point(24, 55)
$subbrand.Size = New-Object System.Drawing.Size(170, 24)
$sidebar.Controls.Add($subbrand)
$brandMark = New-Object System.Windows.Forms.Panel
$brandMark.Location = New-Object System.Drawing.Point(164, 27)
$brandMark.Size = New-Object System.Drawing.Size(24, 24)
$brandMark.BackColor = $purple
$brandMark.Add_Paint({
    param($sender, $eventArgs)
    $eventArgs.Graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(36, 37, 45))
    $eventArgs.Graphics.FillEllipse($brush, 6, 6, 12, 12)
    $brush.Dispose()
})
$sidebar.Controls.Add($brandMark)
$indicator = New-Object System.Windows.Forms.Panel
$indicator.Location = New-Object System.Drawing.Point(8, 86)
$indicator.Size = New-Object System.Drawing.Size(4, 40)
$indicator.BackColor = $purple
$sidebar.Controls.Add($indicator)
$navPages = @('Overview', 'Presets', 'Backups', 'Import / Export', 'Tools')
$navGlyphs = @{
    'Overview' = '◈'
    'Presets' = '▦'
    'Backups' = '◷'
    'Import / Export' = '⇄'
    'Tools' = '⚙'
}
$navY = 82
foreach ($page in $navPages) {
    $button = New-Object System.Windows.Forms.Button
    $button.Text = "$($navGlyphs[$page])    $page"
    $button.Tag = $page
    $button.Location = New-Object System.Drawing.Point(18, $navY)
    $button.Size = New-Object System.Drawing.Size(216, 48)
    $button.FlatStyle = 'Flat'
    $button.FlatAppearance.BorderSize = 0
    $button.TextAlign = 'MiddleLeft'
    $button.Padding = New-Object System.Windows.Forms.Padding(12, 0, 0, 0)
    $button.BackColor = $panel
    $button.ForeColor = $muted
    $button.Cursor = [System.Windows.Forms.Cursors]::Hand
    $button.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 10)
    $button.UseVisualStyleBackColor = $false
    $button.FlatAppearance.MouseOverBackColor = $panel2
    $button.FlatAppearance.MouseDownBackColor = $purpleDeep
    Set-CapsuleButton $button
    $button.AccessibleName = $page
    $tooltip = New-Object System.Windows.Forms.ToolTip
    $tooltip.SetToolTip($button, $page)
    $button.Add_Click({
        param($sender, $eventArgs)
        $script:activePage = [string]$sender.Tag
        $script:targetY = $sender.Top + 4
        Show-Page
    })
    $sidebar.Controls.Add($button)
    $script:navButtons[$page] = $button
    $navY += 54
}
$touch = New-Object System.Windows.Forms.CheckBox
$touch.Text = 'Touchscreen mode'
$touch.ForeColor = $textColor
$touch.BackColor = $panel
$touch.Location = New-Object System.Drawing.Point(22, 660)
$touch.Size = New-Object System.Drawing.Size(175, 34)
$touch.Anchor = 'Left,Bottom'
$touch.Add_CheckedChanged({
    $script:touchMode = $touch.Checked
    $form.Font = New-Object System.Drawing.Font('Segoe UI', $(if ($script:touchMode) { 12 } else { 10 }))
    $navTop = 82
    foreach ($page in $navPages) {
        $button = $script:navButtons[$page]
        $button.Height = $(if ($script:touchMode) { 56 } else { 48 })
        $button.Top = $navTop
        $navTop += $(if ($script:touchMode) { 62 } else { 54 })
    }
    Show-Page
})
$sidebar.Controls.Add($touch)
$sideFoot = New-Object System.Windows.Forms.Label
$sideFoot.Text = 'SAFE PATH MANAGEMENT' + [Environment]::NewLine + 'Local-only  ·  No background services'
$sideFoot.ForeColor = $muted
$sideFoot.Font = New-Object System.Drawing.Font('Segoe UI', 8)
$sideFoot.Location = New-Object System.Drawing.Point(22, 600)
$sideFoot.Size = New-Object System.Drawing.Size(180, 44)
$sideFoot.Anchor = 'Left,Bottom'
$sidebar.Controls.Add($sideFoot)
$main = New-Object System.Windows.Forms.Panel
$main.Dock = 'Fill'
$main.BackColor = $bg
$form.Controls.Add($main)
$topbar = New-Object System.Windows.Forms.Panel
$topbar.Dock = 'Top'
$topbar.Height = 94
$topbar.BackColor = $bg
$topbar.Add_Paint({
    param($sender, $eventArgs)
    $bounds = New-Object System.Drawing.Rectangle(28, 91, ($sender.ClientSize.Width - 56), 2)
    if ($bounds.Width -gt 0) {
        $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($bounds, $purple, [System.Drawing.Color]::FromArgb(57, 48, 75), 0)
        $eventArgs.Graphics.FillRectangle($brush, $bounds)
        $brush.Dispose()
    }
})
$main.Controls.Add($topbar)
$toggleMenu = New-Object System.Windows.Forms.Button
$toggleMenu.Text = '☰'
$toggleMenu.Font = New-Object System.Drawing.Font('Segoe UI Symbol', 14)
$toggleMenu.Location = New-Object System.Drawing.Point(22, 22)
$toggleMenu.Size = New-Object System.Drawing.Size(44, 44)
$toggleMenu.Anchor = 'Top,Left'
$toggleMenu.FlatStyle = 'Flat'
$toggleMenu.FlatAppearance.BorderSize = 0
$toggleMenu.BackColor = $panel2
$toggleMenu.ForeColor = $textColor
$toggleMenu.Cursor = [System.Windows.Forms.Cursors]::Hand
$toggleMenu.UseVisualStyleBackColor = $false
$topbar.Controls.Add($toggleMenu)
Set-CapsuleButton $toggleMenu
$toggleTip = New-Object System.Windows.Forms.ToolTip
$toggleTip.SetToolTip($toggleMenu, 'Open or close the navigation menu')
$title = New-Object System.Windows.Forms.Label
$title.Text = 'Overview'
$title.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 22)
$title.ForeColor = $textColor
$title.Location = New-Object System.Drawing.Point(80, 13)
$title.Size = New-Object System.Drawing.Size(380, 38)
$title.Anchor = 'Top,Left,Right'
$topbar.Controls.Add($title)
$subtitle = New-Object System.Windows.Forms.Label
$subtitle.Text = 'Keep your Windows PATH tidy and recoverable.'
$subtitle.ForeColor = $muted
$subtitle.Location = New-Object System.Drawing.Point(82, 53)
$subtitle.Size = New-Object System.Drawing.Size(490, 24)
$subtitle.Anchor = 'Top,Left,Right'
$topbar.Controls.Add($subtitle)
$scope = New-Object System.Windows.Forms.ComboBox
$scope.DropDownStyle = 'DropDownList'
[void]$scope.Items.Add('User PATH')
[void]$scope.Items.Add('System PATH')
$scope.SelectedIndex = 0
$scope.Location = New-Object System.Drawing.Point(620, 19)
$scope.Size = New-Object System.Drawing.Size(170, 34)
$scope.Anchor = 'Top,Right'
$scope.Add_SelectedIndexChanged({ Show-Page })
$scope.BackColor = $panel2
$scope.ForeColor = $textColor
$scope.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 10)
$scope.Cursor = [System.Windows.Forms.Cursors]::Hand
$topbar.Controls.Add($scope)
$status = New-Object System.Windows.Forms.Label
$status.Text = 'Ready'
$status.ForeColor = [System.Drawing.Color]::FromArgb(177, 151, 255)
$status.BackColor = $panel
$status.Location = New-Object System.Drawing.Point(82, 75)
$status.Size = New-Object System.Drawing.Size(390, 24)
$status.Anchor = 'Top,Left'
$status.TextAlign = 'MiddleLeft'
$status.Padding = New-Object System.Windows.Forms.Padding(8, 0, 0, 0)
$topbar.Controls.Add($status)
$body = New-Object System.Windows.Forms.Panel
$body.Dock = 'Fill'
$body.AutoScroll = $true
$body.BackColor = $bg
$body.Padding = New-Object System.Windows.Forms.Padding(24, 8, 24, 20)
$main.Controls.Add($body)
$menuShadow = New-Object PathifyGui.ShadowPanel
$menuShadow.Location = New-Object System.Drawing.Point(0, 0)
$menuShadow.Size = New-Object System.Drawing.Size(220, 600)
$menuShadow.Visible = $false
$menuShadow.BringToFront()
$main.Controls.Add($menuShadow)
$main.Controls.SetChildIndex($menuShadow, 0)
function Update-MenuLayout {
    $expanded = ($sidebar.Width -gt 140)
    $brand.Visible = $expanded
    $subbrand.Visible = $expanded
    $sideFoot.Visible = $expanded
    $touch.Visible = $expanded
    $brandMark.Left = if ($expanded) { 202 } else { 24 }
    foreach ($page in $navPages) {
        $button = $script:navButtons[$page]
        $button.Width = [Math]::Max(44, $sidebar.ClientSize.Width - 24)
        $button.Left = 12
        if ($expanded) {
            $button.Text = "$($navGlyphs[$page])    $page"
            $button.TextAlign = 'MiddleLeft'
            $button.Padding = New-Object System.Windows.Forms.Padding(12, 0, 0, 0)
        }
        else {
            $button.Text = [string]$navGlyphs[$page]
            $button.TextAlign = 'MiddleCenter'
            $button.Padding = [System.Windows.Forms.Padding]::Empty
        }
    }
    $menuShadow.Height = $main.ClientSize.Height
    $menuShadow.Invalidate()
}
$toggleMenu.Add_Click({
    $script:menuTargetOpen = -not $script:menuTargetOpen
    if ($script:menuTargetOpen) { $menuShadow.Visible = $true }
})
$sidebar.Add_SizeChanged({ Update-MenuLayout })
$main.Add_SizeChanged({ Update-MenuLayout })
function Is-SystemScope { return ($scope.SelectedIndex -eq 1) }
function Get-RegistryPath {
    if (Is-SystemScope) {
        $registry = [Microsoft.Win32.Registry]::LocalMachine
        $keyPath = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
    } else {
        $registry = [Microsoft.Win32.Registry]::CurrentUser
        $keyPath = 'Environment'
    }
    $key = $registry.OpenSubKey($keyPath)
    if ($null -eq $key) { throw 'Could not read the selected PATH registry scope.' }
    try {
        return [string]$key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    } finally { $key.Dispose() }
}
function Invoke-Pathify([string[]]$Arguments) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $exe
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $quoted = foreach ($argument in $Arguments) {
        $value = [string]$argument
        '"' + $value.Replace('"', '\"') + '"'
    }
    $start.Arguments = [string]::Join(' ', [string[]]$quoted)
    $process = [System.Diagnostics.Process]::Start($start)
    $output = $process.StandardOutput.ReadToEnd()
    $errors = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw (($errors + "`r`n" + $output).Trim()) }
    if ($errors) { $status.Text = $errors.Trim() } else { $status.Text = $output.Trim() }
    return $output.Trim()
}
function Add-Section([string]$Text, [int]$Y) {
    $label = New-Object System.Windows.Forms.Label
    $label.Text = $Text
    $label.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 15)
    $label.ForeColor = $textColor
    $label.Location = New-Object System.Drawing.Point(28, $Y)
    $label.Size = New-Object System.Drawing.Size(($body.ClientSize.Width - 70), 34)
    $label.Anchor = 'Top,Left,Right'
    $body.Controls.Add($label)
    return $label
}
function Add-CardPanel([int]$X, [int]$Y, [int]$Width, [int]$Height, [bool]$Hero = $false) {
    $card = New-Object System.Windows.Forms.Panel
    $card.Location = New-Object System.Drawing.Point($X, $Y)
    $card.Size = New-Object System.Drawing.Size($Width, $Height)
    $card.Anchor = 'Top,Left,Right'
    $card.BackColor = $panel
    $card.Tag = $Hero
    $card.Add_Paint({
        param($sender, $eventArgs)
        $bounds = New-Object System.Drawing.Rectangle(0, 0, $sender.Width, $sender.Height)
        if ($sender.Tag) {
            $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($bounds, [System.Drawing.Color]::FromArgb(55, 42, 77), [System.Drawing.Color]::FromArgb(36, 37, 45), 0)
        } else {
            $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($bounds, [System.Drawing.Color]::FromArgb(42, 43, 52), [System.Drawing.Color]::FromArgb(34, 35, 42), 90)
        }
        $eventArgs.Graphics.FillRectangle($brush, $bounds)
        $brush.Dispose()
        $border = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(61, 59, 75))
        $eventArgs.Graphics.DrawRectangle($border, 0, 0, ($sender.Width - 1), ($sender.Height - 1))
        $border.Dispose()
        if ($sender.Tag) {
            $accent = New-Object System.Drawing.Drawing2D.LinearGradientBrush((New-Object System.Drawing.Rectangle(0, 0, 5, $sender.Height)), $purple, $purpleDeep, 90)
            $eventArgs.Graphics.FillRectangle($accent, 0, 0, 5, $sender.Height)
            $accent.Dispose()
        }
    })
    $body.Controls.Add($card)
    $card.SendToBack()
    return $card
}
function Add-Label([string]$Text, [int]$X, [int]$Y, [int]$Width) {
    $label = New-Object System.Windows.Forms.Label
    $label.Text = $Text
    $label.ForeColor = $muted
    $label.Location = New-Object System.Drawing.Point($X, $Y)
    $label.Size = New-Object System.Drawing.Size($Width, 26)
    $body.Controls.Add($label)
    return $label
}
function Add-Button([string]$Text, [int]$X, [int]$Y, [int]$Width, [scriptblock]$Click) {
    $button = New-Object System.Windows.Forms.Button
    $button.Text = $Text
    $button.Location = New-Object System.Drawing.Point($X, $Y)
    $button.Size = New-Object System.Drawing.Size($Width, $(if ($script:touchMode) { 52 } else { 42 }))
    $button.FlatStyle = 'Flat'
    $button.FlatAppearance.BorderSize = 1
    $button.FlatAppearance.BorderColor = [System.Drawing.Color]::FromArgb(92, 74, 127)
    $button.FlatAppearance.MouseOverBackColor = $purpleDeep
    $button.FlatAppearance.MouseDownBackColor = $purple
    $button.BackColor = $panel2
    $button.ForeColor = $textColor
    $button.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 9)
    $button.Cursor = [System.Windows.Forms.Cursors]::Hand
    $button.UseVisualStyleBackColor = $false
    Set-CapsuleButton $button
    $handler = { try { & $Click } catch { [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Pathify', 'OK', 'Error') | Out-Null } }.GetNewClosure()
    $button.Add_Click($handler)
    $body.Controls.Add($button)
    return $button
}
function Add-TextBox([string]$Text, [int]$X, [int]$Y, [int]$Width, [int]$Height, [bool]$Multi = $false) {
    $box = New-Object System.Windows.Forms.TextBox
    $box.Text = $Text
    $box.Location = New-Object System.Drawing.Point($X, $Y)
    $box.Size = New-Object System.Drawing.Size($Width, $Height)
    $box.BackColor = [System.Drawing.Color]::FromArgb(21, 22, 27)
    $box.ForeColor = $textColor
    $box.BorderStyle = 'FixedSingle'
    $box.Font = New-Object System.Drawing.Font('Segoe UI', $(if ($script:touchMode) { 12 } else { 10 }))
    $box.Anchor = 'Top,Left,Right'
    $box.Multiline = $Multi
    if ($Multi) { $box.ScrollBars = 'Vertical'; $box.AcceptsReturn = $true }
    $body.Controls.Add($box)
    return $box
}
function Refresh-BackupList([System.Windows.Forms.ListBox]$List) {
    $List.Items.Clear()
    $output = Invoke-Pathify @('Backups')
    foreach ($line in ($output -split "`r?`n")) { if ($line.Contains("`t")) { [void]$List.Items.Add($line.Trim()) } }
}
function Show-Page {
    foreach ($control in @($body.Controls)) {
        $body.Controls.Remove($control)
        $control.Dispose()
    }
    $script:pageMotion = @()
    $title.Text = $script:activePage
    $subtitle.Text = switch ($script:activePage) {
        'Overview' { 'A cleaner PATH starts with a clear view.' }
        'Presets' { 'Build, preview, and apply PATH collections.' }
        'Backups' { 'Restore with confidence. Every restore creates an undo point.' }
        'Import / Export' { 'Move your preset library with portable JSON.' }
        'Tools' { 'Validation, diagnostics, shells, and folder archives.' }
    }
    foreach ($name in $script:navButtons.Keys) {
        $button = $script:navButtons[$name]
        $button.ForeColor = if ($name -eq $script:activePage) { [System.Drawing.Color]::White } else { $muted }
        $button.BackColor = if ($name -eq $script:activePage) { $panel2 } else { $panel }
    }
    switch ($script:activePage) {
        'Overview' {
            $pathEntries = @(Get-RegistryPath -split ';' | Where-Object { $_ })
            $backupCount = @(Get-ChildItem -LiteralPath (Join-Path $env:APPDATA 'Pathify\backups') -Filter '*.json' -ErrorAction SilentlyContinue).Count
            $scopeLabel = if (Is-SystemScope) { 'SYSTEM SCOPE' } else { 'USER SCOPE' }
            Add-CardPanel 18 16 ($body.ClientSize.Width - 54) 128 $true | Out-Null
            $heroTitle = Add-Label 'Your PATH, under control.' 48 34 480
            $heroTitle.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 21)
            $heroTitle.ForeColor = $textColor
            $heroCaption = Add-Label 'Preview changes, keep snapshots, and get back to a known-good setup.' 49 74 580
            $heroCaption.ForeColor = [System.Drawing.Color]::FromArgb(199, 190, 218)
            $scopeBadge = Add-Label $scopeLabel 49 108 200
            $scopeBadge.ForeColor = $purple
            $scopeBadge.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 9)
            $entryMetric = Add-Label "$($pathEntries.Count) ENTRIES" 590 47 180
            $entryMetric.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 12)
            $entryMetric.ForeColor = $textColor
            $backupMetric = Add-Label "$backupCount JSON SNAPSHOTS" 590 76 210
            $backupMetric.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 9)
            $backupMetric.ForeColor = [System.Drawing.Color]::FromArgb(198, 179, 255)
            if ($body.ClientSize.Width -lt 820) {
                $heroCaption.Size = New-Object System.Drawing.Size(($body.ClientSize.Width - 90), 24)
                $scopeBadge.Visible = $false
                $entryMetric.Location = New-Object System.Drawing.Point(49, 108)
                $backupMetric.Location = New-Object System.Drawing.Point(205, 108)
            }

            Add-CardPanel 18 160 ($body.ClientSize.Width - 54) 275 | Out-Null
            Add-Section 'Current registry PATH' 174 | Out-Null
            $current = Add-TextBox ($pathEntries -join "`r`n") 36 212 ($body.ClientSize.Width - 92) 150 $true
            $current.ReadOnly = $true
            $current.Font = New-Object System.Drawing.Font('Cascadia Code', 9)
            Add-Button 'Create safety snapshot' 36 379 205 { $commandArgs = @('Backup'); if (Is-SystemScope) { $commandArgs += '-s' }; Invoke-Pathify $commandArgs | Out-Null; Show-Page } | Out-Null
            Add-Button 'Browse backups' 254 379 160 { $script:activePage = 'Backups'; $script:targetY = $script:navButtons['Backups'].Top + 4; Show-Page } | Out-Null
            Add-CardPanel 18 452 ($body.ClientSize.Width - 54) 122 | Out-Null
            Add-Section 'Quick actions' 465 | Out-Null
            Add-Button 'Manage presets' 36 508 190 { $script:activePage = 'Presets'; $script:targetY = $script:navButtons['Presets'].Top + 4; Show-Page } | Out-Null
            Add-Button 'Run diagnostics' 242 508 180 { Invoke-Pathify @('Doctor') | Out-Null; [System.Windows.Forms.MessageBox]::Show($status.Text, 'Pathify diagnostics') | Out-Null } | Out-Null
        }
        'Presets' {
            Add-CardPanel 18 48 270 444 | Out-Null
            Add-CardPanel 300 48 ($body.ClientSize.Width - 336) 444 | Out-Null
            Add-Section 'Preset manager' 18 | Out-Null
            $list = New-Object System.Windows.Forms.ListBox
            $script:presetList = $list
            $script:selectedPresetOriginal = $null
            $list.Location = New-Object System.Drawing.Point(28, 58); $list.Size = New-Object System.Drawing.Size(245, 390)
            $list.BackColor = [System.Drawing.Color]::FromArgb(21, 22, 27); $list.ForeColor = $textColor
            $config = Join-Path $env:APPDATA 'Pathify\presets.json'
            $script:presetConfig = $config
            if (Test-Path -LiteralPath $config) {
                $data = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
                foreach ($property in $data.presets.PSObject.Properties) { [void]$list.Items.Add($property.Name) }
            }
            $body.Controls.Add($list)
            $nameBox = Add-TextBox '' 300 58 430 34
            $entriesBox = Add-TextBox '' 300 112 430 250 $true
            $script:presetNameBox = $nameBox
            $script:presetEntriesBox = $entriesBox
            $list.Add_SelectedIndexChanged({
                if ($null -ne $script:presetList.SelectedItem) {
                    $script:presetNameBox.Text = [string]$script:presetList.SelectedItem
                    $script:selectedPresetOriginal = [string]$script:presetList.SelectedItem
                    $data = Get-Content -LiteralPath $script:presetConfig -Raw | ConvertFrom-Json
                    $script:presetEntriesBox.Text = [string]::Join("`r`n", [string[]]$data.presets.PSObject.Properties[$script:presetNameBox.Text].Value)
                }
            })
            Add-Button 'Save / create' 300 382 145 {
                $entries = @($script:presetEntriesBox.Text -split "`r?`n" | Where-Object { $_.Trim() })
                if (-not $script:presetNameBox.Text.Trim() -or $entries.Count -eq 0) { throw 'Enter a preset name and at least one entry.' }
                if ($null -ne $script:presetList.SelectedItem) {
                    if ($script:presetNameBox.Text -cne $script:selectedPresetOriginal) {
                        Invoke-Pathify @('Preset','Rename',$script:selectedPresetOriginal,$script:presetNameBox.Text) | Out-Null
                    }
                    $command = @('Preset','Edit',$script:presetNameBox.Text) + $entries + @('-f')
                } else { $command = @('Preset','Create',$script:presetNameBox.Text) + $entries }
                Invoke-Pathify $command | Out-Null; Show-Page
            } | Out-Null
            Add-Button 'Apply' 460 382 120 {
                if ($null -eq $script:presetList.SelectedItem) { throw 'Select a preset first.' }
                if ([System.Windows.Forms.MessageBox]::Show('Replace the selected PATH?','Confirm PATH change','YesNo','Warning') -eq 'Yes') {
                    $command = @('Set',[string]$script:presetList.SelectedItem,'-f','-b'); if (Is-SystemScope) { $command += '-s' }; Invoke-Pathify $command | Out-Null
                }
            } | Out-Null
            Add-Button 'Remove' 595 382 120 {
                if ($null -eq $script:presetList.SelectedItem) { throw 'Select a preset first.' }
                if ([System.Windows.Forms.MessageBox]::Show('Remove this preset?','Confirm removal','YesNo','Warning') -eq 'Yes') { Invoke-Pathify @('Preset','Remove',[string]$script:presetList.SelectedItem,'-f') | Out-Null; Show-Page }
            } | Out-Null
            Add-Button 'New' 28 462 110 { $script:presetList.ClearSelected(); $script:presetNameBox.Clear(); $script:presetEntriesBox.Clear() } | Out-Null
            Add-Button 'Preview' 300 462 145 {
                if ($null -eq $script:presetList.SelectedItem) { throw 'Select a preset first.' }
                $command = @('Preview',[string]$script:presetList.SelectedItem)
                if (Is-SystemScope) { $command += '-s' }
                $preview = Invoke-Pathify $command
                [System.Windows.Forms.MessageBox]::Show($preview, 'PATH preview') | Out-Null
            } | Out-Null
        }
        'Backups' {
            Add-CardPanel 18 48 ($body.ClientSize.Width - 54) 438 | Out-Null
            Add-Section 'PATH backups' 18 | Out-Null
            $list = New-Object System.Windows.Forms.ListBox
            $script:backupList = $list
            $list.Location = New-Object System.Drawing.Point(28, 58); $list.Size = New-Object System.Drawing.Size(700, 340)
            $list.Anchor = 'Top,Left,Right'; $list.BackColor = [System.Drawing.Color]::FromArgb(21, 22, 27); $list.ForeColor = $textColor
            $body.Controls.Add($list)
            Refresh-BackupList $list
            Add-Button 'Back up current PATH' 28 414 205 { $commandArgs = @('Backup'); if (Is-SystemScope) { $commandArgs += '-s' }; Invoke-Pathify $commandArgs | Out-Null; Refresh-BackupList $script:backupList } | Out-Null
            Add-Button 'Restore latest' 250 414 175 {
                if ([System.Windows.Forms.MessageBox]::Show('Restore the latest backup? The current PATH will be saved as an undo backup.','Confirm restore','YesNo','Warning') -eq 'Yes') {
                    $commandArgs = @('Restore','--latest','-f'); if (Is-SystemScope) { $commandArgs += '-s' }; Invoke-Pathify $commandArgs | Out-Null; Refresh-BackupList $script:backupList
                }
            } | Out-Null
            Add-Button 'Restore selected' 442 414 185 {
                if ($null -eq $script:backupList.SelectedItem) { throw 'Select a backup first.' }
                $filename = ([string]$script:backupList.SelectedItem -split "`t")[-1]
                if ([System.Windows.Forms.MessageBox]::Show("Restore $filename? The current PATH will be saved as an undo backup.",'Confirm restore','YesNo','Warning') -eq 'Yes') { Invoke-Pathify @('Restore','--backup',$filename,'-f') | Out-Null; Refresh-BackupList $script:backupList }
            } | Out-Null
        }
        'Import / Export' {
            Add-CardPanel 18 48 ($body.ClientSize.Width - 54) 172 | Out-Null
            Add-CardPanel 18 238 ($body.ClientSize.Width - 54) 172 | Out-Null
            Add-Section 'Preset files' 18 | Out-Null
            Add-Label 'JSON file path' 28 64 200 | Out-Null
            $importPath = Add-TextBox '' 28 96 690 34
            $script:importPath = $importPath
            Add-Button 'Import presets' 28 146 170 {
                if (-not (Test-Path -LiteralPath $script:importPath.Text)) { throw 'Choose an existing JSON file.' }
                if ([System.Windows.Forms.MessageBox]::Show('Import presets? Matching names will be replaced.','Confirm import','YesNo','Warning') -eq 'Yes') { Invoke-Pathify @('Import',$script:importPath.Text,'-f') | Out-Null }
            } | Out-Null
            Add-Section 'Export' 246 | Out-Null
            Add-Label 'Save JSON to' 28 290 200 | Out-Null
            $exportPath = Add-TextBox (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'pathify-presets.json') 28 322 690 34
            $script:exportPath = $exportPath
            Add-Button 'Export presets' 28 372 170 {
                if ((Test-Path -LiteralPath $script:exportPath.Text) -and [System.Windows.Forms.MessageBox]::Show('Overwrite this file?','Confirm export','YesNo','Warning') -ne 'Yes') { return }
                Invoke-Pathify @('Export',$script:exportPath.Text,'-f') | Out-Null
            } | Out-Null
        }
        'Tools' {
            Add-CardPanel 18 48 ($body.ClientSize.Width - 54) 170 | Out-Null
            Add-CardPanel 18 238 ($body.ClientSize.Width - 54) 210 | Out-Null
            Add-Section 'Diagnostics and utilities' 18 | Out-Null
            Add-Button 'Validate presets' 28 64 180 { $result = Invoke-Pathify @('Validate'); [System.Windows.Forms.MessageBox]::Show($result,'Validation') | Out-Null } | Out-Null
            Add-Button 'Run Doctor' 225 64 150 { $result = Invoke-Pathify @('Doctor'); [System.Windows.Forms.MessageBox]::Show($result,'Pathify Doctor') | Out-Null } | Out-Null
            Add-Button 'Archive a folder' 400 64 180 { Start-Process -FilePath $exe -ArgumentList 'Archive Create' } | Out-Null
            Add-Label 'Managed shell' 28 138 200 | Out-Null
            $shell = New-Object System.Windows.Forms.ComboBox
            $shell.DropDownStyle = 'DropDownList'; [void]$shell.Items.AddRange([object[]]@('pwsh','powershell','cmd')); $shell.SelectedIndex = 0
            $shell.Location = New-Object System.Drawing.Point(28, 170); $shell.Size = New-Object System.Drawing.Size(200, 34); $body.Controls.Add($shell)
            $script:shellChoice = $shell
            Add-Button 'Launch shell' 250 166 160 { Invoke-Pathify @('Launch',[string]$script:shellChoice.SelectedItem) | Out-Null } | Out-Null
            Add-Section 'Advanced command' 246 | Out-Null
            Add-Label 'Run a complete Pathify command in its own terminal for restart options and other advanced actions.' 28 286 710 | Out-Null
            $commandLine = Add-TextBox 'Set <Preset> -rse' 28 322 690 36
            $script:commandLine = $commandLine
            Add-Button 'Open command' 28 374 170 {
                if (-not $script:commandLine.Text.Trim()) { throw 'Enter a Pathify command.' }
                Start-Process -FilePath $exe -ArgumentList $script:commandLine.Text
            } | Out-Null
        }
    }
    foreach ($control in $body.Controls) {
        $target = $control.Location
        $control.Location = New-Object System.Drawing.Point($target.X, ($target.Y + 12))
        $script:pageMotion += @{ Control = $control; TargetY = $target.Y }
    }
}
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 15
$timer.Add_Tick({
    $delta = $script:targetY - $indicator.Top
    if ([Math]::Abs($delta) -lt 1) { $indicator.Top = $script:targetY }
    else { $indicator.Top += [Math]::Sign($delta) * [Math]::Max(1, [Math]::Abs([int]($delta * 0.28))) }
    foreach ($motion in $script:pageMotion) {
        $control = $motion.Control
        if ($control.IsDisposed) { continue }
        $remaining = $motion.TargetY - $control.Top
        if ([Math]::Abs($remaining) -lt 2) {
            $control.Top = $motion.TargetY
        } else {
            $control.Top += [Math]::Sign($remaining) * [Math]::Max(1, [Math]::Abs([int]($remaining * 0.3)))
        }
    }
    $script:pageMotion = @($script:pageMotion | Where-Object { -not $_.Control.IsDisposed -and $_.Control.Top -ne $_.TargetY })
    $menuWidthTarget = if ($script:menuTargetOpen) { 248 } else { 72 }
    $menuDelta = $menuWidthTarget - $sidebar.Width
    if ([Math]::Abs($menuDelta) -lt 2) {
        $sidebar.Width = $menuWidthTarget
    }
    else {
        $sidebar.Width += [Math]::Sign($menuDelta) * [Math]::Max(2, [Math]::Abs([int]($menuDelta * 0.24)))
    }
    $shadowTarget = if ($script:menuTargetOpen) { $script:menuShadowTarget } else { 0 }
    if ($menuShadow.Alpha -lt $shadowTarget) {
        $menuShadow.Alpha = [Math]::Min($shadowTarget, ($menuShadow.Alpha + 9))
        $menuShadow.Visible = $true
        $menuShadow.Invalidate()
    }
    elseif ($menuShadow.Alpha -gt $shadowTarget) {
        $menuShadow.Alpha = [Math]::Max($shadowTarget, ($menuShadow.Alpha - 9))
        $menuShadow.Invalidate()
        if ($menuShadow.Alpha -eq 0) { $menuShadow.Visible = $false }
    }
})
$timer.Start()
Show-Page
[void]$form.ShowDialog()
$timer.Stop()
"####;
        let windows_directory =
            std::env::var_os("WINDIR").ok_or("WINDIR is not set; cannot start the Pathify GUI")?;
        let powershell = PathBuf::from(windows_directory)
            .join("System32")
            .join("WindowsPowerShell")
            .join("v1.0")
            .join("powershell.exe");
        if !powershell.is_file() {
            return Err(
                format!("Windows PowerShell was not found: {}", powershell.display()).into(),
            );
        }
        let timestamp = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
        let script_path = std::env::temp_dir().join(format!(
            "Pathify-Gui-{}-{timestamp}.ps1",
            std::process::id()
        ));
        let mut script_file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&script_path)?;
        let write_result = script_file
            .write_all(b"\xEF\xBB\xBF")
            .and_then(|()| script_file.write_all(script.as_bytes()))
            .and_then(|()| script_file.flush());
        if let Err(error) = write_result {
            let cleanup_result = fs::remove_file(&script_path);
            if let Err(cleanup_error) = cleanup_result {
                return Err(format!(
                    "Failed to write the GUI script ({error}) and remove its temporary file ({cleanup_error})"
                )
                .into());
            }
            return Err(error.into());
        }
        drop(script_file);
        use std::os::windows::process::CommandExt;
        let process_result = Command::new(powershell)
            .args([
                "-NoLogo",
                "-NoProfile",
                "-STA",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
            ])
            .arg(&script_path)
            .env("PATHIFY_EXE_PATH", executable)
            .creation_flags(0x08000000)
            .status();
        let cleanup_result = fs::remove_file(&script_path);
        let status = match process_result {
            Ok(status) => status,
            Err(error) => {
                if let Err(cleanup_error) = cleanup_result {
                    return Err(format!(
                        "Failed to start the GUI ({error}) and remove its temporary script ({cleanup_error})"
                    )
                    .into());
                }
                return Err(error.into());
            }
        };
        cleanup_result?;
        if !status.success() {
            return Err(format!("Pathify GUI exited with status {status}").into());
        }
        Ok(())
    }

    #[cfg(not(windows))]
    pub fn run_gui() -> Result<(), Box<dyn std::error::Error>> {
        Err("the Pathify GUI is supported only on Windows".into())
    }
}
