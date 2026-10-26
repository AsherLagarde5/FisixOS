#[path = "../maker_core.rs"]
mod maker_core;

use std::io::{self, BufRead, Write};
use std::path::PathBuf;

fn main() {
    if let Err(error) = run() {
        eprintln!("ReTerm Module creation failed: {error}");
        std::process::exit(1);
    }
}

fn run() -> io::Result<()> {
    let args = std::env::args_os().skip(1).collect::<Vec<_>>();
    if args.is_empty() {
        println!("ReTerm ModMaker CLI — Source Module Builder");
        println!();
        println!("Builds encrypted ZIP-based ReTerm .mod packages from source files.");
        println!("Modules contain source only; ReTerm runs source with the required host runtime.");
        println!();
        println!("Usage: ReTermModMaker.exe <source-file> [output-path.mod]");
        println!("   or: ReTermModMaker.exe <directory-containing-source-file>");
        println!();
        println!("Supported source: Rust, Python, C, C++, Java, and ReTerm script (.rts).");
        println!("Install the matching host runtime/compiler to launch each module.");
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "missing input source file path",
        ));
    }

    let input_path = PathBuf::from(&args[0]);
    let stdin = io::stdin();
    let stdout = io::stdout();
    let mut stdin_lock = stdin.lock();
    let mut stdout_lock = stdout.lock();

    let (source_file, output_mod) = if input_path.is_file() {
        if args.len() > 2 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "usage: ReTermModMaker <source-file> [output-path.mod]",
            ));
        }
        let out = if args.len() > 1 {
            PathBuf::from(&args[1])
        } else {
            let stem = input_path.file_stem().unwrap_or_default().to_string_lossy();
            input_path
                .parent()
                .unwrap_or_else(|| std::path::Path::new("."))
                .join(format!("{stem}.mod"))
        };
        (input_path, out)
    } else if input_path.is_dir() {
        if args.len() > 1 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "directory mode accepts only the directory path",
            ));
        }
        writeln!(stdout_lock, "Module directory: {}", input_path.display())?;
        let main_rel = prompt(
            &mut stdin_lock,
            &mut stdout_lock,
            "Main source file (relative to folder, e.g. main.py): ",
        )?;
        let src = input_path.join(main_rel);
        let stem = input_path.file_name().unwrap_or_default().to_string_lossy();
        let out = input_path
            .parent()
            .unwrap_or_else(|| std::path::Path::new("."))
            .join(format!("{stem}.mod"));
        (src, out)
    } else {
        return Err(io::Error::new(
            io::ErrorKind::NotFound,
            format!("path does not exist: {}", input_path.display()),
        ));
    };

    let name = prompt(&mut stdin_lock, &mut stdout_lock, "Module name: ")?;
    let callby = prompt(&mut stdin_lock, &mut stdout_lock, "Command name (callby): ")?;
    let version = prompt(&mut stdin_lock, &mut stdout_lock, "Version (e.g. 1.0.0): ")?;
    let description = prompt(&mut stdin_lock, &mut stdout_lock, "Description: ")?;

    let created = maker_core::create_mod_package(
        &source_file,
        &output_mod,
        name,
        callby,
        version,
        description,
        None,
        None,
        None,
        None,
    )?;

    writeln!(stdout_lock, "Built ReTerm module: {}", created.display())?;
    Ok(())
}

fn prompt<R: BufRead, W: Write>(input: &mut R, output: &mut W, label: &str) -> io::Result<String> {
    write!(output, "{label}")?;
    output.flush()?;
    let mut value = String::new();
    if input.read_line(&mut value)? == 0 {
        return Err(io::Error::new(
            io::ErrorKind::UnexpectedEof,
            "input ended while creating module",
        ));
    }
    let trimmed = value.trim().to_owned();
    if trimmed.is_empty() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "field must not be empty",
        ));
    }
    Ok(trimmed)
}
