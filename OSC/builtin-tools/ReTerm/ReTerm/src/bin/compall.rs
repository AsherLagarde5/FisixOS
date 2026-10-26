use std::io;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    if let Err(error) = run() {
        eprintln!("compall: {error}");
        std::process::exit(1);
    }
}

fn run() -> io::Result<()> {
    let args = std::env::args_os().skip(1).collect::<Vec<_>>();
    if args.len() > 1 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "usage: compall [folder]",
        ));
    }
    let folder = args
        .first()
        .map(PathBuf::from)
        .map(Ok)
        .unwrap_or_else(std::env::current_dir)?;
    if !folder.is_dir() {
        return Err(io::Error::new(
            io::ErrorKind::NotFound,
            format!("component folder does not exist: {}", folder.display()),
        ));
    }

    let shell = std::env::var_os("RETERM_EXECUTABLE").ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::NotFound,
            "ReTerm did not provide its executable path; run compall from ReTerm",
        )
    })?;
    let status = Command::new(shell)
        .arg("--compadd-dir")
        .arg(folder)
        .status()?;
    if status.success() {
        Ok(())
    } else {
        Err(io::Error::other(format!(
            "ReTerm reported component installation failures ({status})"
        )))
    }
}
