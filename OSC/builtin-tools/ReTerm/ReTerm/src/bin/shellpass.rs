use std::io;
#[cfg(windows)]
use std::process::Command;

fn main() {
    if let Err(error) = run() {
        eprintln!("shellpass: {error}");
        std::process::exit(1);
    }
}

fn run() -> io::Result<()> {
    let args = std::env::args().skip(1).collect::<Vec<_>>();
    let command_text = command_text(&args)?;

    #[cfg(windows)]
    {
        let status = Command::new("cmd.exe")
            .arg("/D")
            .arg("/C")
            .arg(command_text)
            .status()?;
        std::process::exit(status.code().unwrap_or(1));
    }

    #[cfg(not(windows))]
    {
        let _ = command_text;
        Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "shellpass runs commands through cmd.exe and is only available on Windows",
        ))
    }
}

fn command_text(args: &[String]) -> io::Result<String> {
    if args.is_empty() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "usage: shellpass <cmd command>",
        ));
    }
    Ok(args.join(" "))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn joins_command_arguments_for_cmd() {
        assert_eq!(
            command_text(&["echo".to_owned(), "hello".to_owned()]).unwrap(),
            "echo hello"
        );
    }

    #[test]
    fn requires_a_command() {
        assert_eq!(
            command_text(&[]).unwrap_err().kind(),
            io::ErrorKind::InvalidInput
        );
    }
}
