#[path = "../maker_core.rs"]
mod maker_core;

use std::io;

fn main() {
    if let Err(error) = run() {
        eprintln!("Component creation failed: {error}");
        std::process::exit(1);
    }
}

fn run() -> io::Result<()> {
    let args = std::env::args_os().skip(1).collect::<Vec<_>>();
    if args.len() != 1 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "usage: ReTermCompMaker.exe <component-directory>",
        ));
    }
    let directory = std::path::PathBuf::from(&args[0]);
    let stdin = io::stdin();
    let stdout = io::stdout();
    maker_core::create_interactive(&directory, &mut stdin.lock(), &mut stdout.lock())?;
    Ok(())
}
