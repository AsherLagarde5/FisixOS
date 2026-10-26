# DOSIFY

DOSIFY is a Linux CMD-style shell emulator. Its `.mod` package is not included:
ReTerm modules contain source code only, and DOSIFY is a Cargo application
which depends on external crates and multiple host commands.

## Build

From this directory, with Rust/Cargo installed:

```sh
cargo build --release --locked
```

Run the resulting `target/release/dosify` executable on Linux. DOSIFY maps
commands to Linux tools and is not a Windows-native DOS shell.
