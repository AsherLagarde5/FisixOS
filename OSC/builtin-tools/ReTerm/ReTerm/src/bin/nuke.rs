use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::path::Path;

const ZERO_BUFFER_SIZE: usize = 64 * 1024;

fn main() {
    if let Err(error) = run() {
        eprintln!("nuke: {error}");
        std::process::exit(1);
    }
}

fn run() -> io::Result<()> {
    let args = std::env::args_os().skip(1).collect::<Vec<_>>();
    if args.len() != 1 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "usage: nuke <file>",
        ));
    }

    let path = Path::new(&args[0]);
    let bytes_written = zero_file(path)?;
    println!(
        "Overwrote {} bytes with zeros in {}.",
        bytes_written,
        path.display()
    );
    Ok(())
}

fn zero_file(path: &Path) -> io::Result<u64> {
    validate_target(&fs::symlink_metadata(path)?)?;

    let mut options = OpenOptions::new();
    options.write(true);

    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        options.custom_flags(0x0020_0000);
    }

    let mut file = options.open(path)?;
    let metadata = file.metadata()?;
    validate_target(&metadata)?;

    let length = metadata.len();
    let zeros = [0_u8; ZERO_BUFFER_SIZE];
    let mut remaining = length;
    while remaining > 0 {
        let count = remaining.min(zeros.len() as u64) as usize;
        file.write_all(&zeros[..count])?;
        remaining -= count as u64;
    }
    file.sync_all()?;
    Ok(length)
}

fn validate_target(metadata: &fs::Metadata) -> io::Result<()> {
    if !metadata.is_file() || metadata.file_type().is_symlink() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "target must be a regular file",
        ));
    }

    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        if metadata.file_attributes() & 0x400 != 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "reparse points and symbolic links are not supported",
            ));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn overwrites_file_with_zeros_without_changing_its_length() {
        let path = std::env::temp_dir().join(format!(
            "reterm-nuke-test-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::write(&path, b"data that must be replaced").unwrap();

        assert_eq!(zero_file(&path).unwrap(), 26);
        assert_eq!(fs::metadata(&path).unwrap().len(), 26);
        assert_eq!(fs::read(&path).unwrap(), vec![0; 26]);

        fs::remove_file(path).unwrap();
    }

    #[test]
    fn refuses_to_overwrite_a_directory() {
        let path = std::env::temp_dir().join(format!(
            "reterm-nuke-directory-test-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&path).unwrap();

        assert_eq!(
            zero_file(&path).unwrap_err().kind(),
            io::ErrorKind::InvalidInput
        );

        fs::remove_dir(path).unwrap();
    }
}
