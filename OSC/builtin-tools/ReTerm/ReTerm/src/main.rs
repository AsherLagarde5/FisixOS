#[path = "maker_core.rs"]
mod maker_core;
#[path = "mod_crypto.rs"]
mod mod_crypto;
mod nano;

mod components {
    use super::mod_crypto;
    use serde::{Deserialize, Serialize};
    use std::collections::HashSet;
    use std::ffi::OsStr;
    use std::fs::{self, File, OpenOptions};
    use std::io::{self, Cursor, Read, Write};
    use std::path::{Component, Path, PathBuf};
    use std::process::{Command, ExitStatus, Output, Stdio};
    use zip::ZipArchive;

    const COMPONENT_PASSWORD: &[u8] = b"1337devkey";
    const MAX_COMPONENT_SIZE: u64 = 512 * 1024 * 1024;
    const MAX_ROADMAP_SIZE: u64 = 64 * 1024;

    #[cfg(has_go_rust_helper)]
    const GO_RUST_HELPER_BYTES: &[u8] = include_bytes!(env!("RETERM_GO_RUST_HELPER"));

    #[derive(Clone, Debug, Deserialize, Serialize)]
    pub struct Roadmap {
        header: String,
        exec: String,
        pub name: String,
        pub callby: String,
        pub version: String,
        pub description: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        pub author: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        pub license: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        pub default_args: Option<Vec<String>>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        pub env: Option<std::collections::HashMap<String, String>>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        encrypted_exec: Option<bool>,
    }

    impl Roadmap {
        fn validate(&self) -> io::Result<PathBuf> {
            if self.header != "rdmapv1" && self.header != "rdmapv2" && self.header != "rdmapv3" {
                return Err(invalid_data("unsupported component roadmap header"));
            }
            if self.name.trim().is_empty()
                || self.version.trim().is_empty()
                || self.description.trim().is_empty()
            {
                return Err(invalid_data(
                    "component name, version, and description must not be empty",
                ));
            }
            if self.callby.is_empty()
                || !self
                    .callby
                    .chars()
                    .all(|ch| ch.is_ascii_alphanumeric() || matches!(ch, '_' | '-' | '.'))
            {
                return Err(invalid_data(
                    "component callby may contain only ASCII letters, numbers, '.', '_' and '-'",
                ));
            }

            if let Some(env_map) = &self.env {
                for key in env_map.keys() {
                    if key.is_empty()
                        || !key
                            .chars()
                            .all(|ch| ch.is_ascii_alphanumeric() || ch == '_')
                    {
                        return Err(invalid_data(format!(
                            "component environment variable key `{key}` is invalid"
                        )));
                    }
                }
            }

            let exec = Path::new(&self.exec);
            if self.exec.contains('\\')
                || exec.is_absolute()
                || exec
                    .components()
                    .any(|component| !matches!(component, Component::Normal(_)))
            {
                return Err(invalid_data("component exec must be a safe relative path"));
            }
            let extension = exec
                .extension()
                .and_then(OsStr::to_str)
                .unwrap_or_default()
                .to_ascii_lowercase();
            if !is_supported_component_extension(&extension) {
                return Err(invalid_data(
                    "component exec must reference a supported executable, script, or portable bytecode file",
                ));
            }
            Ok(exec.to_path_buf())
        }
    }

    pub const RMOD_MAGIC: &[u8; 8] = b"RMODv6\0\xFF";
    const PREVIOUS_RMOD_MAGIC: &[u8; 8] = b"RMODv5\0\xFF";

    pub fn parse_mod_bytes(contents: &[u8]) -> io::Result<(Roadmap, Vec<u8>)> {
        if contents.len() < RMOD_MAGIC.len() {
            return Err(invalid_data(
                "invalid ReTerm module file (header is truncated)",
            ));
        }
        let magic = &contents[0..8];
        if magic == RMOD_MAGIC {
            let max_container_size =
                MAX_COMPONENT_SIZE as usize + MAX_ROADMAP_SIZE as usize + 16 + 32;
            if contents.len() > max_container_size {
                return Err(invalid_data("ReTerm module exceeds the 512 MiB size limit"));
            }
            let archive_bytes = mod_crypto::decrypt_mod_payload(&contents[8..])?;
            return parse_source_mod_archive(&archive_bytes);
        }
        let (manifest_bytes, payload_bytes) = if magic == PREVIOUS_RMOD_MAGIC {
            let max_container_size =
                MAX_COMPONENT_SIZE as usize + MAX_ROADMAP_SIZE as usize + 4 + 8 + 16 + 32;
            if contents.len() > max_container_size {
                return Err(invalid_data("ReTerm module exceeds the 512 MiB size limit"));
            }
            let container = mod_crypto::decrypt_mod_payload(&contents[8..])?;
            if container.len() < 4 {
                return Err(invalid_data("ReTerm module container is truncated"));
            }
            let manifest_len = u32::from_le_bytes(container[..4].try_into().unwrap()) as usize;
            if manifest_len == 0 || manifest_len > MAX_ROADMAP_SIZE as usize {
                return Err(invalid_data(
                    "ReTerm module manifest exceeds the 64 KiB size limit",
                ));
            }
            let manifest_end = 4_usize
                .checked_add(manifest_len)
                .ok_or_else(|| invalid_data("ReTerm module manifest length overflows"))?;
            let payload_len = container
                .len()
                .checked_sub(manifest_end)
                .ok_or_else(|| invalid_data("ReTerm module container is truncated"))?;
            if payload_len == 0 || payload_len as u64 > MAX_COMPONENT_SIZE {
                return Err(invalid_data(
                    "ReTerm module source payload is empty or too large",
                ));
            }
            (
                container[4..manifest_end].to_vec(),
                container[manifest_end..].to_vec(),
            )
        } else {
            return Err(invalid_data(
                "invalid ReTerm module file (unsupported header signature)",
            ));
        };
        let roadmap: Roadmap = serde_json::from_slice(&manifest_bytes).map_err(|error| {
            invalid_data(format!(
                "failed to parse ReTerm module rdmapv3 manifest: {error}"
            ))
        })?;
        if roadmap.header != "rdmapv3" {
            return Err(invalid_data(
                "ReTerm module roadmap header must be `rdmapv3`",
            ));
        }
        let exec_path = roadmap.validate()?;
        let extension = exec_path
            .extension()
            .and_then(OsStr::to_str)
            .unwrap_or_default()
            .to_ascii_lowercase();
        if is_supported_mod_source_extension(&extension) {
            let source = std::str::from_utf8(&payload_bytes).map_err(|error| {
                invalid_data(format!("module source is not valid UTF-8: {error}"))
            })?;
            if payload_bytes.starts_with(b"MZ") {
                return Err(invalid_data(
                    "native executable and library binaries cannot be used as source modules",
                ));
            }
            if extension == "rts" {
                crate::maker_core::compile_source_to_bytecode(source, "rts")?;
            }
        } else {
            return Err(invalid_data(
                "ReTerm .mod files must contain supported source code",
            ));
        }
        Ok((roadmap, payload_bytes))
    }

    fn parse_source_mod_archive(archive_bytes: &[u8]) -> io::Result<(Roadmap, Vec<u8>)> {
        let mut archive = ZipArchive::new(Cursor::new(archive_bytes)).map_err(zip_error)?;
        if archive.len() != 2 {
            return Err(invalid_data(
                "source module archive must contain exactly a manifest and one source file",
            ));
        }

        let mut names = HashSet::new();
        let mut total_size = 0_u64;
        for index in 0..archive.len() {
            let file = archive.by_index(index).map_err(zip_error)?;
            if file.is_dir() || !names.insert(file.name().to_owned()) {
                return Err(invalid_data(
                    "source module archive contains a directory or duplicate entry",
                ));
            }
            total_size = total_size
                .checked_add(file.size())
                .ok_or_else(|| invalid_data("source module archive is too large"))?;
            if total_size > MAX_COMPONENT_SIZE {
                return Err(invalid_data(
                    "source module archive expands beyond the 512 MiB size limit",
                ));
            }
        }
        if !names.contains("find.rdmap") {
            return Err(invalid_data("source module archive is missing find.rdmap"));
        }

        let mut manifest_bytes = Vec::new();
        archive
            .by_name("find.rdmap")
            .map_err(zip_error)?
            .read_to_end(&mut manifest_bytes)?;
        if manifest_bytes.is_empty() || manifest_bytes.len() > MAX_ROADMAP_SIZE as usize {
            return Err(invalid_data(
                "ReTerm module manifest exceeds the 64 KiB size limit",
            ));
        }
        let roadmap: Roadmap = serde_json::from_slice(&manifest_bytes)
            .map_err(|error| invalid_data(format!("failed to parse module manifest: {error}")))?;
        if roadmap.header != "rdmapv3" {
            return Err(invalid_data(
                "ReTerm module roadmap header must be `rdmapv3`",
            ));
        }
        let exec_path = roadmap.validate()?;
        let extension = exec_path
            .extension()
            .and_then(OsStr::to_str)
            .unwrap_or_default()
            .to_ascii_lowercase();
        if !is_supported_mod_source_extension(&extension) {
            return Err(invalid_data(
                "ReTerm .mod files must contain supported source code",
            ));
        }
        let exec_name = exec_path
            .to_str()
            .ok_or_else(|| invalid_data("module source path must be valid UTF-8"))?;
        if names.len() != 2 || !names.contains(exec_name) || exec_name == "find.rdmap" {
            return Err(invalid_data(
                "source module archive entries do not match the manifest",
            ));
        }

        let mut source = Vec::new();
        archive
            .by_name(exec_name)
            .map_err(zip_error)?
            .read_to_end(&mut source)?;
        if source.is_empty() {
            return Err(invalid_data("module source file is empty"));
        }
        let source_text = std::str::from_utf8(&source)
            .map_err(|error| invalid_data(format!("module source is not valid UTF-8: {error}")))?;
        if extension == "rts" {
            crate::maker_core::compile_source_to_bytecode(source_text, "rts")?;
        }
        Ok((roadmap, source))
    }

    pub fn install(package_path: &Path) -> io::Result<Roadmap> {
        let is_mod = package_path
            .extension()
            .and_then(OsStr::to_str)
            .is_some_and(|ext| ext.eq_ignore_ascii_case("mod"));
        if is_mod {
            install_mod(package_path)
        } else {
            install_to(package_path, &component_root()?)
        }
    }

    pub fn install_mod(package_path: &Path) -> io::Result<Roadmap> {
        install_mod_to(package_path, &component_root()?)
    }

    /// Runs a `.mod` directly without installing it. This is used by both the
    /// `modrun` shell command and the graphical module launcher.
    pub fn run_mod_file<W: Write>(
        package_path: &Path,
        args: &[String],
        cwd: &Path,
        output: &mut W,
    ) -> io::Result<(Roadmap, i32)> {
        let contents = fs::read(package_path)?;
        let (roadmap, payload) = parse_mod_bytes(&contents)?;
        let mut full_args = roadmap.default_args.clone().unwrap_or_default();
        full_args.extend(args.iter().cloned());
        let extension = Path::new(&roadmap.exec)
            .extension()
            .and_then(OsStr::to_str)
            .unwrap_or_default()
            .to_ascii_lowercase();
        let exit_code = if extension == "rts" {
            if let Some(env_map) = &roadmap.env {
                for (key, value) in env_map {
                    std::env::set_var(key, value);
                }
            }
            execute_rts_source(&payload, &full_args, cwd, output)?
        } else {
            let temp_dir = RuntimeTempDir::create()?;
            let (mut command, stdin_source) = source_command(
                &payload,
                &extension,
                &full_args,
                cwd,
                roadmap.env.as_ref(),
                &temp_dir.0,
            )?;
            let result = source_command_output(&mut command, stdin_source.as_deref())?;
            output.write_all(&result.stdout)?;
            output.write_all(&result.stderr)?;
            result.status.code().unwrap_or(1)
        };
        Ok((roadmap, exit_code))
    }

    struct RuntimeTempDir(PathBuf);

    impl RuntimeTempDir {
        fn create() -> io::Result<Self> {
            let path = std::env::temp_dir().join(format!(
                "reterm-module-{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map_err(io::Error::other)?
                    .as_nanos()
            ));
            fs::create_dir(&path)?;
            Ok(Self(path))
        }
    }

    impl Drop for RuntimeTempDir {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn source_command(
        source: &[u8],
        extension: &str,
        args: &[String],
        cwd: &Path,
        env: Option<&std::collections::HashMap<String, String>>,
        build_dir: &Path,
    ) -> io::Result<(Command, Option<Vec<u8>>)> {
        let source_text = std::str::from_utf8(source)
            .map_err(|error| invalid_data(format!("module source is not valid UTF-8: {error}")))?;
        let (mut command, stdin_source) = match extension {
            "py" => {
                let mut command = Command::new(source_runtime(&["python", "python3"], "Python")?);
                command.arg("-c").arg(source_text);
                (command, None)
            }
            "rs" => {
                let executable = build_dir.join(if cfg!(windows) {
                    "reterm-module.exe"
                } else {
                    "reterm-module"
                });
                compile_rust_source(source, &executable, build_dir)?;
                (Command::new(executable), None)
            }
            "c" | "cpp" | "cc" | "cxx" => {
                let (candidates, language) = if extension == "c" {
                    (&["cc", "gcc", "clang"][..], "C")
                } else {
                    (&["c++", "g++", "clang++"][..], "C++")
                };
                let compiler = source_runtime(candidates, &format!("{language} compiler"))?;
                let executable = build_dir.join(if cfg!(windows) {
                    "reterm-module.exe"
                } else {
                    "reterm-module"
                });
                let mut build = Command::new(compiler);
                build
                    .arg("-x")
                    .arg(if extension == "c" { "c" } else { "c++" })
                    .arg("-")
                    .arg("-o")
                    .arg(&executable)
                    .current_dir(build_dir);
                run_source_compiler(&mut build, language, Some(source))?;
                (Command::new(executable), None)
            }
            "java" => {
                let (package, class_name) = java_entrypoint(source_text).ok_or_else(|| {
                    invalid_data("Java module has no class or record declaration")
                })?;
                let compiler = source_runtime(&["javac"], "Java compiler (JDK)")?;
                let helper_source = build_dir.join("ReTermJavaRunner.java");
                fs::write(&helper_source, JAVA_RUNNER_SOURCE)?;
                let mut build = Command::new(&compiler);
                build
                    .arg("-d")
                    .arg(build_dir)
                    .arg(&helper_source)
                    .current_dir(build_dir);
                run_source_compiler(&mut build, "Java runtime helper", None)?;
                let runtime = source_runtime(&["java"], "Java runtime")?;
                let main_class = if package.is_empty() {
                    class_name
                } else {
                    format!("{package}.{class_name}")
                };
                let mut command = Command::new(runtime);
                command
                    .arg("-cp")
                    .arg(build_dir)
                    .arg("ReTermJavaRunner")
                    .arg(main_class)
                    .arg(build_dir);
                (command, Some(source.to_vec()))
            }
            _ => return Err(invalid_data("unsupported source module language")),
        };
        command
            .args(args)
            .current_dir(cwd)
            .stdin(Stdio::inherit())
            .env("RETERM_EXECUTABLE", std::env::current_exe()?)
            .envs(env.into_iter().flat_map(|values| values.iter()));
        Ok((command, stdin_source))
    }

    fn compile_rust_source(source: &[u8], executable: &Path, build_dir: &Path) -> io::Result<()> {
        let source_path = build_dir.join("reterm-module.rs");
        fs::write(&source_path, source)?;

        #[cfg(has_go_rust_helper)]
        {
            let helper = build_dir.join(if cfg!(windows) {
                "reterm-rust-helper.exe"
            } else {
                "reterm-rust-helper"
            });
            fs::write(&helper, GO_RUST_HELPER_BYTES)?;
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                fs::set_permissions(&helper, fs::Permissions::from_mode(0o700))?;
            }
            let mut command = Command::new(helper);
            command
                .arg("--source")
                .arg(&source_path)
                .arg("--output")
                .arg(executable)
                .current_dir(build_dir);
            run_source_compiler(&mut command, "Rust", None)?;
        }

        #[cfg(not(has_go_rust_helper))]
        {
            let compiler = source_runtime(&["rustc"], "Rust compiler (rustc)")?;
            let mut command = Command::new(compiler);
            command
                .arg("--crate-name")
                .arg("reterm_module")
                .arg("--edition=2021")
                .arg("-o")
                .arg(executable)
                .arg(&source_path)
                .current_dir(build_dir);
            run_source_compiler(&mut command, "Rust", None)?;
        }

        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(executable, fs::Permissions::from_mode(0o700))?;
        }
        Ok(())
    }

    fn source_runtime(candidates: &[&str], description: &str) -> io::Result<String> {
        for candidate in candidates {
            if Command::new(candidate).arg("--version").output().is_ok() {
                return Ok((*candidate).to_owned());
            }
        }
        Err(io::Error::new(
            io::ErrorKind::NotFound,
            format!(
                "{description} is required to run this .mod source module; install it or add it to PATH"
            ),
        ))
    }

    fn source_command_output(command: &mut Command, stdin: Option<&[u8]>) -> io::Result<Output> {
        if let Some(stdin) = stdin {
            command
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .stderr(Stdio::piped());
            let mut child = command.spawn()?;
            let mut pipe = child
                .stdin
                .take()
                .ok_or_else(|| io::Error::other("failed to open module source input pipe"))?;
            let write_result = pipe.write_all(stdin);
            drop(pipe);
            let result = child.wait_with_output()?;
            write_result?;
            Ok(result)
        } else {
            command.output()
        }
    }

    fn run_source_compiler(
        command: &mut Command,
        language: &str,
        source: Option<&[u8]>,
    ) -> io::Result<()> {
        let result = source_command_output(command, source).map_err(|error| {
            io::Error::new(
                error.kind(),
                format!("could not start the {language} compiler: {error}"),
            )
        })?;
        if result.status.success() {
            return Ok(());
        }
        let mut diagnostic = String::from_utf8_lossy(&result.stderr).into_owned();
        if diagnostic.trim().is_empty() {
            diagnostic = String::from_utf8_lossy(&result.stdout).into_owned();
        }
        Err(invalid_data(format!(
            "{language} module compilation failed: {}",
            diagnostic.trim()
        )))
    }

    const JAVA_RUNNER_SOURCE: &str = r#"import java.io.File;
import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import javax.tools.JavaCompiler;
import javax.tools.SimpleJavaFileObject;
import javax.tools.ToolProvider;

public final class ReTermJavaRunner {
    private static final class ModuleSource extends SimpleJavaFileObject {
        private final String source;
        ModuleSource(String className, String source) {
            super(URI.create("string:///" + className.replace('.', '/') + Kind.SOURCE.extension), Kind.SOURCE);
            this.source = source;
        }
        public CharSequence getCharContent(boolean ignoreEncodingErrors) {
            return source;
        }
    }
    public static void main(String[] args) throws Exception {
        String source = new String(System.in.readAllBytes(), StandardCharsets.UTF_8);
        String className = args[0];
        File output = new File(args[1]);
        JavaCompiler compiler = ToolProvider.getSystemJavaCompiler();
        if (compiler == null) throw new IllegalStateException("A JDK with the Java compiler is required");
        boolean compiled = compiler.getTask(null, null, null,
            Arrays.asList("-d", output.getAbsolutePath()), null,
            Arrays.asList(new ModuleSource(className, source))).call();
        if (!compiled) System.exit(1);
        try (URLClassLoader loader = new URLClassLoader(
                new URL[] { output.toURI().toURL() }, ClassLoader.getSystemClassLoader())) {
            Class<?> mainClass = Class.forName(className, true, loader);
            Method main = mainClass.getMethod("main", String[].class);
            main.invoke(null, (Object) Arrays.copyOfRange(args, 2, args.length));
        }
    }
}"#;

    fn java_entrypoint(source: &str) -> Option<(String, String)> {
        let words = source
            .split(|ch: char| !(ch.is_ascii_alphanumeric() || ch == '_' || ch == '$'))
            .filter(|word| !word.is_empty())
            .collect::<Vec<_>>();
        let class_name = words
            .windows(2)
            .find(|pair| matches!(pair[0], "class" | "record" | "enum"))
            .map(|pair| pair[1].to_owned())?;
        let package = source
            .split_once("package ")
            .and_then(|(_, rest)| rest.split(';').next())
            .map(str::trim)
            .unwrap_or_default()
            .to_owned();
        Some((package, class_name))
    }

    fn install_mod_to(package_path: &Path, component_root: &Path) -> io::Result<Roadmap> {
        if !package_path.is_file() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "modadd requires an existing ReTerm .mod module file",
            ));
        }

        let contents = fs::read(package_path)?;
        let (roadmap, payload_bytes) = parse_mod_bytes(&contents)?;
        let exec_path = roadmap.validate()?;
        let source_extension = exec_path
            .extension()
            .and_then(OsStr::to_str)
            .unwrap_or_default()
            .to_ascii_lowercase();
        let installed_exec = if source_extension == "rs" {
            PathBuf::from(format!("{}.exe", roadmap.callby))
        } else {
            exec_path
        };

        let destination = component_root.join(&roadmap.callby);
        if destination.exists() {
            return Err(io::Error::new(
                io::ErrorKind::AlreadyExists,
                format!("component/module `{}` is already installed", roadmap.callby),
            ));
        }

        fs::create_dir_all(component_root)?;
        let staging = component_root.join(format!(
            ".install-mod-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_err(io::Error::other)?
                .as_nanos()
        ));
        fs::create_dir(&staging)?;
        let install_result = (|| {
            let target_exec = staging.join(&installed_exec);
            if let Some(parent) = target_exec.parent() {
                fs::create_dir_all(parent)?;
            }
            if source_extension == "rs" {
                compile_rust_source(&payload_bytes, &target_exec, &staging)?;
            } else {
                fs::write(
                    &target_exec,
                    &mod_crypto::encrypt_mod_payload(&payload_bytes)?,
                )?;
            }
            let mut installed_roadmap = roadmap.clone();
            if source_extension == "rs" {
                installed_roadmap.exec = installed_exec
                    .to_str()
                    .ok_or_else(|| invalid_data("installed executable path is not valid UTF-8"))?
                    .to_owned();
                installed_roadmap.encrypted_exec = None;
            } else {
                installed_roadmap.encrypted_exec = Some(true);
            }
            fs::write(
                staging.join("find.rdmap"),
                serde_json::to_vec_pretty(&installed_roadmap).map_err(invalid_data)?,
            )?;
            fs::rename(&staging, &destination)
        })();
        if let Err(error) = install_result {
            let _ = fs::remove_dir_all(&staging);
            return Err(error);
        }

        let mut installed_roadmap = roadmap;
        if source_extension == "rs" {
            installed_roadmap.exec = installed_exec
                .to_str()
                .ok_or_else(|| invalid_data("installed executable path is not valid UTF-8"))?
                .to_owned();
            installed_roadmap.encrypted_exec = None;
        }
        Ok(installed_roadmap)
    }

    pub fn install_all(
        package_directory: &Path,
    ) -> io::Result<Vec<(PathBuf, Result<Roadmap, String>)>> {
        install_all_to(package_directory, &component_root()?)
    }

    fn install_all_to(
        package_directory: &Path,
        component_root: &Path,
    ) -> io::Result<Vec<(PathBuf, Result<Roadmap, String>)>> {
        if !package_directory.is_dir() {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!(
                    "component folder does not exist: {}",
                    package_directory.display()
                ),
            ));
        }

        let mut packages = fs::read_dir(package_directory)?
            .map(|entry| {
                let entry = entry?;
                let file_type = entry.file_type()?;
                let extension = entry
                    .path()
                    .extension()
                    .and_then(OsStr::to_str)
                    .unwrap_or_default()
                    .to_ascii_lowercase();
                let is_comp = file_type.is_file() && extension == "comp";
                let is_mod = file_type.is_file() && extension == "mod";
                Ok((entry.path(), is_comp, is_mod))
            })
            .collect::<io::Result<Vec<_>>>()?
            .into_iter()
            .filter_map(|(path, is_comp, is_mod)| (is_comp || is_mod).then_some((path, is_mod)))
            .collect::<Vec<_>>();
        packages.sort_by_key(|(path, _)| {
            path.file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .to_ascii_lowercase()
        });

        Ok(packages
            .into_iter()
            .map(|(package, is_mod)| {
                let result = if is_mod {
                    install_mod_to(&package, component_root)
                } else {
                    install_to(&package, component_root)
                }
                .map_err(|error| error.to_string());
                (package, result)
            })
            .collect())
    }

    pub fn list_installed() -> io::Result<Vec<Roadmap>> {
        let Some(root) = component_root_if_present()? else {
            return Ok(Vec::new());
        };
        list_installed_in(&root)
    }

    pub fn list_installed_mods() -> io::Result<Vec<Roadmap>> {
        Ok(list_installed()?
            .into_iter()
            .filter(|roadmap| roadmap.header == "rdmapv3")
            .collect())
    }

    fn list_installed_in(root: &Path) -> io::Result<Vec<Roadmap>> {
        let mut installed = Vec::new();
        for entry in fs::read_dir(root)? {
            let entry = entry?;
            if !entry.file_type()?.is_dir() {
                continue;
            }
            let manifest = entry.path().join("find.rdmap");
            if manifest.is_file() {
                let roadmap: Roadmap = serde_json::from_slice(&fs::read(manifest)?)
                    .map_err(|error| invalid_data(error))?;
                roadmap.validate()?;
                installed.push(roadmap);
            }
        }
        installed.sort_by_key(|roadmap| roadmap.callby.to_ascii_lowercase());
        Ok(installed)
    }

    pub fn remove_installed(callby: &str) -> io::Result<Roadmap> {
        if callby.is_empty()
            || !callby
                .chars()
                .all(|ch| ch.is_ascii_alphanumeric() || matches!(ch, '_' | '-' | '.'))
        {
            return Err(invalid_data("invalid component command name"));
        }
        let Some(root) = component_root_if_present()? else {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!("component `{callby}` is not installed"),
            ));
        };
        remove_installed_matching(&root, callby, false)
    }

    #[cfg(test)]
    fn remove_installed_in(root: &Path, callby: &str) -> io::Result<Roadmap> {
        remove_installed_matching(root, callby, false)
    }

    pub fn remove_installed_mod(callby: &str) -> io::Result<Roadmap> {
        if callby.is_empty()
            || !callby
                .chars()
                .all(|ch| ch.is_ascii_alphanumeric() || matches!(ch, '_' | '-' | '.'))
        {
            return Err(invalid_data("invalid module command name"));
        }
        let Some(root) = component_root_if_present()? else {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!("module `{callby}` is not installed"),
            ));
        };
        remove_installed_matching(&root, callby, true)
    }

    fn remove_installed_matching(
        root: &Path,
        callby: &str,
        modules_only: bool,
    ) -> io::Result<Roadmap> {
        for entry in fs::read_dir(root)? {
            let entry = entry?;
            if !entry.file_type()?.is_dir() {
                continue;
            }
            let directory = entry.path();
            let manifest = directory.join("find.rdmap");
            if !manifest.is_file() {
                continue;
            }
            let roadmap: Roadmap = serde_json::from_slice(&fs::read(manifest)?)
                .map_err(|error| invalid_data(error))?;
            if roadmap.callby.eq_ignore_ascii_case(callby) {
                if modules_only && roadmap.header != "rdmapv3" {
                    continue;
                }
                roadmap.validate()?;
                fs::remove_dir_all(directory)?;
                return Ok(roadmap);
            }
        }
        Err(io::Error::new(
            io::ErrorKind::NotFound,
            format!(
                "{} `{callby}` is not installed",
                if modules_only { "module" } else { "component" }
            ),
        ))
    }

    fn install_to(package_path: &Path, component_root: &Path) -> io::Result<Roadmap> {
        if !package_path.is_file()
            || !package_path
                .extension()
                .and_then(OsStr::to_str)
                .is_some_and(|extension| extension.eq_ignore_ascii_case("comp"))
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "compadd requires an existing .comp file",
            ));
        }

        fs::create_dir_all(component_root)?;
        let mut archive = ZipArchive::new(File::open(package_path)?).map_err(zip_error)?;
        let mut manifest = None;
        let mut total_size = 0_u64;
        let mut names = HashSet::new();

        for index in 0..archive.len() {
            let file = archive
                .by_index_decrypt(index, COMPONENT_PASSWORD)
                .map_err(zip_error)?
                .map_err(|_| invalid_data("component archive password is incorrect"))?;
            let name = file.name();
            let is_manifest = name == "find.rdmap";
            let file_size = file.size();
            if name.contains('\\') {
                return Err(invalid_data("component archive contains an unsafe path"));
            }
            let relative_path = file
                .enclosed_name()
                .filter(|path| {
                    !path.as_os_str().is_empty()
                        && path
                            .components()
                            .all(|component| matches!(component, Component::Normal(_)))
                })
                .ok_or_else(|| invalid_data("component archive contains an unsafe path"))?
                .to_path_buf();
            if !names.insert(relative_path.clone()) {
                return Err(invalid_data("component archive contains duplicate paths"));
            }
            if let Some(mode) = file.unix_mode() {
                if mode & 0o170000 == 0o120000 {
                    return Err(invalid_data("component archive may not contain symlinks"));
                }
            }
            total_size = total_size
                .checked_add(file_size)
                .ok_or_else(|| invalid_data("component archive is too large"))?;
            if total_size > MAX_COMPONENT_SIZE {
                return Err(invalid_data(
                    "component archive expands beyond the 512 MiB size limit",
                ));
            }
            drop(file);
            if is_manifest {
                if file_size > MAX_ROADMAP_SIZE {
                    return Err(invalid_data("find.rdmap exceeds the 64 KiB size limit"));
                }
                let mut contents = Vec::with_capacity(file_size as usize);
                archive
                    .by_index_decrypt(index, COMPONENT_PASSWORD)
                    .map_err(zip_error)?
                    .map_err(|_| invalid_data("component archive password is incorrect"))?
                    .read_to_end(&mut contents)?;
                let roadmap: Roadmap =
                    serde_json::from_slice(&contents).map_err(|error| invalid_data(error))?;
                manifest = Some(roadmap);
            }
        }

        let roadmap = manifest
            .ok_or_else(|| invalid_data("component archive must contain find.rdmap at its root"))?;
        let exec_path = roadmap.validate()?;
        if !names.contains(&exec_path) {
            return Err(invalid_data(
                "component archive does not contain the executable declared by find.rdmap",
            ));
        }
        let destination = component_root.join(&roadmap.callby);
        if destination.exists() {
            return Err(io::Error::new(
                io::ErrorKind::AlreadyExists,
                format!(
                    "component command `{}` is already installed",
                    roadmap.callby
                ),
            ));
        }

        let staging = component_root.join(format!(
            ".install-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_err(io::Error::other)?
                .as_nanos()
        ));
        fs::create_dir(&staging)?;
        let extraction_result = extract_archive(&mut archive, &staging);
        if let Err(error) = extraction_result {
            let _ = fs::remove_dir_all(&staging);
            return Err(error);
        }
        if !staging.join(&exec_path).is_file() {
            let _ = fs::remove_dir_all(&staging);
            return Err(invalid_data(
                "component executable declared by find.rdmap is missing",
            ));
        }
        if let Err(error) = fs::rename(&staging, &destination) {
            let _ = fs::remove_dir_all(&staging);
            return Err(error);
        }
        Ok(roadmap)
    }

    fn extract_archive(archive: &mut ZipArchive<File>, staging: &Path) -> io::Result<()> {
        for index in 0..archive.len() {
            let mut file = archive
                .by_index_decrypt(index, COMPONENT_PASSWORD)
                .map_err(zip_error)?
                .map_err(|_| invalid_data("component archive password is incorrect"))?;
            let relative_path = file
                .enclosed_name()
                .filter(|path| {
                    !path.as_os_str().is_empty()
                        && path
                            .components()
                            .all(|component| matches!(component, Component::Normal(_)))
                })
                .ok_or_else(|| invalid_data("component archive contains an unsafe path"))?
                .to_path_buf();
            let output_path = staging.join(relative_path);
            if file.is_dir() {
                fs::create_dir_all(output_path)?;
            } else {
                if let Some(parent) = output_path.parent() {
                    fs::create_dir_all(parent)?;
                }
                let mut output = File::create(output_path)?;
                io::copy(&mut file, &mut output)?;
            }
        }
        Ok(())
    }

    pub fn run_command(
        command: &str,
        args: &[String],
        cwd: &Path,
    ) -> io::Result<Option<ExitStatus>> {
        let Some(root) = component_root_if_present()? else {
            return Ok(None);
        };
        run_command_in(&root, command, args, cwd)
    }

    fn run_command_in(
        root: &Path,
        command: &str,
        args: &[String],
        cwd: &Path,
    ) -> io::Result<Option<ExitStatus>> {
        let entries = fs::read_dir(root)?.collect::<Result<Vec<_>, _>>()?;
        for entry in entries {
            if !entry.file_type()?.is_dir() {
                continue;
            }
            let directory = entry.path();
            let roadmap_path = directory.join("find.rdmap");
            if !roadmap_path.is_file() {
                continue;
            }
            let roadmap: Roadmap = serde_json::from_slice(&fs::read(&roadmap_path)?)
                .map_err(|error| invalid_data(error))?;
            let exec_path = roadmap.validate()?;
            if !roadmap.callby.eq_ignore_ascii_case(command) {
                continue;
            }
            let executable = directory.join(exec_path);
            if !executable.is_file() {
                return Err(io::Error::new(
                    io::ErrorKind::NotFound,
                    format!(
                        "component `{}` is missing its declared executable",
                        roadmap.callby
                    ),
                ));
            }
            return run_executable(&executable, args, cwd, &roadmap).map(Some);
        }
        Ok(None)
    }

    pub fn execute_bytecode<W: Write>(
        bytecode: &[u8],
        args: &[String],
        cwd: &Path,
        output: &mut W,
    ) -> io::Result<i32> {
        let instructions = decode_bytecode(bytecode)?;
        let mut instruction_cursor = 0;
        let mut exit_code = 0;
        let mut vars: std::collections::HashMap<String, String> = std::collections::HashMap::new();
        let mut cmp_flag: bool = false;

        let mut labels: std::collections::HashMap<String, usize> = std::collections::HashMap::new();
        for (index, (opcode, payload_start, payload_end)) in instructions.iter().enumerate() {
            if *opcode == 0x0A {
                let name = std::str::from_utf8(&bytecode[*payload_start..*payload_end])
                    .map_err(|error| invalid_data(format!("invalid bytecode label: {error}")))?
                    .to_owned();
                if name.is_empty() {
                    return Err(invalid_data("bytecode contains an empty label"));
                }
                if labels.insert(name, index + 1).is_some() {
                    return Err(invalid_data("bytecode contains a duplicate label"));
                }
            }
        }
        for (opcode, payload_start, payload_end) in &instructions {
            if *opcode == 0x0B || *opcode == 0x0C {
                let target = std::str::from_utf8(&bytecode[*payload_start..*payload_end]).map_err(
                    |error| invalid_data(format!("invalid bytecode jump target: {error}")),
                )?;
                if !labels.contains_key(target.trim()) {
                    return Err(invalid_data(format!(
                        "bytecode jump references missing label `{}`",
                        target.trim()
                    )));
                }
            }
        }
        while instruction_cursor < instructions.len() {
            let (opcode, payload_start, payload_end) = instructions[instruction_cursor];
            instruction_cursor += 1;
            let raw_str = std::str::from_utf8(&bytecode[payload_start..payload_end])
                .map_err(|error| invalid_data(format!("bytecode operand is not UTF-8: {error}")))?;
            let operand_str = interpolate_vars(raw_str, &vars, args);

            match opcode {
                0x01 => {
                    // PRINT
                    writeln!(output, "{operand_str}")?;
                }
                0x10 => {
                    write!(output, "{operand_str}")?;
                }
                0x02 => {
                    // SETENV
                    if let Some((key, val)) = operand_str.split_once('=') {
                        std::env::set_var(key.trim(), val.trim());
                    }
                }
                0x03 => {
                    // EXEC
                    let parts = crate::shell::parse_command(&operand_str)
                        .map_err(|error| invalid_data(format!("invalid EXEC command: {error}")))?;
                    if !parts.is_empty() {
                        let mut cmd = Command::new(&parts[0]);
                        cmd.args(&parts[1..]).current_dir(cwd).status()?;
                    }
                }
                0x04 => {
                    // WRITEFILE
                    if let Some((rel_path, content)) = operand_str.split_once("::") {
                        let path = crate::shell::resolve_path(cwd, rel_path.trim());
                        if let Some(parent) = path.parent() {
                            fs::create_dir_all(parent)?;
                        }
                        fs::write(path, content)?;
                    }
                }
                0x05 => {
                    // READFILE
                    let path = crate::shell::resolve_path(cwd, operand_str.trim());
                    if path.is_file() {
                        let contents = fs::read_to_string(path)?;
                        writeln!(output, "{contents}")?;
                    }
                }
                0x06 => {
                    // SLEEP
                    if let Ok(ms) = operand_str.trim().parse::<u64>() {
                        std::thread::sleep(std::time::Duration::from_millis(ms));
                    }
                }
                0x07 => {
                    // ECHO_ARGS
                    writeln!(output, "{}", args.join(" "))?;
                }
                0x08 => {
                    // EXIT
                    if let Ok(code) = operand_str.trim().parse::<i32>() {
                        exit_code = code;
                        break;
                    }
                }
                0x09 => {
                    // SETVAR — set an internal VM variable
                    if let Some((key, val)) = operand_str.split_once('=') {
                        let resolved_val = interpolate_vars(val.trim(), &vars, args);
                        vars.insert(key.trim().to_string(), resolved_val);
                    }
                }
                0x0A => {
                    // LABEL — no-op at runtime (used for jump targets)
                }
                0x0B => {
                    // JUMP — unconditional jump to label
                    if let Some(&target) = labels.get(operand_str.trim()) {
                        instruction_cursor = target;
                    }
                }
                0x0C => {
                    // JUMPZ — jump if cmp_flag is false
                    if !cmp_flag {
                        if let Some(&target) = labels.get(operand_str.trim()) {
                            instruction_cursor = target;
                        }
                    }
                }
                0x0D => {
                    // CMP — compare expression, sets cmp_flag
                    cmp_flag = evaluate_cmp(&operand_str);
                }
                0x0E => {
                    // INCR — increment a numeric variable
                    let var = operand_str.trim().to_string();
                    let val = vars
                        .get(&var)
                        .and_then(|v| v.parse::<i64>().ok())
                        .unwrap_or(0);
                    vars.insert(var, (val + 1).to_string());
                }
                0x0F => {
                    // NOP
                }
                0x11 => {
                    // NUKE — overwrite a regular file with zero bytes.
                    let path = if !operand_str.trim().is_empty() {
                        crate::shell::resolve_path(cwd, operand_str.trim())
                    } else if args.len() == 1 {
                        crate::shell::resolve_path(cwd, &args[0])
                    } else {
                        return Err(io::Error::new(
                            io::ErrorKind::InvalidInput,
                            "usage: nuke <file>",
                        ));
                    };
                    if !operand_str.trim().is_empty() && !args.is_empty() {
                        return Err(io::Error::new(
                            io::ErrorKind::InvalidInput,
                            "nuke module accepts a file path either as an argument or in its script, not both",
                        ));
                    }
                    let bytes_written = zero_regular_file(&path)?;
                    writeln!(
                        output,
                        "Overwrote {bytes_written} bytes with zeros in {}.",
                        path.display()
                    )?;
                }
                0x12 => {
                    // COMPALL — install packages using ReTerm's portable component loader.
                    if !operand_str.trim().is_empty() && !args.is_empty() {
                        return Err(io::Error::new(
                            io::ErrorKind::InvalidInput,
                            "compall module accepts a folder either as an argument or in its script, not both",
                        ));
                    }
                    let folder = if !operand_str.trim().is_empty() {
                        crate::shell::resolve_path(cwd, operand_str.trim())
                    } else if let Some(folder) = args.first() {
                        crate::shell::resolve_path(cwd, folder)
                    } else {
                        cwd.to_path_buf()
                    };
                    let results = install_all(&folder)?;
                    let mut installed = 0;
                    let mut failed = 0;
                    for (package, result) in results {
                        match result {
                            Ok(component) => {
                                installed += 1;
                                writeln!(
                                    output,
                                    "Installed component {} ({}) as `{}` from {}.",
                                    component.name,
                                    component.version,
                                    component.callby,
                                    package.display()
                                )?;
                            }
                            Err(error) => {
                                failed += 1;
                                writeln!(
                                    output,
                                    "Failed to install {}: {error}",
                                    package.display()
                                )?;
                            }
                        }
                    }
                    writeln!(
                        output,
                        "Component batch complete: {installed} installed, {failed} failed."
                    )?;
                    if failed > 0 {
                        return Err(io::Error::other(format!(
                            "{failed} component package(s) could not be installed"
                        )));
                    }
                }
                0x13 => {
                    // SHELLPASS — execute a command using the host's command interpreter.
                    if !operand_str.trim().is_empty() && !args.is_empty() {
                        return Err(io::Error::new(
                            io::ErrorKind::InvalidInput,
                            "shellpass module accepts a command either as arguments or in its script, not both",
                        ));
                    }
                    let command_text = if operand_str.trim().is_empty() {
                        args.join(" ")
                    } else {
                        operand_str
                    };
                    if command_text.trim().is_empty() {
                        return Err(io::Error::new(
                            io::ErrorKind::InvalidInput,
                            "usage: shellpass <command>",
                        ));
                    }
                    #[cfg(windows)]
                    let status = Command::new("cmd.exe")
                        .args(["/D", "/C"])
                        .arg(&command_text)
                        .current_dir(cwd)
                        .status()?;
                    #[cfg(not(windows))]
                    let status = Command::new("/bin/sh")
                        .arg("-c")
                        .arg(&command_text)
                        .current_dir(cwd)
                        .status()?;
                    exit_code = status.code().unwrap_or(1);
                    break;
                }
                _ => return Err(invalid_data("bytecode contains an unsupported opcode")),
            }
        }
        Ok(exit_code)
    }

    fn zero_regular_file(path: &Path) -> io::Result<u64> {
        let link_metadata = fs::symlink_metadata(path)?;
        validate_nuke_target(&link_metadata)?;

        let mut options = OpenOptions::new();
        options.write(true);
        #[cfg(windows)]
        {
            use std::os::windows::fs::OpenOptionsExt;
            options.custom_flags(0x0020_0000);
        }
        #[cfg(target_os = "linux")]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.custom_flags(0x0002_0000);
        }

        let mut file = options.open(path)?;
        let metadata = file.metadata()?;
        validate_nuke_target(&metadata)?;
        let length = metadata.len();
        let zeros = [0_u8; 64 * 1024];
        let mut remaining = length;
        while remaining > 0 {
            let count = remaining.min(zeros.len() as u64) as usize;
            file.write_all(&zeros[..count])?;
            remaining -= count as u64;
        }
        file.sync_all()?;
        Ok(length)
    }

    fn validate_nuke_target(metadata: &fs::Metadata) -> io::Result<()> {
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

    fn decode_bytecode(bytecode: &[u8]) -> io::Result<Vec<(u8, usize, usize)>> {
        let mut instructions = Vec::new();
        let mut cursor = 0;
        while cursor < bytecode.len() {
            if bytecode.len() - cursor < 5 {
                return Err(invalid_data("bytecode ends inside an instruction header"));
            }
            let opcode = bytecode[cursor];
            if !(0x01..=0x13).contains(&opcode) {
                return Err(invalid_data(format!(
                    "bytecode has unsupported opcode 0x{opcode:02X}"
                )));
            }
            let payload_len = u32::from_le_bytes(
                bytecode[cursor + 1..cursor + 5]
                    .try_into()
                    .map_err(invalid_data)?,
            ) as usize;
            let payload_start = cursor + 5;
            let payload_end = payload_start
                .checked_add(payload_len)
                .filter(|end| *end <= bytecode.len())
                .ok_or_else(|| invalid_data("bytecode ends inside an instruction operand"))?;
            instructions.push((opcode, payload_start, payload_end));
            cursor = payload_end;
        }
        if instructions.is_empty() {
            return Err(invalid_data("bytecode contains no instructions"));
        }
        let mut labels = std::collections::HashSet::new();
        for (opcode, payload_start, payload_end) in &instructions {
            let operand = std::str::from_utf8(&bytecode[*payload_start..*payload_end])
                .map_err(|error| invalid_data(format!("bytecode operand is not UTF-8: {error}")))?;
            if *opcode == 0x0A {
                if operand.is_empty() {
                    return Err(invalid_data("bytecode contains an empty label"));
                }
                if !labels.insert(operand) {
                    return Err(invalid_data("bytecode contains a duplicate label"));
                }
            }
        }
        for (opcode, payload_start, payload_end) in &instructions {
            if *opcode == 0x0B || *opcode == 0x0C {
                let target = std::str::from_utf8(&bytecode[*payload_start..*payload_end]).map_err(
                    |error| invalid_data(format!("invalid bytecode jump target: {error}")),
                )?;
                if !labels.contains(target.trim()) {
                    return Err(invalid_data(format!(
                        "bytecode jump references missing label `{}`",
                        target.trim()
                    )));
                }
            }
        }
        Ok(instructions)
    }

    /// Substitute `${varname}` references in a string with their values.
    fn interpolate_vars(
        s: &str,
        vars: &std::collections::HashMap<String, String>,
        args: &[String],
    ) -> String {
        if !s.contains("${") {
            return s.to_string();
        }
        let chars: Vec<char> = s.chars().collect();
        let mut result = String::with_capacity(s.len());
        let mut i = 0;
        while i < chars.len() {
            if i + 1 < chars.len() && chars[i] == '$' && chars[i + 1] == '{' {
                i += 2;
                let mut var_name = String::new();
                while i < chars.len() && chars[i] != '}' {
                    var_name.push(chars[i]);
                    i += 1;
                }
                if i < chars.len() {
                    i += 1;
                } // skip '}'
                if var_name == "args" {
                    result.push_str(&args.join(" "));
                } else if let Some(val) = vars.get(&var_name) {
                    result.push_str(val);
                } else {
                    // Leave unresolved references as empty
                }
            } else {
                result.push(chars[i]);
                i += 1;
            }
        }
        result
    }

    /// Evaluate a comparison expression like "3 < 10" or "hello == hello".
    fn evaluate_cmp(expr: &str) -> bool {
        let ops = ["==", "!=", "<=", ">=", "<", ">"];
        for op in ops {
            if let Some(pos) = expr.find(op) {
                let left = expr[..pos].trim();
                let right = expr[pos + op.len()..].trim();

                // Try numeric comparison first
                if let (Ok(l), Ok(r)) = (left.parse::<f64>(), right.parse::<f64>()) {
                    return match op {
                        "==" => (l - r).abs() < f64::EPSILON,
                        "!=" => (l - r).abs() >= f64::EPSILON,
                        "<" => l < r,
                        ">" => l > r,
                        "<=" => l <= r,
                        ">=" => l >= r,
                        _ => false,
                    };
                }

                // Fall back to string comparison
                return match op {
                    "==" => left == right,
                    "!=" => left != right,
                    "<" => left < right,
                    ">" => left > right,
                    "<=" => left <= right,
                    ">=" => left >= right,
                    _ => false,
                };
            }
        }
        // No operator found: non-empty / non-"0" / non-"false" → true
        !expr.is_empty() && expr != "0" && expr != "false"
    }

    fn run_executable(
        executable: &Path,
        args: &[String],
        cwd: &Path,
        roadmap: &Roadmap,
    ) -> io::Result<ExitStatus> {
        let extension = executable
            .extension()
            .and_then(OsStr::to_str)
            .unwrap_or_default()
            .to_ascii_lowercase();

        if extension == "rbc" {
            let stored_bytecode = fs::read(executable)?;
            let bytecode = if roadmap.encrypted_exec == Some(true) {
                mod_crypto::decrypt_mod_payload(&stored_bytecode)?
            } else {
                stored_bytecode
            };
            let mut full_args = Vec::new();
            if let Some(defaults) = &roadmap.default_args {
                full_args.extend(defaults.clone());
            }
            full_args.extend(args.iter().cloned());
            if let Some(env_map) = &roadmap.env {
                for (k, v) in env_map {
                    std::env::set_var(k, v);
                }
            }
            let stdout = io::stdout();
            let mut lock = stdout.lock();
            let code = execute_bytecode(&bytecode, &full_args, cwd, &mut lock)?;
            return Ok(exit_status_from_code(code));
        }

        if extension == "rts" {
            let stored_source = fs::read(executable)?;
            let source = if roadmap.encrypted_exec == Some(true) {
                mod_crypto::decrypt_mod_payload(&stored_source)?
            } else {
                stored_source
            };
            let mut full_args = roadmap.default_args.clone().unwrap_or_default();
            full_args.extend(args.iter().cloned());
            if let Some(env_map) = &roadmap.env {
                for (key, value) in env_map {
                    std::env::set_var(key, value);
                }
            }
            let stdout = io::stdout();
            let mut lock = stdout.lock();
            let code = execute_rts_source(&source, &full_args, cwd, &mut lock)?;
            return Ok(exit_status_from_code(code));
        }

        if is_supported_source_extension(&extension) {
            let temp_dir = RuntimeTempDir::create()?;
            let stored_source = fs::read(executable)?;
            let source = if roadmap.encrypted_exec == Some(true) {
                mod_crypto::decrypt_mod_payload(&stored_source)?
            } else {
                stored_source
            };
            let (mut command, stdin_source) =
                source_command(&source, &extension, &[], cwd, None, &temp_dir.0)?;
            if let Some(env_map) = &roadmap.env {
                command.envs(env_map);
            }
            if let Some(defaults) = &roadmap.default_args {
                command.args(defaults);
            }
            command
                .args(args)
                .env("RETERM_EXECUTABLE", std::env::current_exe()?);
            let result = source_command_output(&mut command, stdin_source.as_deref())?;
            let stdout = io::stdout();
            let mut stdout = stdout.lock();
            stdout.write_all(&result.stdout)?;
            stdout.write_all(&result.stderr)?;
            return Ok(result.status);
        }

        let mut command = match extension.as_str() {
            "exe" | "mod" => Command::new(executable),
            #[cfg(not(windows))]
            "sh" => {
                let mut command = Command::new("/bin/sh");
                command.arg(executable);
                command
            }
            #[cfg(windows)]
            "bat" | "cmd" => {
                let mut command = Command::new("cmd.exe");
                command.arg("/C").arg(executable);
                command
            }
            #[cfg(windows)]
            "vbs" => {
                let mut command = Command::new("cscript.exe");
                command.arg("//NoLogo").arg(executable);
                command
            }
            #[cfg(not(windows))]
            "bat" | "cmd" | "vbs" => {
                return Err(io::Error::new(
                    io::ErrorKind::Unsupported,
                    "Windows script components can only run on Windows",
                ));
            }
            _ => return Err(invalid_data("unsupported component executable type")),
        };
        if let Some(env_map) = &roadmap.env {
            command.envs(env_map);
        }
        if let Some(defaults) = &roadmap.default_args {
            command.args(defaults);
        }
        command
            .args(args)
            .current_dir(cwd)
            .env("RETERM_EXECUTABLE", std::env::current_exe()?)
            .status()
    }

    pub(crate) fn home_directory() -> io::Result<PathBuf> {
        #[cfg(windows)]
        let home = std::env::var_os("USERPROFILE")
            .or_else(|| std::env::var_os("HOME"))
            .or_else(|| {
                let drive = std::env::var_os("HOMEDRIVE")?;
                let home_path = std::env::var_os("HOMEPATH")?;
                Some(PathBuf::from(drive).join(home_path).into_os_string())
            });
        #[cfg(not(windows))]
        let home = std::env::var_os("HOME").or_else(|| std::env::var_os("USERPROFILE"));

        home.map(PathBuf::from).ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::NotFound,
                "could not locate user home directory",
            )
        })
    }

    fn component_root() -> io::Result<PathBuf> {
        #[cfg(not(windows))]
        if let Some(data_home) = std::env::var_os("XDG_DATA_HOME") {
            return Ok(PathBuf::from(data_home).join("reterm").join("components"));
        }
        if let Some(local_app_data) = std::env::var_os("LOCALAPPDATA") {
            return Ok(PathBuf::from(local_app_data)
                .join("ReTerm")
                .join("components"));
        }
        Ok(home_directory()?.join(".reterm").join("components"))
    }

    fn component_root_if_present() -> io::Result<Option<PathBuf>> {
        let root = component_root()?;
        if root.is_dir() {
            Ok(Some(root))
        } else {
            Ok(None)
        }
    }

    fn zip_error(error: zip::result::ZipError) -> io::Error {
        io::Error::new(io::ErrorKind::InvalidData, error)
    }

    fn invalid_data(error: impl Into<Box<dyn std::error::Error + Send + Sync>>) -> io::Error {
        io::Error::new(io::ErrorKind::InvalidData, error)
    }

    fn is_supported_component_extension(extension: &str) -> bool {
        matches!(
            extension,
            "exe"
                | "mod"
                | "rbc"
                | "rts"
                | "bat"
                | "cmd"
                | "vbs"
                | "py"
                | "rs"
                | "c"
                | "cpp"
                | "cc"
                | "cxx"
                | "java"
        ) || cfg!(not(windows)) && extension == "sh"
    }

    fn is_supported_source_extension(extension: &str) -> bool {
        matches!(extension, "py" | "rs" | "c" | "cpp" | "cc" | "cxx" | "java")
    }

    fn is_supported_mod_source_extension(extension: &str) -> bool {
        extension == "rts" || is_supported_source_extension(extension)
    }

    fn execute_rts_source<W: Write>(
        source: &[u8],
        args: &[String],
        cwd: &Path,
        output: &mut W,
    ) -> io::Result<i32> {
        let source = std::str::from_utf8(source)
            .map_err(|error| invalid_data(format!("module source is not valid UTF-8: {error}")))?;
        let bytecode = crate::maker_core::compile_source_to_bytecode(source, "rts")?;
        execute_bytecode(&bytecode, args, cwd, output)
    }

    fn exit_status_from_code(code: i32) -> ExitStatus {
        #[cfg(windows)]
        {
            use std::os::windows::process::ExitStatusExt;
            std::process::ExitStatus::from_raw(code as u32)
        }
        #[cfg(not(windows))]
        {
            use std::os::unix::process::ExitStatusExt;
            std::process::ExitStatus::from_raw((code & 0xff) << 8)
        }
    }
    #[cfg(test)]
    mod tests {
        use super::*;
        use std::io::Write;
        use zip::unstable::write::FileOptionsExt;
        use zip::write::FileOptions;
        use zip::ZipWriter;

        fn valid_roadmap() -> Roadmap {
            Roadmap {
                header: "rdmapv1".to_owned(),
                exec: "comp/examplefolder/main.exe".to_owned(),
                name: "Example App".to_owned(),
                callby: "testapp".to_owned(),
                version: "1.0.0".to_owned(),
                description: "Example component".to_owned(),
                author: None,
                license: None,
                default_args: None,
                env: None,
                encrypted_exec: None,
            }
        }

        fn valid_roadmap_v2() -> Roadmap {
            Roadmap {
                header: "rdmapv2".to_owned(),
                exec: "comp/examplefolder/main.exe".to_owned(),
                name: "Example App v2".to_owned(),
                callby: "testapp2".to_owned(),
                version: "2.0.0".to_owned(),
                description: "Example v2 component".to_owned(),
                author: Some("WebTek".to_owned()),
                license: Some("MIT".to_owned()),
                default_args: Some(vec!["--verbose".to_owned()]),
                env: Some(std::collections::HashMap::from([(
                    "APP_ENV".to_owned(),
                    "test".to_owned(),
                )])),
                encrypted_exec: None,
            }
        }

        fn valid_roadmap_v3() -> Roadmap {
            Roadmap {
                header: "rdmapv3".to_owned(),
                exec: "testmod.rts".to_owned(),
                name: "Example Source Module v3".to_owned(),
                callby: "testmod".to_owned(),
                version: "3.0.0".to_owned(),
                description: "Example v3 compiled module".to_owned(),
                author: Some("WebTek".to_owned()),
                license: Some("Apache-2.0".to_owned()),
                default_args: Some(vec!["--list".to_owned()]),
                env: Some(std::collections::HashMap::from([(
                    "MOD_ENV".to_owned(),
                    "test".to_owned(),
                )])),
                encrypted_exec: None,
            }
        }

        fn make_mod_bytes(roadmap: &Roadmap, payload: &[u8]) -> Vec<u8> {
            let manifest_bytes = serde_json::to_vec(roadmap).unwrap();
            let mut container = Vec::new();
            container.extend_from_slice(&(manifest_bytes.len() as u32).to_le_bytes());
            container.extend_from_slice(&manifest_bytes);
            container.extend_from_slice(payload);
            let encrypted = mod_crypto::encrypt_mod_payload(&container).unwrap();

            let mut mod_bytes = Vec::new();
            mod_bytes.extend_from_slice(PREVIOUS_RMOD_MAGIC);
            mod_bytes.extend_from_slice(&encrypted);
            mod_bytes
        }

        #[test]
        fn v6_source_module_is_an_encrypted_zip_with_manifest_and_source() {
            let temp = std::env::temp_dir().join(format!(
                "reterm-mod-v6-test-{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            ));
            fs::create_dir_all(&temp).unwrap();
            let module_path = temp.join("testmod.mod");
            let source = "fn main() { println!(\"private Rust source\"); }\n";
            crate::maker_core::create_mod_package_from_source(
                source,
                "rs",
                &module_path,
                "Private Module".to_owned(),
                "testmod".to_owned(),
                "1.0.0".to_owned(),
                "Encrypted source test".to_owned(),
                None,
                None,
                None,
                None,
            )
            .unwrap();
            let bytes = fs::read(&module_path).unwrap();

            assert!(bytes.starts_with(RMOD_MAGIC));
            assert!(!bytes.windows(b"testmod".len()).any(|w| w == b"testmod"));
            assert!(!bytes.windows(source.len()).any(|w| w == source.as_bytes()));
            let archive_bytes =
                mod_crypto::decrypt_mod_payload(&bytes[RMOD_MAGIC.len()..]).unwrap();
            assert!(archive_bytes.starts_with(b"PK\x03\x04"));
            let mut archive = ZipArchive::new(Cursor::new(archive_bytes)).unwrap();
            assert_eq!(archive.len(), 2);
            assert!(archive.by_name("find.rdmap").is_ok());
            assert!(archive.by_name("testmod.rs").is_ok());

            let (parsed_roadmap, parsed_payload) = parse_mod_bytes(&bytes).unwrap();
            assert_eq!(parsed_roadmap.callby, "testmod");
            assert_eq!(parsed_payload, source.as_bytes());

            let mut tampered = bytes;
            tampered[RMOD_MAGIC.len() + 16] ^= 1;
            assert!(parse_mod_bytes(&tampered)
                .unwrap_err()
                .to_string()
                .contains("authentication failed"));
            fs::remove_dir_all(temp).unwrap();
        }

        #[test]
        fn previous_encrypted_module_layout_accepts_source_only() {
            let roadmap = valid_roadmap_v3();
            let source = b"PRINT legacy source module\nEXIT 0\n";
            let bytes = make_mod_bytes(&roadmap, source);

            let (parsed_roadmap, parsed_payload) = parse_mod_bytes(&bytes).unwrap();
            assert_eq!(parsed_roadmap.callby, roadmap.callby);
            assert_eq!(parsed_payload, source);
        }

        #[test]
        fn roadmap_accepts_the_example_component_format() {
            assert_eq!(
                valid_roadmap().validate().unwrap(),
                PathBuf::from("comp/examplefolder/main.exe")
            );
            assert_eq!(
                valid_roadmap_v2().validate().unwrap(),
                PathBuf::from("comp/examplefolder/main.exe")
            );
            assert_eq!(
                valid_roadmap_v3().validate().unwrap(),
                PathBuf::from("testmod.rts")
            );
        }

        #[test]
        fn roadmap_rejects_unsupported_header_and_invalid_env_key() {
            let mut roadmap = valid_roadmap();
            roadmap.header = "rdmapv4".to_owned();
            assert!(roadmap.validate().is_err());

            let mut roadmap_v2 = valid_roadmap_v2();
            roadmap_v2.env = Some(std::collections::HashMap::from([(
                "INVALID KEY!".to_owned(),
                "val".to_owned(),
            )]));
            assert!(roadmap_v2.validate().is_err());
        }

        #[test]
        fn modadd_installs_and_runs_source_with_rdmapv3_metadata() {
            let temp = std::env::temp_dir().join(format!(
                "reterm-mod-test-{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            ));
            let root = temp.join("components");
            let mod_file = temp.join("testmod.mod");
            fs::create_dir_all(&temp).unwrap();

            crate::maker_core::create_mod_package_from_source(
                "PRINT installed source module\nEXIT 0\n",
                "rts",
                &mod_file,
                "Example Source Module".to_owned(),
                "testmod".to_owned(),
                "3.0.0".to_owned(),
                "Source module install test".to_owned(),
                None,
                None,
                None,
                None,
            )
            .unwrap();

            let installed = install_mod_to(&mod_file, &root).unwrap();
            assert_eq!(installed.callby, "testmod");
            assert_eq!(installed.header, "rdmapv3");
            assert!(root.join("testmod").join("testmod.rts").is_file());

            let status = run_command_in(&root, "testmod", &[], &std::env::current_dir().unwrap())
                .unwrap()
                .unwrap();
            assert!(status.success());

            let removed = remove_installed_in(&root, "testmod").unwrap();
            assert_eq!(removed.name, "Example Source Module");
            assert!(!root.join("testmod").exists());
            fs::remove_dir_all(temp).unwrap();
        }

        #[test]
        fn portable_utility_opcodes_execute_without_native_binaries() {
            let temp = std::env::temp_dir().join(format!(
                "reterm-portable-utilities-{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            ));
            fs::create_dir_all(&temp).unwrap();

            let nuke_bytecode =
                crate::maker_core::compile_source_to_bytecode("NUKE\n", "rts").unwrap();
            assert_eq!(decode_bytecode(&nuke_bytecode).unwrap()[0].0, 0x11);
            let file = temp.join("data.bin");
            fs::write(&file, b"portable bytecode").unwrap();
            let mut output = Vec::new();
            assert_eq!(
                execute_bytecode(
                    &nuke_bytecode,
                    &[file.to_string_lossy().into_owned()],
                    &temp,
                    &mut output
                )
                .unwrap(),
                0
            );
            assert!(fs::read(&file).unwrap().iter().all(|byte| *byte == 0));

            let compall_bytecode =
                crate::maker_core::compile_source_to_bytecode("COMPALL\n", "rts").unwrap();
            assert_eq!(decode_bytecode(&compall_bytecode).unwrap()[0].0, 0x12);
            output.clear();
            execute_bytecode(&compall_bytecode, &[], &temp, &mut output).unwrap();
            assert!(String::from_utf8_lossy(&output)
                .contains("Component batch complete: 0 installed, 0 failed."));

            let shellpass_bytecode =
                crate::maker_core::compile_source_to_bytecode("SHELLPASS\n", "rts").unwrap();
            assert_eq!(decode_bytecode(&shellpass_bytecode).unwrap()[0].0, 0x13);
            #[cfg(windows)]
            let command = "exit /b 7";
            #[cfg(not(windows))]
            let command = "exit 7";
            assert_eq!(
                execute_bytecode(
                    &shellpass_bytecode,
                    &[command.to_owned()],
                    &temp,
                    &mut output
                )
                .unwrap(),
                7
            );

            fs::remove_dir_all(temp).unwrap();
        }

        #[test]
        fn modrun_executes_a_v6_source_module_without_installing_it() {
            let temp = std::env::temp_dir().join(format!(
                "reterm-modrun-test-{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            ));
            fs::create_dir_all(&temp).unwrap();
            let module_file = temp.join("testmod.mod");
            let source = b"PRINT launched directly\nEXIT 3\n";
            crate::maker_core::create_mod_package_from_source(
                std::str::from_utf8(source).unwrap(),
                "rts",
                &module_file,
                "Direct Source Module".to_owned(),
                "testmod".to_owned(),
                "3.0.0".to_owned(),
                "Source module run test".to_owned(),
                None,
                None,
                None,
                None,
            )
            .unwrap();

            let mut output = Vec::new();
            let (module, code) = run_mod_file(&module_file, &[], &temp, &mut output).unwrap();
            assert_eq!(module.callby, "testmod");
            assert_eq!(code, 3);
            assert_eq!(String::from_utf8(output).unwrap(), "launched directly\n");
            fs::remove_dir_all(temp).unwrap();
        }

        #[test]
        fn mod_parser_rejects_bytecode_and_native_binary_modules() {
            let mut bytecode_roadmap = valid_roadmap_v3();
            bytecode_roadmap.exec = "testmod.rbc".to_owned();
            let bytecode_mod = make_mod_bytes(&bytecode_roadmap, b"compiled bytecode");
            assert!(parse_mod_bytes(&bytecode_mod)
                .unwrap_err()
                .to_string()
                .contains("source code"));

            let mut native_roadmap = valid_roadmap_v3();
            native_roadmap.exec = "testmod.rbin".to_owned();
            let native_mod = make_mod_bytes(&native_roadmap, b"\x7FELF native executable");
            assert!(parse_mod_bytes(&native_mod).is_err());

            let compiled_rts =
                crate::maker_core::compile_source_to_bytecode("EXIT 0\n", "rts").unwrap();
            let binary_payload_mod = make_mod_bytes(&valid_roadmap_v3(), &compiled_rts);
            assert!(parse_mod_bytes(&binary_payload_mod).is_err());
        }

        #[test]
        fn install_mod_rejects_renamed_raw_exe_file() {
            let temp = std::env::temp_dir().join(format!(
                "reterm-mod-raw-exe-test-{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            ));
            fs::create_dir_all(&temp).unwrap();
            let fake_mod = temp.join("fake.mod");
            fs::write(&fake_mod, b"MZ......this is a raw exe file").unwrap();

            let result = install_mod_to(&fake_mod, &temp.join("components"));
            assert!(result.is_err());
            let error_msg = result.unwrap_err().to_string();
            assert!(error_msg.contains("unsupported header signature"));
            fs::remove_dir_all(temp).unwrap();
        }

        #[test]
        fn rust_source_module_compiles_and_runs_from_encrypted_zip() {
            let timestamp = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let temp = std::env::temp_dir().join(format!(
                "reterm-rust-source-test-{}-{timestamp}",
                std::process::id()
            ));
            fs::create_dir_all(&temp).unwrap();
            let module_file = temp.join("portable-rust.mod");
            crate::maker_core::create_mod_package_from_source(
                "fn main() { println!(\"Rust source on both hosts\"); }\n",
                "rs",
                &module_file,
                "Portable Rust".to_owned(),
                "portable_rust".to_owned(),
                "1.0.0".to_owned(),
                "Rust source smoke test".to_owned(),
                None,
                None,
                None,
                None,
            )
            .unwrap();

            let (module, payload) = parse_mod_bytes(&fs::read(&module_file).unwrap()).unwrap();
            assert_eq!(module.exec, "portable_rust.rs");
            assert_eq!(
                payload,
                b"fn main() { println!(\"Rust source on both hosts\"); }\n"
            );

            let mut output = Vec::new();
            let (_, code) = run_mod_file(&module_file, &[], &temp, &mut output).unwrap();
            assert_eq!(code, 0);
            assert_eq!(
                String::from_utf8(output).unwrap(),
                "Rust source on both hosts\n"
            );
            fs::remove_dir_all(temp).unwrap();
        }

        #[test]
        fn rust_source_module_installs_and_runs_without_bytecode() {
            let timestamp = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let temp = std::env::temp_dir().join(format!(
                "reterm-mod-source-e2e-{}-{timestamp}",
                std::process::id()
            ));
            let root = temp.join("components");
            fs::create_dir_all(&temp).unwrap();

            let callby = "portable_rust";
            let source = "fn main() { println!(\"Rust runs from source\"); }\n";
            let source_path = temp.join("source.rs");
            let package_path = temp.join(format!("{callby}.mod"));
            fs::write(&source_path, source).unwrap();
            crate::maker_core::create_mod_package(
                &source_path,
                &package_path,
                "Portable Rust".to_owned(),
                callby.to_owned(),
                "1.0.0".to_owned(),
                "Encrypted Rust source smoke test".to_owned(),
                None,
                None,
                None,
                None,
            )
            .unwrap();

            let package_bytes = fs::read(&package_path).unwrap();
            assert!(!package_bytes
                .windows(source.len())
                .any(|window| window == source.as_bytes()));
            let (module, payload) = parse_mod_bytes(&package_bytes).unwrap();
            assert_eq!(module.exec, format!("{callby}.rs"));
            assert_eq!(payload, source.as_bytes());

            let mut output = Vec::new();
            let (_, exit_code) = run_mod_file(&package_path, &[], &temp, &mut output).unwrap();
            assert_eq!(exit_code, 0);
            assert_eq!(
                String::from_utf8(output).unwrap(),
                "Rust runs from source\n"
            );

            let installed = install_mod_to(&package_path, &root).unwrap();
            assert_eq!(installed.exec, format!("{callby}.exe"));
            let installed_executable = fs::read(root.join(callby).join(&installed.exec)).unwrap();
            #[cfg(windows)]
            assert!(installed_executable.starts_with(b"MZ"));
            #[cfg(target_os = "linux")]
            assert!(installed_executable.starts_with(b"\x7FELF"));
            let status = run_command_in(&root, callby, &[], &temp).unwrap().unwrap();
            assert!(status.success(), "installed Rust module failed");

            fs::remove_dir_all(temp).unwrap();
        }

        #[test]
        fn modrun_executes_full_python_syntax_with_host_runtime() {
            let temp = std::env::temp_dir().join(format!(
                "reterm-full-python-test-{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            ));
            fs::create_dir_all(&temp).unwrap();
            let module_file = temp.join("fullpython.mod");
            let source = "def greet(values):\n    return [value.upper() for value in values]\nprint(','.join(greet(['full', 'python'])))\n";
            crate::maker_core::create_mod_package_from_source(
                source,
                "py",
                &module_file,
                "Full Python".to_owned(),
                "fullpython".to_owned(),
                "1.0.0".to_owned(),
                "Execute using CPython".to_owned(),
                None,
                None,
                None,
                None,
            )
            .unwrap();

            let mut output = Vec::new();
            let (_, code) = run_mod_file(&module_file, &[], &temp, &mut output).unwrap();
            assert_eq!(code, 0);
            assert_eq!(
                String::from_utf8(output).unwrap().replace("\r\n", "\n"),
                "FULL,PYTHON\n"
            );
            fs::remove_dir_all(temp).unwrap();
        }

        #[test]
        fn roadmap_rejects_paths_outside_the_component_and_unsupported_files() {
            let mut roadmap = valid_roadmap();
            roadmap.exec = "../outside.exe".to_owned();
            assert!(roadmap.validate().is_err());

            roadmap.exec = "comp/app.ps1".to_owned();
            assert!(roadmap.validate().is_err());
        }

        #[test]
        fn roadmap_rejects_invalid_command_names() {
            let mut roadmap = valid_roadmap();
            roadmap.callby = "../run".to_owned();
            assert!(roadmap.validate().is_err());
        }

        #[test]
        fn compadd_installs_manifest_and_callby_launches_declared_executable() {
            let temp = std::env::temp_dir().join(format!(
                "reterm-component-test-{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            ));
            let root = temp.join("components");
            let package = temp.join("nano.comp");
            fs::create_dir_all(&temp).unwrap();

            let file = File::create(&package).unwrap();
            let mut archive = ZipWriter::new(file);
            let options = FileOptions::default().with_deprecated_encryption(COMPONENT_PASSWORD);
            archive.start_file("find.rdmap", options).unwrap();
            archive
                .write_all(&serde_json::to_vec(&valid_roadmap()).unwrap())
                .unwrap();
            archive
                .start_file("comp/examplefolder/main.exe", options)
                .unwrap();
            archive
                .write_all(&fs::read(std::env::current_exe().unwrap()).unwrap())
                .unwrap();
            archive.finish().unwrap();

            let installed = install_to(&package, &root).unwrap();
            assert_eq!(installed.callby, "testapp");
            assert_eq!(list_installed_in(&root).unwrap()[0].callby, "testapp");
            assert!(root
                .join("testapp")
                .join("comp/examplefolder/main.exe")
                .is_file());
            let status = run_command_in(
                &root,
                "testapp",
                &["--list".to_owned()],
                &std::env::current_dir().unwrap(),
            )
            .unwrap()
            .unwrap();
            assert!(status.success());

            let removed = remove_installed_in(&root, "testapp").unwrap();
            assert_eq!(removed.name, "Example App");
            assert!(!root.join("testapp").exists());
            fs::remove_dir_all(temp).unwrap();
        }

        #[test]
        fn compaddall_installs_every_top_level_component_and_reports_failures() {
            let timestamp = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let temp = std::env::temp_dir().join(format!(
                "reterm-compaddall-test-{}-{timestamp}",
                std::process::id()
            ));
            let packages = temp.join("packages");
            let installed = temp.join("installed");
            fs::create_dir_all(&packages).unwrap();

            for (filename, callby) in [("a.comp", "first"), ("b.COMP", "second")] {
                let package = packages.join(filename);
                let file = File::create(&package).unwrap();
                let mut archive = ZipWriter::new(file);
                let options = FileOptions::default().with_deprecated_encryption(COMPONENT_PASSWORD);
                let mut roadmap = valid_roadmap();
                roadmap.callby = callby.to_owned();
                archive.start_file("find.rdmap", options).unwrap();
                archive
                    .write_all(&serde_json::to_vec(&roadmap).unwrap())
                    .unwrap();
                archive
                    .start_file("comp/examplefolder/main.exe", options)
                    .unwrap();
                archive.write_all(b"test executable").unwrap();
                archive.finish().unwrap();
            }
            fs::write(packages.join("broken.comp"), b"not a zip file").unwrap();
            fs::write(packages.join("ignored.txt"), b"not a component").unwrap();
            fs::create_dir(packages.join("nested.comp")).unwrap();

            let results = install_all_to(&packages, &installed).unwrap();
            assert_eq!(results.len(), 3);
            assert_eq!(
                results.iter().filter(|(_, result)| result.is_ok()).count(),
                2
            );
            assert!(results[0].0.ends_with("a.comp"));
            assert!(results[0].1.as_ref().unwrap().callby == "first");
            assert!(results[1].0.ends_with("b.COMP"));
            assert!(results[1].1.as_ref().unwrap().callby == "second");
            assert!(results[2].1.is_err());
            assert!(installed.join("first/find.rdmap").is_file());
            assert!(installed.join("second/find.rdmap").is_file());
            fs::remove_dir_all(temp).unwrap();
        }
    }
}

mod shell {
    use std::fs;
    use std::io::{self, BufRead, Write};
    use std::path::{Path, PathBuf};
    use std::process::Command;

    use super::components;

    pub fn run() -> io::Result<()> {
        let mut cwd = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("C:\\"));
        let history_file = history_file_path()?;
        let mut history = load_history(&history_file)?;
        let mut directory_stack = Vec::new();
        let mut previous_cwd = None;
        let stdin = io::stdin();
        let stdout = io::stdout();
        run_with_state(
            stdin.lock(),
            stdout.lock(),
            &mut cwd,
            &mut history,
            Some(&history_file),
            &mut directory_stack,
            &mut previous_cwd,
        )
    }

    #[cfg(test)]
    fn run_with_io<R: BufRead, W: Write>(input: R, output: W, cwd: &mut PathBuf) -> io::Result<()> {
        let mut history = Vec::new();
        let mut directory_stack = Vec::new();
        let mut previous_cwd = None;
        run_with_state(
            input,
            output,
            cwd,
            &mut history,
            None,
            &mut directory_stack,
            &mut previous_cwd,
        )
    }

    fn run_with_state<R: BufRead, W: Write>(
        mut input: R,
        mut output: W,
        cwd: &mut PathBuf,
        history: &mut Vec<String>,
        history_file: Option<&Path>,
        directory_stack: &mut Vec<PathBuf>,
        previous_cwd: &mut Option<PathBuf>,
    ) -> io::Result<()> {
        let mut line = String::new();
        writeln!(output, "ReTerm — Rust shell")?;
        writeln!(
            output,
            "Type `help` for commands. Use `exit` to close this terminal."
        )?;

        loop {
            write!(output, "reterm:{}> ", cwd.display())?;
            output.flush()?;
            line.clear();
            if input.read_line(&mut line)? == 0 {
                break;
            }
            let command = line.trim();
            if command.is_empty() {
                continue;
            }
            history.push(command.to_owned());
            if let Some(history_file) = history_file {
                if let Err(error) = append_history(history_file, command) {
                    writeln!(output, "reterm: could not persist history: {error}")?;
                }
            }
            match parse_command(command) {
                Ok(args) if args.is_empty() => {}
                Ok(args) => match execute(
                    &args,
                    cwd,
                    &history,
                    directory_stack,
                    previous_cwd,
                    &mut output,
                ) {
                    Ok(true) => break,
                    Ok(false) => {}
                    Err(error) => writeln!(output, "reterm: {error}")?,
                },
                Err(error) => writeln!(output, "reterm: {error}")?,
            }
        }
        Ok(())
    }

    fn history_file_path() -> io::Result<PathBuf> {
        #[cfg(not(windows))]
        if let Some(data_home) = std::env::var_os("XDG_DATA_HOME") {
            return Ok(PathBuf::from(data_home).join("reterm").join("history.txt"));
        }
        if let Some(local_app_data) = std::env::var_os("LOCALAPPDATA") {
            return Ok(PathBuf::from(local_app_data)
                .join("ReTerm")
                .join("history.txt"));
        }
        Ok(components::home_directory()?
            .join(".reterm")
            .join("history.txt"))
    }

    fn load_history(path: &Path) -> io::Result<Vec<String>> {
        match fs::read_to_string(path) {
            Ok(contents) => Ok(contents.lines().map(str::to_owned).collect()),
            Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(Vec::new()),
            Err(error) => Err(error),
        }
    }

    fn append_history(path: &Path, command: &str) -> io::Result<()> {
        let parent = path
            .parent()
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "invalid history path"))?;
        fs::create_dir_all(parent)?;
        let mut file = fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(path)?;
        writeln!(file, "{command}")
    }

    pub(crate) fn parse_command(line: &str) -> Result<Vec<String>, &'static str> {
        let mut args = Vec::new();
        let mut token = String::new();
        let mut quote = None;
        let mut started = false;
        let mut chars = line.chars().peekable();

        while let Some(ch) = chars.next() {
            match quote {
                Some(delimiter) if ch == delimiter => quote = None,
                Some('"') if ch == '\\' && chars.peek() == Some(&'"') => {
                    token.push(chars.next().unwrap_or_default());
                }
                Some(_) => token.push(ch),
                None if ch == '"' || ch == '\'' => {
                    quote = Some(ch);
                    started = true;
                }
                None if ch.is_whitespace() => {
                    if started {
                        args.push(std::mem::take(&mut token));
                        started = false;
                    }
                }
                None => {
                    token.push(ch);
                    started = true;
                }
            }
        }
        if quote.is_some() {
            return Err("unclosed quote");
        }
        if started {
            args.push(token);
        }
        Ok(args)
    }

    pub(crate) fn resolve_path(cwd: &Path, value: &str) -> PathBuf {
        let path = PathBuf::from(value);
        if path.is_absolute() {
            path
        } else {
            cwd.join(path)
        }
    }

    #[cfg(windows)]
    fn normalize_path(path: PathBuf) -> PathBuf {
        let path_text = path.to_string_lossy();
        if let Some(unc_path) = path_text.strip_prefix(r"\\?\UNC\") {
            return PathBuf::from(format!(r"\\{unc_path}"));
        }
        if let Some(drive_path) = path_text.strip_prefix(r"\\?\") {
            return PathBuf::from(drive_path);
        }
        path
    }

    #[cfg(not(windows))]
    fn normalize_path(path: PathBuf) -> PathBuf {
        path
    }

    fn execute<W: Write>(
        args: &[String],
        cwd: &mut PathBuf,
        history: &[String],
        directory_stack: &mut Vec<PathBuf>,
        previous_cwd: &mut Option<PathBuf>,
        output: &mut W,
    ) -> io::Result<bool> {
        let command = args[0].to_ascii_lowercase();
        let values = &args[1..];
        match command.as_str() {
            "exit" | "quit" => return Ok(true),
            "help" | "?" => {
                writeln!(output, "ReTerm built-in commands:")?;
                writeln!(
                    output,
                    "  cd [path]                 Change this shell's directory"
                )?;
                writeln!(
                    output,
                    "  pwd                       Print the working directory"
                )?;
                writeln!(output, "  ls [path] [-a] [-l]       List files")?;
                writeln!(output, "  cat <file>                Print a text file")?;
                writeln!(output, "  nano [file]               Edit a text file")?;
                writeln!(output, "  echo <text>               Print text")?;
                writeln!(output, "  touch <file>              Create a file")?;
                writeln!(
                    output,
                    "  mkdir <path>              Create a directory tree"
                )?;
                writeln!(output, "  cp <source> <target>      Copy a file")?;
                writeln!(output, "  mv <source> <target>      Move a path")?;
                writeln!(
                    output,
                    "  rm [-r] <path>            Remove a file or directory"
                )?;
                writeln!(
                    output,
                    "  env [name]                List or print environment variables"
                )?;
                writeln!(
                    output,
                    "  history [text]            Show or search command history"
                )?;
                writeln!(output, "  which <command>           Find a command")?;
                writeln!(
                    output,
                    "  find <folder> <name>      Find matching paths recursively"
                )?;
                writeln!(output, "  grep [-i] <text> <file>   Search text in files")?;
                writeln!(output, "  head/tail [-n count] <file> Show file lines")?;
                writeln!(
                    output,
                    "  wc <file>                 Count lines, words, and bytes"
                )?;
                writeln!(output, "  sort [-r] <file>          Sort file lines")?;
                writeln!(
                    output,
                    "  uniq <file>               Remove adjacent duplicate lines"
                )?;
                writeln!(output, "  tree [folder]             Show a directory tree")?;
                writeln!(output, "  basename/dirname <path>   Show path components")?;
                writeln!(output, "  realpath <path>           Resolve a full path")?;
                writeln!(output, "  date                      Show UTC date and time")?;
                writeln!(output, "  whoami / hostname         Show current identity")?;
                writeln!(output, "  sleep <seconds>           Wait for a duration")?;
                writeln!(
                    output,
                    "  seq [start [step]] end    Print an integer sequence"
                )?;
                writeln!(
                    output,
                    "  pushd/popd                Save and restore directories"
                )?;
                writeln!(
                    output,
                    "  export NAME=VALUE         Set a shell environment variable"
                )?;
                writeln!(
                    output,
                    "  unset NAME                Remove a shell environment variable"
                )?;
                writeln!(
                    output,
                    "  compadd <file>.comp       Install one ReTerm component"
                )?;
                writeln!(
                    output,
                    "  compall <folder>         Install every .comp in a folder"
                )?;
                writeln!(
                    output,
                    "  complist                  List installed components"
                )?;
                writeln!(
                    output,
                    "  compremove <callby>       Remove an installed component"
                )?;
                writeln!(
                    output,
                    "  modrun <file>.mod [args]  Run a source ReTerm module"
                )?;
                writeln!(
                    output,
                    "  modadd <file>.mod         Install an executable module"
                )?;
                writeln!(output, "  modlist                   List installed modules")?;
                writeln!(
                    output,
                    "  modremove <callby>        Remove an installed module"
                )?;
                writeln!(output, "  clear                     Clear the terminal")?;
                writeln!(output, "  exit                      Close this shell")?;
                writeln!(
                    output,
                    "Other commands run as executables in this shell's working directory."
                )?;
            }
            "pwd" => writeln!(output, "{}", cwd.display())?,
            "cd" => {
                if values.is_empty() {
                    let home = components::home_directory()?;
                    let target = if home.is_dir() {
                        normalize_path(home.canonicalize()?)
                    } else {
                        normalize_path(home)
                    };
                    *previous_cwd = Some(std::mem::replace(cwd, target));
                } else if values[0] == "-" {
                    if let Some(previous) = previous_cwd.take() {
                        *previous_cwd = Some(std::mem::replace(cwd, previous));
                    } else {
                        writeln!(output, "reterm: no previous directory is available")?;
                    }
                } else {
                    let target =
                        normalize_path(resolve_path(cwd, &values.join(" ")).canonicalize()?);
                    if !target.is_dir() {
                        writeln!(output, "reterm: not a directory: {}", target.display())?;
                    } else {
                        *previous_cwd = Some(std::mem::replace(cwd, target));
                    }
                }
            }
            "ls" | "dir" => list_directory(values, cwd, output)?,
            "which" => {
                if values.len() != 1 {
                    return Err(usage("which <command>"));
                }
                match find_command(&values[0], cwd)? {
                    Some(path) => writeln!(output, "{path}")?,
                    None => writeln!(output, "reterm: command not found: {}", values[0])?,
                }
            }
            "find" => {
                if values.len() != 2 {
                    return Err(usage("find <folder> <name-fragment>"));
                }
                find_paths(&resolve_path(cwd, &values[0]), &values[1], output)?;
            }
            "grep" => grep_files(values, cwd, output)?,
            "head" => show_file_lines(values, cwd, output, false)?,
            "tail" => show_file_lines(values, cwd, output, true)?,
            "wc" => count_file(values, cwd, output)?,
            "sort" => sort_file(values, cwd, output)?,
            "uniq" => unique_lines(values, cwd, output)?,
            "tree" => {
                if values.len() > 1 {
                    return Err(usage("tree [folder]"));
                }
                let root = values
                    .first()
                    .map(|path| resolve_path(cwd, path))
                    .unwrap_or_else(|| cwd.clone());
                writeln!(output, "{}", root.display())?;
                write_tree(&root, 0, output)?;
            }
            "basename" => {
                if values.len() != 1 {
                    return Err(usage("basename <path>"));
                }
                let path = Path::new(&values[0]);
                let name = path.file_name().unwrap_or_else(|| path.as_os_str());
                writeln!(output, "{}", name.to_string_lossy())?;
            }
            "dirname" => {
                if values.len() != 1 {
                    return Err(usage("dirname <path>"));
                }
                let path = Path::new(&values[0]);
                writeln!(
                    output,
                    "{}",
                    path.parent().unwrap_or_else(|| Path::new(".")).display()
                )?;
            }
            "realpath" => {
                let path = required_path(values, cwd, "realpath <path>")?;
                writeln!(output, "{}", normalize_path(path.canonicalize()?).display())?;
            }
            "date" => {
                if !values.is_empty() {
                    return Err(usage("date"));
                }
                writeln!(output, "{}", current_utc_date()?)?;
            }
            "whoami" => {
                if !values.is_empty() {
                    return Err(usage("whoami"));
                }
                let user = std::env::var("USERNAME")
                    .or_else(|_| std::env::var("USER"))
                    .map_err(|_| {
                        io::Error::new(io::ErrorKind::NotFound, "user name is unavailable")
                    })?;
                if let Ok(domain) = std::env::var("USERDOMAIN") {
                    writeln!(output, "{domain}\\{user}")?;
                } else {
                    writeln!(output, "{user}")?;
                }
            }
            "hostname" => {
                if !values.is_empty() {
                    return Err(usage("hostname"));
                }
                let hostname = std::env::var("COMPUTERNAME")
                    .or_else(|_| std::env::var("HOSTNAME"))
                    .map_err(|_| {
                        io::Error::new(io::ErrorKind::NotFound, "host name is unavailable")
                    })?;
                writeln!(output, "{hostname}")?;
            }
            "sleep" => {
                if values.len() != 1 {
                    return Err(usage("sleep <seconds>"));
                }
                let seconds = values[0].parse::<f64>().map_err(|_| {
                    io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "duration must be a non-negative number of seconds",
                    )
                })?;
                if !seconds.is_finite() || seconds < 0.0 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "duration must be a finite non-negative number of seconds",
                    ));
                }
                let duration = std::time::Duration::try_from_secs_f64(seconds).map_err(|_| {
                    io::Error::new(io::ErrorKind::InvalidInput, "duration is too large")
                })?;
                std::thread::sleep(duration);
            }
            "seq" => print_sequence(values, output)?,
            "cat" | "type" => {
                let path = required_path(values, cwd, "cat <file>")?;
                let contents = fs::read_to_string(&path)?;
                write!(output, "{contents}")?;
                if !contents.ends_with('\n') {
                    writeln!(output)?;
                }
            }
            "echo" => writeln!(output, "{}", values.join(" "))?,
            "touch" => {
                let path = required_path(values, cwd, "touch <file>")?;
                fs::OpenOptions::new()
                    .create(true)
                    .append(true)
                    .open(path)?;
            }
            "mkdir" => {
                let path = required_path(values, cwd, "mkdir <path>")?;
                fs::create_dir_all(path)?;
            }
            "cp" | "copy" => {
                let (source, target) = required_pair(values, cwd, "cp <source> <target>")?;
                fs::copy(&source, &target)?;
                writeln!(
                    output,
                    "Copied {} to {}",
                    source.display(),
                    target.display()
                )?;
            }
            "mv" | "move" | "ren" => {
                let (source, target) = required_pair(values, cwd, "mv <source> <target>")?;
                fs::rename(&source, &target)?;
                writeln!(output, "Moved {} to {}", source.display(), target.display())?;
            }
            "rm" | "del" | "erase" => {
                let recursive = values
                    .first()
                    .is_some_and(|value| value == "-r" || value == "-rf");
                let path_args = if recursive { &values[1..] } else { values };
                let path = required_path(path_args, cwd, "rm [-r] <path>")?;
                if path.is_dir() {
                    if !recursive {
                        writeln!(output, "reterm: use rm -r to remove a directory")?;
                    } else {
                        fs::remove_dir_all(path)?;
                    }
                } else {
                    fs::remove_file(path)?;
                }
            }
            "env" => {
                if let Some(name) = values.first() {
                    match std::env::var(name) {
                        Ok(value) => writeln!(output, "{value}")?,
                        Err(error) => writeln!(output, "reterm: {name}: {error}")?,
                    }
                } else {
                    let mut entries: Vec<_> = std::env::vars().collect();
                    entries.sort_by(|left, right| {
                        left.0
                            .to_ascii_lowercase()
                            .cmp(&right.0.to_ascii_lowercase())
                    });
                    for (name, value) in entries {
                        writeln!(output, "{name}={value}")?;
                    }
                }
            }
            "export" => {
                if values.len() != 1 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: export NAME=VALUE",
                    ));
                }
                let (name, value) = values[0].split_once('=').ok_or_else(|| {
                    io::Error::new(io::ErrorKind::InvalidInput, "usage: export NAME=VALUE")
                })?;
                if !valid_env_name(name) {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "environment variable name must start with a letter or '_' and contain only letters, numbers, and '_'",
                    ));
                }
                std::env::set_var(name, value);
            }
            "unset" => {
                if values.len() != 1 || !valid_env_name(&values[0]) {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: unset NAME",
                    ));
                }
                std::env::remove_var(&values[0]);
            }
            "pushd" => {
                if values.is_empty() {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: pushd <path>",
                    ));
                }
                let target = resolve_path(cwd, &values.join(" "));
                let target = normalize_path(target.canonicalize()?);
                if !target.is_dir() {
                    return Err(io::Error::new(
                        io::ErrorKind::NotADirectory,
                        format!("not a directory: {}", target.display()),
                    ));
                }
                directory_stack.push(cwd.clone());
                *previous_cwd = Some(std::mem::replace(cwd, target));
                writeln!(output, "{}", cwd.display())?;
            }
            "popd" => {
                if !values.is_empty() {
                    return Err(io::Error::new(io::ErrorKind::InvalidInput, "usage: popd"));
                }
                let previous = directory_stack.pop().ok_or_else(|| {
                    io::Error::new(io::ErrorKind::NotFound, "directory stack is empty")
                })?;
                *previous_cwd = Some(std::mem::replace(cwd, previous));
                writeln!(output, "{}", cwd.display())?;
            }
            "history" => {
                for (index, command) in history.iter().enumerate() {
                    if values.is_empty()
                        || command
                            .to_ascii_lowercase()
                            .contains(&values.join(" ").to_ascii_lowercase())
                    {
                        writeln!(output, "{:>4}  {command}", index + 1)?;
                    }
                }
            }
            "compadd" => {
                if values.len() != 1 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: compadd <file>.comp",
                    ));
                }
                let package = resolve_path(cwd, &values[0]);
                let installed = components::install(&package)?;
                writeln!(
                    output,
                    "Installed component {} ({}) as `{}`.",
                    installed.name, installed.version, installed.callby
                )?;
            }
            "compaddall" | "compall" => {
                if values.len() != 1 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: compall <folder>",
                    ));
                }
                let package_directory = resolve_path(cwd, &values[0]);
                let results = components::install_all(&package_directory)?;
                let mut installed = 0;
                let mut failed = 0;
                for (package, result) in results {
                    match result {
                        Ok(component) => {
                            installed += 1;
                            writeln!(
                                output,
                                "Installed component {} ({}) as `{}` from {}.",
                                component.name,
                                component.version,
                                component.callby,
                                package.display()
                            )?;
                        }
                        Err(error) => {
                            failed += 1;
                            writeln!(output, "Failed to install {}: {error}", package.display())?;
                        }
                    }
                }
                writeln!(
                    output,
                    "Component batch complete: {installed} installed, {failed} failed."
                )?;
                if failed > 0 {
                    return Err(io::Error::other(format!(
                        "{failed} component package(s) could not be installed"
                    )));
                }
            }
            "complist" => {
                if !values.is_empty() {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: complist",
                    ));
                }
                let installed = components::list_installed()?;
                if installed.is_empty() {
                    writeln!(output, "No components installed.")?;
                } else {
                    for component in installed {
                        let mut details = format!(
                            "{} ({}) — {}",
                            component.name, component.version, component.callby
                        );
                        if let Some(author) = &component.author {
                            details.push_str(&format!(" by {author}"));
                        }
                        if let Some(license) = &component.license {
                            details.push_str(&format!(" [{license}]"));
                        }
                        details.push_str(&format!(" — {}", component.description));
                        writeln!(output, "{details}")?;
                    }
                }
            }
            "compremove" => {
                if values.len() != 1 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: compremove <callby>",
                    ));
                }
                let removed = components::remove_installed(&values[0])?;
                writeln!(
                    output,
                    "Removed component {} (`{}`).",
                    removed.name, removed.callby
                )?;
            }
            "modadd" => {
                if values.len() != 1 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: modadd <file>.mod",
                    ));
                }
                let package = resolve_path(cwd, &values[0]);
                let installed = components::install_mod(&package)?;
                writeln!(
                    output,
                    "Installed module {} ({}) as `{}`.",
                    installed.name, installed.version, installed.callby
                )?;
            }
            "modrun" => {
                if values.is_empty() {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: modrun <file>.mod [arguments...]",
                    ));
                }
                let package = resolve_path(cwd, &values[0]);
                let (module, exit_code) =
                    components::run_mod_file(&package, &values[1..], cwd, output)?;
                writeln!(
                    output,
                    "Module {} ({}) exited with code {exit_code}.",
                    module.name, module.version
                )?;
            }
            "modall" => {
                if values.len() != 1 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: modall <folder>",
                    ));
                }
                let package_directory = resolve_path(cwd, &values[0]);
                let results = components::install_all(&package_directory)?;
                let mut installed = 0;
                let mut failed = 0;
                for (package, result) in results {
                    match result {
                        Ok(component) => {
                            installed += 1;
                            writeln!(
                                output,
                                "Installed module {} ({}) as `{}` from {}.",
                                component.name,
                                component.version,
                                component.callby,
                                package.display()
                            )?;
                        }
                        Err(error) => {
                            failed += 1;
                            writeln!(output, "Failed to install {}: {error}", package.display())?;
                        }
                    }
                }
                writeln!(
                    output,
                    "Module batch complete: {installed} installed, {failed} failed."
                )?;
                if failed > 0 {
                    return Err(io::Error::other(format!(
                        "{failed} package(s) could not be installed"
                    )));
                }
            }
            "modlist" => {
                if !values.is_empty() {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: modlist",
                    ));
                }
                let mods = components::list_installed_mods()?;
                if mods.is_empty() {
                    writeln!(output, "No rdmapv3 modules installed.")?;
                } else {
                    for component in mods {
                        let mut details = format!(
                            "{} ({}) — {}",
                            component.name, component.version, component.callby
                        );
                        if let Some(author) = &component.author {
                            details.push_str(&format!(" by {author}"));
                        }
                        if let Some(license) = &component.license {
                            details.push_str(&format!(" [{license}]"));
                        }
                        details.push_str(&format!(" — {}", component.description));
                        writeln!(output, "{details}")?;
                    }
                }
            }
            "modremove" => {
                if values.len() != 1 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: modremove <callby>",
                    ));
                }
                let removed = components::remove_installed_mod(&values[0])?;
                writeln!(
                    output,
                    "Removed module {} (`{}`).",
                    removed.name, removed.callby
                )?;
            }
            "clear" | "cls" => write!(output, "\x1b[2J\x1b[H")?,
            "nano" => super::nano::run(values, cwd)?,
            _ => {
                let status = match components::run_command(&args[0], values, cwd)? {
                    Some(status) => status,
                    None => Command::new(&args[0])
                        .args(values)
                        .current_dir(cwd)
                        .status()?,
                };
                if !status.success() {
                    writeln!(output, "Process exited with status {status}.")?;
                }
            }
        }
        Ok(false)
    }

    fn valid_env_name(name: &str) -> bool {
        let mut chars = name.chars();
        chars
            .next()
            .is_some_and(|first| first.is_ascii_alphabetic() || first == '_')
            && chars.all(|ch| ch.is_ascii_alphanumeric() || ch == '_')
    }

    fn find_command(command: &str, cwd: &Path) -> io::Result<Option<String>> {
        const BUILTINS: &[&str] = &[
            "basename",
            "cat",
            "cd",
            "clear",
            "compadd",
            "compaddall",
            "compall",
            "compremove",
            "complist",
            "cp",
            "date",
            "dirname",
            "echo",
            "env",
            "exit",
            "export",
            "find",
            "grep",
            "head",
            "help",
            "history",
            "hostname",
            "ls",
            "mkdir",
            "mv",
            "nano",
            "popd",
            "pushd",
            "pwd",
            "realpath",
            "rm",
            "seq",
            "sleep",
            "sort",
            "tail",
            "touch",
            "tree",
            "uniq",
            "unset",
            "wc",
            "which",
            "whoami",
        ];
        if BUILTINS
            .iter()
            .any(|builtin| builtin.eq_ignore_ascii_case(command))
        {
            return Ok(Some(format!("{command} (ReTerm built-in)")));
        }
        if let Some(component) = components::list_installed()?
            .into_iter()
            .find(|component| component.callby.eq_ignore_ascii_case(command))
        {
            return Ok(Some(format!("{} (installed component)", component.callby)));
        }

        let command_path = Path::new(command);
        let has_extension = command_path.extension().is_some();
        let extensions = if has_extension {
            vec![String::new()]
        } else {
            #[cfg(windows)]
            let configured =
                std::env::var("PATHEXT").unwrap_or_else(|_| ".COM;.EXE;.BAT;.CMD".to_owned());
            #[cfg(not(windows))]
            let configured = String::new();
            std::iter::once(String::new())
                .chain(
                    configured
                        .split(';')
                        .filter(|ext| !ext.is_empty())
                        .map(str::to_owned),
                )
                .collect()
        };
        let mut directories = vec![cwd.to_path_buf()];
        directories.extend(std::env::split_paths(
            &std::env::var_os("PATH").unwrap_or_default(),
        ));

        for directory in directories {
            for extension in &extensions {
                let candidate = if extension.is_empty() {
                    directory.join(command_path)
                } else {
                    directory.join(format!("{command}{extension}"))
                };
                if candidate.is_file() {
                    return Ok(Some(candidate.to_string_lossy().into_owned()));
                }
            }
        }
        Ok(None)
    }

    fn find_paths<W: Write>(directory: &Path, fragment: &str, output: &mut W) -> io::Result<()> {
        if !directory.is_dir() {
            return Err(io::Error::new(
                io::ErrorKind::NotADirectory,
                format!("not a directory: {}", directory.display()),
            ));
        }
        let mut entries = fs::read_dir(directory)?.collect::<Result<Vec<_>, _>>()?;
        entries.sort_by_key(|entry| entry.file_name().to_string_lossy().to_ascii_lowercase());
        for entry in entries {
            let kind = entry.file_type()?;
            if kind.is_symlink() {
                continue;
            }
            let path = entry.path();
            if entry
                .file_name()
                .to_string_lossy()
                .to_ascii_lowercase()
                .contains(&fragment.to_ascii_lowercase())
            {
                writeln!(output, "{}", path.display())?;
            }
            if kind.is_dir() {
                find_paths(&path, fragment, output)?;
            }
        }
        Ok(())
    }

    fn grep_files<W: Write>(values: &[String], cwd: &Path, output: &mut W) -> io::Result<()> {
        let (ignore_case, args) = if values.first().is_some_and(|value| value == "-i") {
            (true, &values[1..])
        } else {
            (false, values)
        };
        if args.len() < 2 {
            return Err(usage("grep [-i] <text> <file> [file ...]"));
        }
        let needle = &args[0];
        for value in &args[1..] {
            let path = resolve_path(cwd, value);
            let contents = fs::read_to_string(&path)?;
            for (line_number, line) in contents.lines().enumerate() {
                let matches = if ignore_case {
                    line.to_ascii_lowercase()
                        .contains(&needle.to_ascii_lowercase())
                } else {
                    line.contains(needle)
                };
                if matches {
                    writeln!(output, "{}:{}:{}", path.display(), line_number + 1, line)?;
                }
            }
        }
        Ok(())
    }

    fn show_file_lines<W: Write>(
        values: &[String],
        cwd: &Path,
        output: &mut W,
        from_end: bool,
    ) -> io::Result<()> {
        let usage_text = if from_end {
            "tail [-n count] <file>"
        } else {
            "head [-n count] <file>"
        };
        let (count, file) = if values.first().is_some_and(|value| value == "-n") {
            if values.len() != 3 {
                return Err(usage(usage_text));
            }
            let count = values[1].parse::<usize>().map_err(|_| {
                io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "line count must be a non-negative integer",
                )
            })?;
            (count, &values[2])
        } else if values.len() == 1 {
            (10, &values[0])
        } else {
            return Err(usage(usage_text));
        };
        let contents = fs::read_to_string(resolve_path(cwd, file))?;
        let lines = contents.lines().collect::<Vec<_>>();
        let selected = if from_end {
            &lines[lines.len().saturating_sub(count)..]
        } else {
            &lines[..lines.len().min(count)]
        };
        for line in selected {
            writeln!(output, "{line}")?;
        }
        Ok(())
    }

    fn count_file<W: Write>(values: &[String], cwd: &Path, output: &mut W) -> io::Result<()> {
        if values.len() != 1 {
            return Err(usage("wc <file>"));
        }
        let path = resolve_path(cwd, &values[0]);
        let contents = fs::read(&path)?;
        let text = String::from_utf8_lossy(&contents);
        writeln!(
            output,
            "{} {} {} {}",
            text.lines().count(),
            text.split_whitespace().count(),
            contents.len(),
            path.display()
        )
    }

    fn sort_file<W: Write>(values: &[String], cwd: &Path, output: &mut W) -> io::Result<()> {
        let (reverse, file) = if values.first().is_some_and(|value| value == "-r") {
            if values.len() != 2 {
                return Err(usage("sort [-r] <file>"));
            }
            (true, &values[1])
        } else if values.len() == 1 {
            (false, &values[0])
        } else {
            return Err(usage("sort [-r] <file>"));
        };
        let contents = fs::read_to_string(resolve_path(cwd, file))?;
        let mut lines = contents.lines().collect::<Vec<_>>();
        lines.sort_unstable_by_key(|line| line.to_ascii_lowercase());
        if reverse {
            lines.reverse();
        }
        for line in lines {
            writeln!(output, "{line}")?;
        }
        Ok(())
    }

    fn unique_lines<W: Write>(values: &[String], cwd: &Path, output: &mut W) -> io::Result<()> {
        if values.len() != 1 {
            return Err(usage("uniq <file>"));
        }
        let contents = fs::read_to_string(resolve_path(cwd, &values[0]))?;
        let mut previous = None;
        for line in contents.lines() {
            if previous != Some(line) {
                writeln!(output, "{line}")?;
                previous = Some(line);
            }
        }
        Ok(())
    }

    fn write_tree<W: Write>(directory: &Path, depth: usize, output: &mut W) -> io::Result<()> {
        let mut entries = fs::read_dir(directory)?.collect::<Result<Vec<_>, _>>()?;
        entries.sort_by_key(|entry| entry.file_name().to_string_lossy().to_ascii_lowercase());
        for entry in entries {
            let kind = entry.file_type()?;
            if kind.is_symlink() {
                continue;
            }
            writeln!(
                output,
                "{}{}{}",
                "  ".repeat(depth),
                entry.file_name().to_string_lossy(),
                if kind.is_dir() { "\\" } else { "" }
            )?;
            if kind.is_dir() {
                write_tree(&entry.path(), depth + 1, output)?;
            }
        }
        Ok(())
    }

    fn current_utc_date() -> io::Result<String> {
        let elapsed = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_err(io::Error::other)?;
        let days = (elapsed.as_secs() / 86_400) as i64;
        let seconds = elapsed.as_secs() % 86_400;
        let z = days + 719_468;
        let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
        let day_of_era = z - era * 146_097;
        let year_of_era =
            (day_of_era - day_of_era / 1_460 + day_of_era / 36_524 - day_of_era / 146_096) / 365;
        let mut year = year_of_era + era * 400;
        let day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
        let month_prime = (5 * day_of_year + 2) / 153;
        let day = day_of_year - (153 * month_prime + 2) / 5 + 1;
        let month = month_prime + if month_prime < 10 { 3 } else { -9 };
        year += i64::from(month <= 2);
        Ok(format!(
            "{year:04}-{month:02}-{day:02} {:02}:{:02}:{:02} UTC",
            seconds / 3_600,
            (seconds % 3_600) / 60,
            seconds % 60
        ))
    }

    fn print_sequence<W: Write>(values: &[String], output: &mut W) -> io::Result<()> {
        if !(1..=3).contains(&values.len()) {
            return Err(usage("seq [start [step]] end"));
        }
        let parsed = values
            .iter()
            .map(|value| {
                value.parse::<i64>().map_err(|_| {
                    io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "sequence values must be integers",
                    )
                })
            })
            .collect::<io::Result<Vec<_>>>()?;
        let (mut current, step, end) = match parsed.as_slice() {
            [end] => (1, 1, *end),
            [start, end] => (*start, if start <= end { 1 } else { -1 }, *end),
            [start, step, end] if *step != 0 => (*start, *step, *end),
            _ => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "sequence step must not be zero",
                ))
            }
        };
        if (step > 0 && current > end) || (step < 0 && current < end) {
            return Ok(());
        }
        for _ in 0..100_000 {
            writeln!(output, "{current}")?;
            if current == end {
                return Ok(());
            }
            let next = current.checked_add(step).ok_or_else(|| {
                io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "sequence overflows integer range",
                )
            })?;
            if (step > 0 && next > end) || (step < 0 && next < end) {
                return Ok(());
            }
            current = next;
        }
        Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "sequence exceeds the 100000 line limit",
        ))
    }

    fn required_path(values: &[String], cwd: &Path, usage: &str) -> io::Result<PathBuf> {
        values
            .first()
            .map(|value| resolve_path(cwd, value))
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, format!("usage: {usage}")))
    }

    fn usage(message: &str) -> io::Error {
        io::Error::new(io::ErrorKind::InvalidInput, format!("usage: {message}"))
    }

    fn required_pair(values: &[String], cwd: &Path, usage: &str) -> io::Result<(PathBuf, PathBuf)> {
        if values.len() < 2 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("usage: {usage}"),
            ));
        }
        Ok((resolve_path(cwd, &values[0]), resolve_path(cwd, &values[1])))
    }

    fn list_directory<W: Write>(values: &[String], cwd: &Path, output: &mut W) -> io::Result<()> {
        let mut all = false;
        let mut long = false;
        let mut path = None;
        for value in values {
            if value.starts_with('-') {
                all |= value.contains('a');
                long |= value.contains('l');
            } else {
                path = Some(value);
            }
        }
        let path = path
            .map(|value| resolve_path(cwd, value))
            .unwrap_or_else(|| cwd.to_path_buf());
        let mut entries = fs::read_dir(&path)?.collect::<Result<Vec<_>, _>>()?;
        entries.sort_by_key(|entry| entry.file_name().to_string_lossy().to_ascii_lowercase());
        for entry in entries {
            let name = entry.file_name().to_string_lossy().into_owned();
            if !all && name.starts_with('.') {
                continue;
            }
            let metadata = entry.metadata()?;
            if long {
                writeln!(
                    output,
                    "{:>12}  {}{}",
                    metadata.len(),
                    name,
                    if metadata.is_dir() { "\\" } else { "" }
                )?;
            } else if metadata.is_dir() {
                writeln!(output, "[{name}]")?;
            } else {
                writeln!(output, "{name}")?;
            }
        }
        Ok(())
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use std::io::Cursor;

        #[test]
        fn parser_preserves_quoted_paths_and_empty_arguments() {
            assert_eq!(
                parse_command(r#"cd "a folder""#).unwrap(),
                ["cd", "a folder"]
            );
            assert_eq!(
                parse_command(r#"echo "" done"#).unwrap(),
                ["echo", "", "done"]
            );
            assert!(parse_command(r#"cat "unfinished"#).is_err());
        }

        #[test]
        fn shell_keeps_directory_changes_for_later_commands() {
            let timestamp = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let root = std::env::temp_dir().join(format!("reterm-shell-{timestamp}"));
            fs::create_dir_all(root.join("child")).unwrap();

            let input = Cursor::new("cd child\npwd\nexit\n");
            let mut output = Vec::new();
            let mut cwd = root.clone();
            run_with_io(input, &mut output, &mut cwd).unwrap();

            assert_eq!(
                cwd,
                normalize_path(root.join("child").canonicalize().unwrap())
            );
            let output = String::from_utf8(output).unwrap();
            assert!(output.contains(&cwd.to_string_lossy().to_string()));
            fs::remove_dir_all(root).unwrap();
        }

        #[test]
        fn directory_stack_and_cd_dash_restore_previous_directories() {
            let timestamp = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let root = std::env::temp_dir().join(format!("reterm-dir-stack-{timestamp}"));
            fs::create_dir_all(root.join("child")).unwrap();
            let input = Cursor::new("pushd child\npopd\ncd child\ncd -\npwd\nexit\n");
            let mut output = Vec::new();
            let mut cwd = root.clone();

            run_with_io(input, &mut output, &mut cwd).unwrap();

            assert_eq!(cwd, normalize_path(root.canonicalize().unwrap()));
            assert!(String::from_utf8(output)
                .unwrap()
                .contains(&root.display().to_string()));
            fs::remove_dir_all(root).unwrap();
        }

        #[test]
        fn text_file_commands_work_on_paths_relative_to_the_shell_directory() {
            let timestamp = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let root = std::env::temp_dir().join(format!("reterm-text-tools-{timestamp}"));
            fs::create_dir_all(root.join("nested")).unwrap();
            fs::write(root.join("lines.txt"), "pear\nApple\npear\nline\n").unwrap();

            let mut output = Vec::new();
            let mut cwd = root.clone();
            let input = Cursor::new(
                "head -n 2 lines.txt\ntail -n 1 lines.txt\nwc lines.txt\nsort lines.txt\nuniq lines.txt\ngrep -i pear lines.txt\nfind . lines\ntree .\nexit\n",
            );
            run_with_io(input, &mut output, &mut cwd).unwrap();
            let output = String::from_utf8(output).unwrap();
            assert!(output.contains("4 4 21"));
            assert!(output.contains("Apple"));
            assert!(output.contains("line"));
            assert!(output.contains("pear"));
            assert!(output.contains(&root.join("lines.txt").display().to_string()));
            assert!(output.contains("nested\\"));
            fs::remove_dir_all(root).unwrap();
        }

        #[test]
        fn date_and_integer_sequences_return_bounded_output() {
            let mut sequence = Vec::new();
            print_sequence(
                &["5".to_owned(), "-2".to_owned(), "1".to_owned()],
                &mut sequence,
            )
            .unwrap();
            assert_eq!(String::from_utf8(sequence).unwrap(), "5\n3\n1\n");
            let date = current_utc_date().unwrap();
            assert_eq!(date.len(), "2026-10-07 01:23:45 UTC".len());
            assert!(print_sequence(
                &["1".to_owned(), "0".to_owned(), "3".to_owned()],
                &mut Vec::new()
            )
            .is_err());
        }

        #[test]
        fn export_and_unset_change_the_shell_environment() {
            let timestamp = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let variable = format!("RETERM_TEST_{}_{}", std::process::id(), timestamp);
            let input = Cursor::new(format!(
                "export {variable}=enabled\nenv {variable}\nunset {variable}\nenv {variable}\nexit\n"
            ));
            let mut output = Vec::new();
            let mut cwd = std::env::current_dir().unwrap();
            run_with_io(input, &mut output, &mut cwd).unwrap();
            assert_eq!(
                std::env::var(&variable).unwrap_err(),
                std::env::VarError::NotPresent
            );
            let output = String::from_utf8(output).unwrap();
            assert!(output.contains("enabled"));
            assert!(output.contains(&format!("{variable}: environment variable not found")));
        }

        #[test]
        fn shell_runs_rust_builtins_without_a_command_interpreter() {
            let input = Cursor::new("echo hello world\nexit\n");
            let mut output = Vec::new();
            let mut cwd = std::env::current_dir().unwrap();
            run_with_io(input, &mut output, &mut cwd).unwrap();
            assert!(String::from_utf8(output).unwrap().contains("hello world"));
        }

        #[test]
        fn shell_history_persists_across_runs() {
            let timestamp = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let root = std::env::temp_dir().join(format!("reterm-history-{timestamp}"));
            let history_file = root.join("history.txt");
            let mut history = load_history(&history_file).unwrap();
            let mut cwd = root.clone();
            let input = Cursor::new("echo saved command\nexit\n");
            let mut output = Vec::new();

            run_with_state(
                input,
                &mut output,
                &mut cwd,
                &mut history,
                Some(&history_file),
                &mut Vec::new(),
                &mut None,
            )
            .unwrap();

            assert_eq!(
                load_history(&history_file).unwrap(),
                ["echo saved command", "exit"]
            );
            fs::remove_dir_all(root).unwrap();
        }
    }
}

#[allow(dead_code)]
fn main() {
    let args = std::env::args_os().skip(1).collect::<Vec<_>>();
    if args.first().is_some_and(|arg| arg == "--run-mod") {
        if args.len() < 2 {
            eprintln!("ReTerm: usage: ReTerm --run-mod <file>.mod [arguments...]");
            std::process::exit(2);
        }
        let package = std::path::PathBuf::from(&args[1]);
        let module_args = args[2..]
            .iter()
            .map(|arg| arg.to_string_lossy().into_owned())
            .collect::<Vec<_>>();
        let cwd = match std::env::current_dir() {
            Ok(path) => path,
            Err(error) => {
                eprintln!("ReTerm: could not determine current directory: {error}");
                std::process::exit(1);
            }
        };
        let stdout = std::io::stdout();
        let mut output = stdout.lock();
        match components::run_mod_file(&package, &module_args, &cwd, &mut output) {
            Ok((_module, exit_code)) => std::process::exit(exit_code),
            Err(error) => {
                eprintln!("ReTerm: {error}");
                std::process::exit(1);
            }
        }
    }
    if args.first().is_some_and(|arg| arg == "--compadd-dir") {
        if args.len() != 2 {
            eprintln!("ReTerm: usage: ReTerm.exe --compadd-dir <folder>");
            std::process::exit(2);
        }
        let directory = std::path::PathBuf::from(&args[1]);
        match components::install_all(&directory) {
            Ok(results) => {
                let mut installed = 0;
                let mut failed = 0;
                for (package, result) in results {
                    match result {
                        Ok(component) => {
                            installed += 1;
                            println!(
                                "Installed component {} ({}) as `{}` from {}.",
                                component.name,
                                component.version,
                                component.callby,
                                package.display()
                            );
                        }
                        Err(error) => {
                            failed += 1;
                            eprintln!("Failed to install {}: {error}", package.display());
                        }
                    }
                }
                println!("Component batch complete: {installed} installed, {failed} failed.");
                if failed > 0 {
                    std::process::exit(1);
                }
            }
            Err(error) => {
                eprintln!("ReTerm: {error}");
                std::process::exit(1);
            }
        }
        return;
    }
    if let Err(error) = shell::run() {
        eprintln!("ReTerm shell error: {error}");
        std::process::exit(1);
    }
}
