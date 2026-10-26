use eframe::egui::{self, Color32, RichText, TextEdit, Vec2};
use std::path::{Path, PathBuf};
use std::process::Command;

const PANEL: Color32 = Color32::from_rgb(24, 29, 42);
const TEXT: Color32 = Color32::from_rgb(231, 237, 247);
const MUTED: Color32 = Color32::from_rgb(151, 165, 188);
const ACCENT: Color32 = Color32::from_rgb(105, 220, 184);

struct ModLauncher {
    reterm_path: String,
    module_path: String,
    arguments: String,
    output: String,
}

impl Default for ModLauncher {
    fn default() -> Self {
        Self {
            reterm_path: default_reterm_path().to_string_lossy().into_owned(),
            module_path: String::new(),
            arguments: String::new(),
            output: "Choose a portable .mod file, then click Launch Module.".to_owned(),
        }
    }
}

impl ModLauncher {
    fn choose_module(&mut self) {
        if let Some(path) = rfd::FileDialog::new()
            .add_filter("ReTerm Module", &["mod"])
            .pick_file()
        {
            self.module_path = path.to_string_lossy().into_owned();
        }
    }

    fn choose_reterm(&mut self) {
        if let Some(path) = rfd::FileDialog::new().pick_file() {
            self.reterm_path = path.to_string_lossy().into_owned();
        }
    }

    fn launch(&mut self) {
        let module = PathBuf::from(self.module_path.trim());
        if !module.is_file() {
            self.output = "Select an existing .mod file.".to_owned();
            return;
        }
        let reterm = PathBuf::from(self.reterm_path.trim());
        if !reterm.is_file() {
            self.output = "Select the ReTerm executable for this platform.".to_owned();
            return;
        }
        let args = match parse_arguments(&self.arguments) {
            Ok(args) => args,
            Err(error) => {
                self.output = error.to_owned();
                return;
            }
        };
        let working_directory = module.parent().unwrap_or_else(|| Path::new("."));
        match Command::new(reterm)
            .arg("--run-mod")
            .arg(&module)
            .args(args)
            .current_dir(working_directory)
            .output()
        {
            Ok(result) => {
                let mut text = String::from_utf8_lossy(&result.stdout).into_owned();
                let stderr = String::from_utf8_lossy(&result.stderr);
                if !stderr.is_empty() {
                    if !text.is_empty() {
                        text.push('\n');
                    }
                    text.push_str(&stderr);
                }
                self.output = format!("Exit status: {}\n{}", result.status, text.trim_end());
            }
            Err(error) => self.output = format!("Could not launch ReTerm: {error}"),
        }
    }
}

impl eframe::App for ModLauncher {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::CentralPanel::default()
            .frame(egui::Frame::new().fill(PANEL).inner_margin(24))
            .show(ctx, |ui| {
                ui.heading(RichText::new("ReTerm Module Launcher").color(TEXT));
                ui.label(
                    RichText::new(
                        "Runs portable .mod files through ReTerm's VM on Windows and Linux.",
                    )
                    .color(MUTED),
                );
                ui.add_space(18.0);
                if path_row(ui, "ReTerm executable", &mut self.reterm_path) {
                    self.choose_reterm();
                }
                if path_row(ui, "Module (.mod)", &mut self.module_path) {
                    self.choose_module();
                }
                ui.label(RichText::new("Module arguments (quotes supported)").color(MUTED));
                ui.add(TextEdit::singleline(&mut self.arguments).desired_width(f32::INFINITY));
                ui.add_space(14.0);
                if ui
                    .add(
                        egui::Button::new(RichText::new("Launch Module").strong())
                            .fill(ACCENT)
                            .min_size(Vec2::new(150.0, 38.0)),
                    )
                    .clicked()
                {
                    self.launch();
                }
                ui.add_space(14.0);
                ui.label(RichText::new("Output").color(MUTED));
                ui.add(
                    TextEdit::multiline(&mut self.output)
                        .desired_rows(12)
                        .desired_width(f32::INFINITY),
                );
            });
    }
}

fn path_row(ui: &mut egui::Ui, label: &str, value: &mut String) -> bool {
    ui.label(RichText::new(label).color(MUTED));
    ui.horizontal(|ui| {
        ui.add(TextEdit::singleline(value).desired_width(ui.available_width() - 95.0));
        ui.button("Browse...").clicked()
    })
    .inner
}

fn default_reterm_path() -> PathBuf {
    let executable = if cfg!(windows) {
        "ReTerm.exe"
    } else {
        "ReTerm"
    };
    std::env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(|parent| parent.join(executable)))
        .unwrap_or_else(|| PathBuf::from(executable))
}

fn parse_arguments(text: &str) -> Result<Vec<String>, &'static str> {
    let mut args = Vec::new();
    let mut value = String::new();
    let mut quote = None;
    for ch in text.chars() {
        match quote {
            Some(q) if ch == q => quote = None,
            Some(_) => value.push(ch),
            None if ch == '\'' || ch == '"' => quote = Some(ch),
            None if ch.is_whitespace() => {
                if !value.is_empty() {
                    args.push(std::mem::take(&mut value));
                }
            }
            None => value.push(ch),
        }
    }
    if quote.is_some() {
        return Err("Module arguments contain an unclosed quote");
    }
    if !value.is_empty() {
        args.push(value);
    }
    Ok(args)
}

fn main() -> eframe::Result<()> {
    eframe::run_native(
        "ReTerm Module Launcher",
        eframe::NativeOptions {
            viewport: egui::ViewportBuilder::default()
                .with_fullscreen(true)
                .with_inner_size([700.0, 480.0]),
            ..Default::default()
        },
        Box::new(|_| Ok(Box::<ModLauncher>::default())),
    )
}
