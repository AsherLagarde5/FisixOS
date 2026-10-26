//! DOSIFY – Windows CMD-style shell emulator for Linux
//! Interactive CLI that maps common `cmd.exe` commands to Linux equivalents.

use std::collections::HashMap;
use std::env;
use std::fs;
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use colored::*;
use regex::Regex;
use rustyline::error::ReadlineError;
use rustyline::DefaultEditor;
use which::which;

const MARKER_FILE: &str = ".dosify_deps_installed";
const VERSION: &str = env!("CARGO_PKG_VERSION");

/// Recommended system packages that improve command coverage.
const DEPS: &[(&str, &str)] = &[
    ("whois", "whois"),
    ("tree", "tree"),
    ("traceroute", "traceroute"),
    ("net-tools", "netstat"),
    ("dnsutils", "nslookup"),
    ("curl", "curl"),
    ("iproute2", "ip"),
];

fn main() {
    match env::args().nth(1).as_deref() {
        Some("--help" | "-h") => {
            print_help();
            return;
        }
        Some("--version" | "-V") => {
            println!("DOSIFY {VERSION} (Linux CMD emulator)");
            return;
        }
        _ => {}
    }

    // Graceful Ctrl-C
    let running = Arc::new(AtomicBool::new(true));
    let r = running.clone();
    let _ = ctrlc::set_handler(move || {
        r.store(false, Ordering::SeqCst);
        println!();
    });

    // First-run dependency prompt
    first_run_setup();

    println!(
        "{} Type '{}' to leave. Type '{}' for a short overview.",
        "DOSIFY shell active.".green().bold(),
        "exit".cyan(),
        "help".cyan()
    );

    let mut errorlevel: i32 = 0;
    let mut rl = DefaultEditor::new().expect("Failed to create readline editor");

    // Optional history file
    let hist_path = dirs::home_dir().map(|h| h.join(".dosify_history"));
    if let Some(ref p) = hist_path {
        let _ = rl.load_history(p);
    }

    while running.load(Ordering::SeqCst) {
        let cwd = env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
        let prompt = format!("{}> ", to_winpath(&cwd));
        let readline = rl.readline(&prompt);

        match readline {
            Ok(line) => {
                let line = line.trim();
                if line.is_empty() {
                    continue;
                }
                let _ = rl.add_history_entry(line);

                if line.eq_ignore_ascii_case("exit") || line.eq_ignore_ascii_case("quit") {
                    break;
                }

                // .bat detection
                let first = line.split_whitespace().next().unwrap_or("");
                if first.to_lowercase().ends_with(".bat") {
                    if !Path::new(first).exists() {
                        println!("File not found: {}", first);
                        errorlevel = 1;
                        continue;
                    }
                    errorlevel = run_bat(first);
                    continue;
                }

                // Expand %ERRORLEVEL% and simple %VAR%
                let expanded = expand_vars(line, errorlevel);
                errorlevel = dispatch(&expanded, errorlevel);
            }
            Err(ReadlineError::Interrupted) => {
                // Ctrl-C
                println!("^C");
                continue;
            }
            Err(ReadlineError::Eof) => break,
            Err(err) => {
                eprintln!("Readline error: {:?}", err);
                break;
            }
        }
    }

    if let Some(ref p) = hist_path {
        let _ = rl.save_history(p);
    }
    println!("Goodbye.");
}

// ---------------------------------------------------------------------------
// First-run dependency installer
// ---------------------------------------------------------------------------
fn first_run_setup() {
    let marker = dirs::home_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join(MARKER_FILE);

    if marker.exists() {
        return;
    }

    println!(
        "{}",
        "==============================================".cyan()
    );
    println!("{}", "  DOSIFY first-run setup".cyan().bold());
    println!(
        "{}",
        "==============================================".cyan()
    );
    println!();
    println!("Recommended packages for better CMD compatibility:");
    println!("  • whois       – domain lookup");
    println!("  • tree        – graphical directory tree");
    println!("  • traceroute  – path tracing (tracert)");
    println!("  • net-tools   – netstat, arp, route");
    println!("  • dnsutils    – nslookup, dig");
    println!("  • curl        – data transfer");
    println!("  • iproute2    – ip, ss (modern ipconfig/netstat)");
    println!();
    println!("PowerShell (pwsh) is optional and not auto-installed.");
    println!();

    let mut missing: Vec<&str> = Vec::new();
    for (pkg, bin) in DEPS {
        if which(bin).is_err() {
            missing.push(pkg);
        }
    }

    if missing.is_empty() {
        println!("All recommended packages are already installed.");
        let _ = fs::write(&marker, b"");
        println!("Setup complete. Starting DOSIFY...");
        std::thread::sleep(std::time::Duration::from_secs(1));
        return;
    }

    println!("Missing: {}", missing.join(" "));
    print!("Install them now? [Y/n] ");
    let _ = io::stdout().flush();

    let mut answer = String::new();
    let _ = io::stdin().read_line(&mut answer);
    let answer = answer.trim();
    let yes =
        answer.is_empty() || answer.eq_ignore_ascii_case("y") || answer.eq_ignore_ascii_case("yes");

    if yes {
        println!();
        println!("Installing {} ...", missing.join(" "));
        if which("apt-get").is_ok() {
            let status = Command::new("apt-get").args(["update", "-qq"]).status();
            if status.map(|s| s.success()).unwrap_or(false) {
                let status = Command::new("apt-get")
                    .arg("install")
                    .arg("-y")
                    .args(&missing)
                    .status();
                if status.map(|s| s.success()).unwrap_or(false) {
                    println!("{}", "Installation successful.".green());
                } else {
                    println!("{}", "Installation failed (or partially failed).".yellow());
                }
            }
        } else {
            println!("apt-get not found. Please install the packages manually.");
        }
    } else {
        println!("Skipping installation. You can install later with:");
        println!("  apt-get install -y {}", missing.join(" "));
    }

    let _ = fs::write(&marker, b"");
    println!();
    println!("Starting DOSIFY...");
    std::thread::sleep(std::time::Duration::from_secs(1));
}

// ---------------------------------------------------------------------------
// Path translation (Linux → fake Windows)
// ---------------------------------------------------------------------------
fn to_winpath(p: &Path) -> String {
    let s = p.to_string_lossy().to_string();
    let home = dirs::home_dir().unwrap_or_else(|| PathBuf::from("/home"));
    let home_str = home.to_string_lossy();

    if s.starts_with(home_str.as_ref()) {
        let sub = &s[home_str.len()..];
        let user = env::var("USER").unwrap_or_else(|_| "User".into());
        let user_cap = {
            let mut c = user.chars();
            match c.next() {
                None => String::new(),
                Some(f) => f.to_uppercase().collect::<String>() + c.as_str(),
            }
        };
        format!("C:\\Users\\{}{}", user_cap, sub.replace('/', "\\"))
    } else if s.starts_with("/etc") {
        format!("C:\\Windows\\System32{}", s[4..].replace('/', "\\"))
    } else if s.starts_with("/tmp") {
        format!("C:\\Temp{}", s[4..].replace('/', "\\"))
    } else if s.starts_with("/var/log") {
        format!("C:\\Windows\\Logs{}", s[8..].replace('/', "\\"))
    } else {
        format!("C:\\{}", s.trim_start_matches('/').replace('/', "\\"))
    }
}

// ---------------------------------------------------------------------------
// Variable expansion
// ---------------------------------------------------------------------------
fn expand_vars(line: &str, errorlevel: i32) -> String {
    let mut out = line.replace("%ERRORLEVEL%", &errorlevel.to_string());
    // Simple %VAR% → env lookup
    let re = Regex::new(r"%([A-Za-z0-9_]+)%").unwrap();
    out = re
        .replace_all(&out, |caps: &regex::Captures| {
            let name = &caps[1];
            env::var(name).unwrap_or_else(|_| format!("%{}%", name))
        })
        .to_string();
    // !VAR! delayed expansion style
    let re2 = Regex::new(r"!([A-Za-z0-9_]+)!").unwrap();
    out = re2
        .replace_all(&out, |caps: &regex::Captures| {
            let name = &caps[1];
            env::var(name).unwrap_or_else(|_| format!("!{}!", name))
        })
        .to_string();
    out
}

// ---------------------------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------------------------
fn dispatch(line: &str, _prev_errorlevel: i32) -> i32 {
    let parts: Vec<&str> = line.split_whitespace().collect();
    if parts.is_empty() {
        return 0;
    }

    let cmd = parts[0].to_lowercase();
    let args: Vec<&str> = parts[1..].to_vec();
    let args_joined = args.join(" ");

    // Drive letter support (C:, D:, …)
    if cmd.len() == 2 && cmd.ends_with(':') {
        let drive = cmd.chars().next().unwrap().to_ascii_uppercase();
        match drive {
            'C' => {
                if let Some(h) = dirs::home_dir() {
                    let _ = env::set_current_dir(h);
                }
            }
            'D' => {
                if env::set_current_dir("/mnt/d").is_err() {
                    println!("The system cannot find the drive specified.");
                    return 1;
                }
            }
            'E' => {
                if env::set_current_dir("/mnt/e").is_err() {
                    println!("The system cannot find the drive specified.");
                    return 1;
                }
            }
            _ => {
                println!("The system cannot find the drive specified.");
                return 1;
            }
        }
        return 0;
    }

    match cmd.as_str() {
        // Directory & file
        "dir" => {
            let mut v = vec!["-la"];
            v.extend(args.iter().copied());
            run_external("ls", &v)
        }
        "cd" | "chdir" => {
            if args.is_empty() {
                if let Some(h) = dirs::home_dir() {
                    let _ = env::set_current_dir(h);
                }
                0
            } else {
                match env::set_current_dir(args[0]) {
                    Ok(()) => 0,
                    Err(_) => {
                        println!("The system cannot find the path specified.");
                        1
                    }
                }
            }
        }
        "md" | "mkdir" => {
            let mut v = vec!["-p"];
            v.extend(args.iter().copied());
            run_external("mkdir", &v)
        }
        "rd" | "rmdir" => {
            let status = run_external("rmdir", &args);
            if status != 0 {
                println!("The directory is not empty or does not exist.");
            }
            status
        }
        "del" | "erase" | "deltree" => {
            if args.is_empty() {
                println!("The syntax of the command is incorrect.");
                return 1;
            }
            print!("Are you sure (Y/N)? ");
            let _ = io::stdout().flush();
            let mut confirm = String::new();
            let _ = io::stdin().read_line(&mut confirm);
            if confirm.trim().eq_ignore_ascii_case("y") {
                if cmd == "deltree" {
                    let mut v = vec!["-rf"];
                    v.extend(args.iter().copied());
                    run_external("rm", &v)
                } else {
                    let mut v = vec!["-f"];
                    v.extend(args.iter().copied());
                    run_external("rm", &v)
                }
            } else {
                0
            }
        }
        "copy" | "xcopy" => {
            let mut v = vec!["-ri"];
            v.extend(args.iter().copied());
            run_external("cp", &v)
        }
        "move" | "ren" | "rename" => {
            let mut v = vec!["-i"];
            v.extend(args.iter().copied());
            run_external("mv", &v)
        }
        "type" => run_external("cat", &args),
        "more" => run_external("less", &args),
        "tree" => {
            if which("tree").is_ok() {
                run_external("tree", &args)
            } else {
                let path = if args.is_empty() { "." } else { args[0] };
                run_external("find", &[path, "-print"])
            }
        }
        "robocopy" => {
            let mut v = vec!["-av"];
            v.extend(args.iter().copied());
            run_external("rsync", &v)
        }
        "where" => {
            if args.is_empty() {
                println!("The syntax of the command is incorrect.");
                1
            } else if let Ok(p) = which(args[0]) {
                println!("{}", p.display());
                0
            } else {
                println!("Could not find files");
                1
            }
        }
        "attrib" => {
            if which("lsattr").is_ok() {
                run_external("lsattr", &args)
            } else {
                let mut v = vec!["-l"];
                v.extend(args.iter().copied());
                run_external("ls", &v)
            }
        }
        "cacls" | "icacls" | "xcacls" => {
            if which("getfacl").is_ok() {
                run_external("getfacl", &args)
            } else {
                let mut v = vec!["-l"];
                v.extend(args.iter().copied());
                run_external("ls", &v)
            }
        }
        "mklink" => {
            if args.len() < 2 {
                println!("Syntax: mklink <link> <target>");
                1
            } else {
                // mklink link target → ln -s target link
                run_external("ln", &["-s", args[1], args[0]])
            }
        }
        "pushd" => {
            let old = env::current_dir().ok();
            if let Some(a) = args.get(0) {
                if env::set_current_dir(a).is_ok() {
                    if let Some(o) = old {
                        env::set_var("DOSIFY_PUSHD", o.to_string_lossy().as_ref());
                    }
                    0
                } else {
                    println!("The system cannot find the path specified.");
                    1
                }
            } else {
                1
            }
        }
        "popd" => {
            if let Ok(prev) = env::var("DOSIFY_PUSHD") {
                let _ = env::set_current_dir(&prev);
                env::remove_var("DOSIFY_PUSHD");
                0
            } else {
                println!("No previous directory saved.");
                1
            }
        }

        // Screen & session
        "cls" => {
            print!("\x1B[2J\x1B[1;1H");
            let _ = io::stdout().flush();
            0
        }
        "ver" => {
            println!("{}  DOSIFY (Linux CMD emulator) v{}", uname_r(), VERSION);
            0
        }
        "hostname" => run_external("hostname", &[]),
        "whoami" => run_external("whoami", &[]),
        "systeminfo" => {
            println!("=== System ===");
            let _ = run_external("uname", &["-a"]);
            println!("\n=== Memory ===");
            let _ = run_external("free", &["-h"]);
            println!("\n=== Disk ===");
            let _ = run_external("df", &["-h"]);
            0
        }
        "title" => {
            print!("\x1B]0;{}\x07", args_joined);
            let _ = io::stdout().flush();
            0
        }
        "color" | "prompt" => 0,
        "pause" => {
            print!("Press any key to continue...");
            let _ = io::stdout().flush();
            let mut b = [0u8; 1];
            let _ = io::stdin().read(&mut b);
            println!();
            0
        }
        "help" => {
            print_help();
            0
        }

        // Date / time
        "date" => run_external("date", &["+%D"]),
        "time" => run_external("date", &["+%T"]),
        "timeout" => {
            let secs = args.get(0).and_then(|s| s.parse::<u64>().ok()).unwrap_or(1);
            std::thread::sleep(std::time::Duration::from_secs(secs));
            0
        }

        // Network
        "ipconfig" => run_external("ip", &["a"]),
        "ping" => run_external("ping", &args),
        "arp" => {
            if which("arp").is_ok() {
                run_external("arp", &args)
            } else {
                let mut v = vec!["neigh"];
                v.extend(args.iter().copied());
                run_external("ip", &v)
            }
        }
        "netstat" => {
            if which("netstat").is_ok() {
                run_external("netstat", &args)
            } else {
                run_external("ss", &args)
            }
        }
        "nslookup" => run_external("nslookup", &args),
        "tracert" | "traceroute" => {
            if which("traceroute").is_ok() {
                run_external("traceroute", &args)
            } else {
                run_external("tracepath", &args)
            }
        }
        "route" => {
            if which("ip").is_ok() {
                let mut v = vec!["route"];
                v.extend(args.iter().copied());
                run_external("ip", &v)
            } else {
                run_external("route", &args)
            }
        }
        "curl" => run_external("curl", &args),
        "whois" => run_external("whois", &args),
        "getmac" => run_external("ip", &["link", "show"]),

        // Process
        "tasklist" | "qprocess" | "tlist" => run_external("ps", &["aux"]),
        "taskkill" | "tskill" => run_external("kill", &args),

        // Search / text
        "findstr" | "find" => run_external("grep", &args),
        "sort" => run_external("sort", &args),
        "fc" | "comp" | "windiff" => {
            let mut v = vec!["-u"];
            v.extend(args.iter().copied());
            run_external("diff", &v)
        }
        "strings" => run_external("strings", &args),

        // Environment
        "set" => {
            if args.is_empty() {
                let mut vars: Vec<_> = env::vars().collect();
                vars.sort_by(|a, b| a.0.cmp(&b.0));
                for (k, v) in vars {
                    println!("{}={}", k, v);
                }
                0
            } else if let Some(eq) = args_joined.find('=') {
                let key = &args_joined[..eq];
                let val = &args_joined[eq + 1..];
                env::set_var(key, val);
                0
            } else {
                for (k, v) in env::vars() {
                    if k.eq_ignore_ascii_case(&args_joined) {
                        println!("{}={}", k, v);
                    }
                }
                0
            }
        }
        "path" => {
            if args.is_empty() {
                if let Ok(p) = env::var("PATH") {
                    println!("{}", p);
                }
                0
            } else {
                env::set_var("PATH", &args_joined);
                0
            }
        }
        "setx" => {
            println!(
                "SETX is not fully supported (persistent env vars require shell profile edits)."
            );
            1
        }

        // Misc
        "doskey" => {
            if args_joined.eq_ignore_ascii_case("/history") {
                println!("(history is available via arrow keys)");
                0
            } else {
                println!("Only 'doskey /history' is currently supported.");
                1
            }
        }
        "vol" => {
            let _ = run_external("df", &["-h", "."]);
            println!();
            let _ = run_external("lsblk", &[]);
            0
        }
        "label" => {
            println!("Disk label changes are limited on Linux.");
            0
        }
        "start" => {
            if args.is_empty() {
                1
            } else {
                let mut child = Command::new(args[0]);
                child.args(&args[1..]);
                child.stdout(Stdio::null()).stderr(Stdio::null());
                match child.spawn() {
                    Ok(_) => {
                        println!("Started.");
                        0
                    }
                    Err(e) => {
                        eprintln!("{}", e);
                        1
                    }
                }
            }
        }
        "tar" => run_external("tar", &args),
        "ssh" => run_external("ssh", &args),
        "ftp" => run_external("ftp", &args),
        "print" => {
            println!("Printing not supported in this emulator");
            0
        }
        "rem" | "::" => 0,
        "echo" => {
            if args_joined.eq_ignore_ascii_case("off") || args_joined.eq_ignore_ascii_case("on") {
                0
            } else {
                println!("{}", args_joined);
                0
            }
        }

        // Fallback – try to run the command as-is
        _ => {
            if which(&cmd).is_err() {
                println!(
                    "'{}' is not recognized as an internal or external command,",
                    cmd
                );
                println!("operable program or batch file.");
                1
            } else {
                run_external(&cmd, &args)
            }
        }
    }
}

fn run_external(cmd: &str, args: &[&str]) -> i32 {
    match Command::new(cmd).args(args).status() {
        Ok(s) => s.code().unwrap_or(1),
        Err(e) => {
            eprintln!("{}: {}", cmd, e);
            1
        }
    }
}

fn uname_r() -> String {
    Command::new("uname")
        .arg("-r")
        .output()
        .ok()
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .map(|s| s.trim().to_string())
        .unwrap_or_else(|| "unknown".into())
}

fn print_help() {
    println!(
        r#"DOSIFY - Windows CMD emulator (Rust)
Common supported commands:
  dir, cd, md, rd, del, copy, xcopy, move, ren, type, more, tree
  cls, ver, hostname, whoami, systeminfo, date, time, timeout, pause
  ipconfig, ping, arp, netstat, nslookup, tracert, route, curl, whois
  tasklist, taskkill, find, findstr, sort, fc, where, attrib
  set, path, pushd, popd, start, robocopy, tar, mklink
  help, exit

Many other CMD commands will attempt to run the Linux equivalent
or show a "not recognized" message. .bat files are partially supported.
"#
    );
}

// ---------------------------------------------------------------------------
// Very basic .bat runner (labels + goto + mapped commands)
// ---------------------------------------------------------------------------
fn run_bat(path: &str) -> i32 {
    let content = match fs::read_to_string(path) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("Cannot read {}: {}", path, e);
            return 1;
        }
    };

    let mut labels: HashMap<String, usize> = HashMap::new();
    let mut lines: Vec<String> = Vec::new();

    for raw in content.lines() {
        let mut line = raw.trim_end_matches('\r').to_string();
        if line.starts_with('@') {
            line = line[1..].to_string();
        }
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        if trimmed.starts_with(':') {
            let label = trimmed[1..]
                .split_whitespace()
                .next()
                .unwrap_or("")
                .to_string();
            if !label.is_empty() {
                labels.insert(label.to_lowercase(), lines.len());
            }
            continue;
        }
        lines.push(line);
    }

    let mut ip: usize = 0;
    let mut errorlevel = 0i32;

    while ip < lines.len() {
        let bline = &lines[ip];
        ip += 1;

        let expanded = expand_vars(bline, errorlevel);
        let parts: Vec<&str> = expanded.split_whitespace().collect();
        if parts.is_empty() {
            continue;
        }
        let cmd = parts[0].to_lowercase();

        if cmd == "goto" {
            if let Some(target) = parts.get(1) {
                let key = target.to_lowercase();
                if let Some(&idx) = labels.get(&key) {
                    ip = idx;
                    continue;
                } else {
                    eprintln!("Label not found: {}", target);
                    errorlevel = 1;
                }
            }
            continue;
        }
        if cmd == "rem" || cmd == "::" {
            continue;
        }
        if cmd == "pause" {
            print!("Press any key to continue...");
            let _ = io::stdout().flush();
            let mut b = [0u8; 1];
            let _ = io::stdin().read(&mut b);
            println!();
            continue;
        }

        // Re-use the main dispatcher
        errorlevel = dispatch(&expanded, errorlevel);
    }

    errorlevel
}
