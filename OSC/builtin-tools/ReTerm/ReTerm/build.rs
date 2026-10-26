use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    println!("cargo:rustc-check-cfg=cfg(has_go_rust_helper)");
    println!("cargo:rerun-if-changed=tools/rustc-helper/main.go");

    let target = env::var("TARGET").expect("Cargo must provide TARGET");
    let (goos, goarch) = match target.as_str() {
        "x86_64-pc-windows-msvc" | "x86_64-pc-windows-gnu" => ("windows", "amd64"),
        "x86_64-unknown-linux-gnu" => ("linux", "amd64"),
        _ => return,
    };
    let root = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("manifest directory"));
    let output = PathBuf::from(env::var_os("OUT_DIR").expect("build output directory")).join(
        if goos == "windows" {
            "reterm-rust-helper.exe"
        } else {
            "reterm-rust-helper"
        },
    );
    let result = Command::new("go")
        .args(["build", "-trimpath", "-ldflags=-s -w", "-o"])
        .arg(&output)
        .arg("main.go")
        .current_dir(root.join("tools/rustc-helper"))
        .env("GOOS", goos)
        .env("GOARCH", goarch)
        .env("CGO_ENABLED", "0")
        .output();

    match result {
        Ok(result) if result.status.success() => {
            println!("cargo:rustc-cfg=has_go_rust_helper");
            println!("cargo:rustc-env=RETERM_GO_RUST_HELPER={}", output.display());
        }
        Ok(result) => {
            if env::var_os("RETERM_REQUIRE_GO_HELPER").is_some() {
                panic!(
                    "Go helper build failed: {}",
                    String::from_utf8_lossy(&result.stderr)
                );
            }
            println!("cargo:warning=Go helper build failed; Rust modules will use PATH rustc");
        }
        Err(error) => {
            if env::var_os("RETERM_REQUIRE_GO_HELPER").is_some() {
                panic!("Go is required to build release binaries: {error}");
            }
            println!("cargo:warning=Go not available; Rust modules will use PATH rustc");
        }
    }
}
