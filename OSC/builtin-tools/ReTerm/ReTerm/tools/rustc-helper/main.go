package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

func main() {
	source := flag.String("source", "", "Rust source file to compile")
	output := flag.String("output", "", "output executable path")
	flag.Parse()
	if *source == "" || *output == "" {
		fatal("usage: reterm-rust-helper --source <file.rs> --output <executable>")
	}

	rustc, env, err := resolveRustc()
	if err != nil {
		fatal(err.Error())
	}
	command := exec.Command(rustc, "--crate-name", "reterm_module", "--edition=2021", "-o", *output, *source)
	command.Env = env
	command.Stdout = os.Stdout
	command.Stderr = os.Stderr
	if err := command.Run(); err != nil {
		fatal(fmt.Sprintf("Rust source compilation failed: %v", err))
	}
}

func resolveRustc() (string, []string, error) {
	if compiler, err := exec.LookPath("rustc"); err == nil {
		return compiler, os.Environ(), nil
	}
	host, err := rustHost()
	if err != nil {
		return "", nil, err
	}
	cache, err := os.UserCacheDir()
	if err != nil {
		return "", nil, fmt.Errorf("could not locate the user cache: %w", err)
	}
	root := filepath.Join(cache, "ReTerm", "rust-toolchain")
	cargoHome := filepath.Join(root, "cargo")
	rustupHome := filepath.Join(root, "rustup")
	rustcName := "rustc"
	rustupName := "rustup-init"
	if runtime.GOOS == "windows" {
		rustcName += ".exe"
		rustupName += ".exe"
	}
	toolEnv := withEnv(os.Environ(), "CARGO_HOME", cargoHome)
	toolEnv = withEnv(toolEnv, "RUSTUP_HOME", rustupHome)
	rustc := filepath.Join(cargoHome, "bin", rustcName)
	if _, err := os.Stat(rustc); err != nil {
		if err := os.MkdirAll(root, 0o700); err != nil {
			return "", nil, fmt.Errorf("could not create the Rust toolchain cache: %w", err)
		}
		installer := filepath.Join(root, rustupName)
		if err := downloadRustup(host, installer); err != nil {
			return "", nil, err
		}
		args := []string{"-y", "--no-modify-path", "--profile", "minimal", "--default-host", host, "--default-toolchain", "stable"}
		if err := runInstaller(installer, args, toolEnv); err != nil {
			return "", nil, err
		}
		_ = os.Remove(installer)
	}
	if _, err := os.Stat(rustc); err != nil {
		return "", nil, fmt.Errorf("rustup completed but rustc is missing from %s", filepath.Dir(rustc))
	}
	return rustc, toolEnv, nil
}

func rustHost() (string, error) {
	if runtime.GOARCH != "amd64" {
		return "", fmt.Errorf("managed Rust toolchains are currently available only for x86-64 hosts")
	}
	switch runtime.GOOS {
	case "windows":
		return "x86_64-pc-windows-gnu", nil
	case "linux":
		return "x86_64-unknown-linux-gnu", nil
	default:
		return "", fmt.Errorf("managed Rust toolchains are not available for %s", runtime.GOOS)
	}
}

func downloadRustup(host, destination string) error {
	name := "rustup-init"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	url := "https://static.rust-lang.org/rustup/dist/" + host + "/" + name
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Minute)
	defer cancel()
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return err
	}
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		return fmt.Errorf("could not download rustup from %s: %w", url, err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("rustup download failed: HTTP %s", response.Status)
	}
	file, err := os.OpenFile(destination, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o700)
	if err != nil {
		return fmt.Errorf("could not create rustup installer: %w", err)
	}
	_, copyErr := io.Copy(file, io.LimitReader(response.Body, 100<<20))
	closeErr := file.Close()
	if copyErr != nil {
		_ = os.Remove(destination)
		return fmt.Errorf("could not save rustup installer: %w", copyErr)
	}
	if closeErr != nil {
		_ = os.Remove(destination)
		return closeErr
	}
	return nil
}

func runInstaller(path string, args, env []string) error {
	command := exec.Command(path, args...)
	command.Env = env
	command.Stdout = os.Stdout
	command.Stderr = os.Stderr
	if err := command.Run(); err != nil {
		return fmt.Errorf("could not install the managed Rust toolchain: %w", err)
	}
	return nil
}

func withEnv(env []string, key, value string) []string {
	result := make([]string, 0, len(env)+1)
	for _, entry := range env {
		name, _, ok := strings.Cut(entry, "=")
		if !ok || !strings.EqualFold(name, key) {
			result = append(result, entry)
		}
	}
	return append(result, key+"="+value)
}

func fatal(message string) {
	fmt.Fprintln(os.Stderr, message)
	os.Exit(1)
}
