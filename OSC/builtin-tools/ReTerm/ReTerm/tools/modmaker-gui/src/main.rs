#[path = "../../../src/maker_core.rs"]
mod maker_core;

use eframe::egui::{self, Color32, RichText, Stroke, Vec2};
use std::path::PathBuf;

const BACKGROUND: Color32 = Color32::from_rgb(15, 18, 27);
const PANEL: Color32 = Color32::from_rgb(24, 29, 42);
const BORDER: Color32 = Color32::from_rgb(51, 61, 82);
const TEXT: Color32 = Color32::from_rgb(231, 237, 247);
const MUTED: Color32 = Color32::from_rgb(151, 165, 188);
const ACCENT: Color32 = Color32::from_rgb(105, 220, 184);

#[derive(Default)]
struct ModMakerApp {
    source_path: String,
    output_path: String,
    name: String,
    callby: String,
    version: String,
    description: String,
    author: String,
    license: String,
    message: Option<(bool, String)>,
}

impl ModMakerApp {
    fn pick_source(&mut self) {
        let dialog = rfd::FileDialog::new().add_filter(
            "Source Files",
            &["rs", "py", "c", "cpp", "cc", "cxx", "java", "rts"],
        );
        if let Some(path) = dialog.pick_file() {
            let path_str = path.to_string_lossy().into_owned();
            self.source_path = path_str.clone();
            if self.output_path.is_empty() {
                let stem = path.file_stem().unwrap_or_default().to_string_lossy();
                let parent = path.parent().unwrap_or_else(|| std::path::Path::new("."));
                self.output_path = parent
                    .join(format!("{stem}.mod"))
                    .to_string_lossy()
                    .into_owned();
            }
            if self.callby.is_empty() {
                self.callby = path
                    .file_stem()
                    .unwrap_or_default()
                    .to_string_lossy()
                    .into_owned();
            }
            if self.name.is_empty() {
                self.name = path
                    .file_stem()
                    .unwrap_or_default()
                    .to_string_lossy()
                    .into_owned();
            }
        }
    }

    fn pick_output_path(&mut self) {
        let dialog = rfd::FileDialog::new()
            .add_filter("ReTerm Module", &["mod"])
            .set_file_name("module.mod");
        if let Some(path) = dialog.save_file() {
            self.output_path = path.to_string_lossy().into_owned();
        }
    }

    fn create_module(&mut self) {
        let author_opt = (!self.author.trim().is_empty()).then(|| self.author.trim().to_owned());
        let license_opt = (!self.license.trim().is_empty()).then(|| self.license.trim().to_owned());

        let result = maker_core::create_mod_package(
            &PathBuf::from(&self.source_path),
            &PathBuf::from(&self.output_path),
            self.name.clone(),
            self.callby.clone(),
            self.version.clone(),
            self.description.clone(),
            author_opt,
            license_opt,
            None,
            None,
        );

        self.message = Some(match result {
            Ok(path) => (
                true,
                format!("Built source-only ReTerm module: {}", path.display()),
            ),
            Err(error) => (false, error.to_string()),
        });
    }
}

impl eframe::App for ModMakerApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::CentralPanel::default()
            .frame(
                egui::Frame::new()
                    .fill(BACKGROUND)
                    .inner_margin(egui::Margin::same(24)),
            )
            .show(ctx, |ui| {
                ui.set_max_width(760.0);
                ui.vertical(|ui| {
                    ui.label(
                        RichText::new("ReTerm ModMaker")
                            .size(24.0)
                            .strong()
                            .color(TEXT),
                    );
                    ui.add_space(4.0);
                    ui.label(
                        RichText::new("Build one encrypted ReTerm .mod from source code only. Supported languages: Rust, Python, C, C++, Java, and ReTerm script (.rts). ReTerm uses the matching host runtime/compiler when launched.")
                            .color(MUTED),
                    );
                    ui.add_space(20.0);

                    egui::Frame::new()
                        .fill(PANEL)
                        .stroke(Stroke::new(1.0_f32, BORDER))
                        .corner_radius(10)
                        .inner_margin(egui::Margin::same(18))
                        .show(ui, |ui| {
                            let browse_source = path_row(ui, "Source File Path", &mut self.source_path);
                            if browse_source {
                                self.pick_source();
                            }
                            ui.label(
                                RichText::new("Install the matching host runtime/compiler. Cargo projects and external dependencies are not bundled.")
                                    .size(12.0)
                                    .color(MUTED),
                            );
                            ui.add_space(10.0);
                            let browse_output = path_row(ui, "Output .mod File Path", &mut self.output_path);
                            if browse_output {
                                self.pick_output_path();
                            }
                        });
                    ui.add_space(14.0);

                    egui::Frame::new()
                        .fill(PANEL)
                        .stroke(Stroke::new(1.0_f32, BORDER))
                        .corner_radius(10)
                        .inner_margin(egui::Margin::same(18))
                        .show(ui, |ui| {
                            ui.label(RichText::new("Module Specifications (rdmapv3)").strong().color(TEXT));
                            ui.add_space(8.0);
                            ui.label(RichText::new("Module Name").color(MUTED));
                            ui.text_edit_singleline(&mut self.name);
                            ui.add_space(8.0);
                            ui.label(RichText::new("Command Name (callby)").color(MUTED));
                            ui.text_edit_singleline(&mut self.callby);
                            ui.add_space(8.0);
                            ui.label(RichText::new("Version").color(MUTED));
                            ui.text_edit_singleline(&mut self.version);
                            ui.add_space(8.0);
                            ui.label(RichText::new("Author (Optional)").color(MUTED));
                            ui.text_edit_singleline(&mut self.author);
                            ui.add_space(8.0);
                            ui.label(RichText::new("License (Optional)").color(MUTED));
                            ui.text_edit_singleline(&mut self.license);
                            ui.add_space(8.0);
                            ui.label(RichText::new("Description").color(MUTED));
                            ui.add(
                                egui::TextEdit::multiline(&mut self.description)
                                    .desired_rows(3)
                                    .desired_width(f32::INFINITY),
                            );
                        });
                    ui.add_space(18.0);

                    let ready = !self.source_path.trim().is_empty()
                        && !self.output_path.trim().is_empty()
                        && !self.name.trim().is_empty()
                        && !self.callby.trim().is_empty()
                        && !self.version.trim().is_empty()
                        && !self.description.trim().is_empty();

                    if ui
                        .add_enabled(
                            ready,
                            egui::Button::new(
                                RichText::new("Build Executable .mod")
                                    .strong()
                                    .color(Color32::from_rgb(12, 25, 25)),
                            )
                            .fill(ACCENT)
                            .min_size(Vec2::new(170.0, 40.0)),
                        )
                        .clicked()
                    {
                        self.create_module();
                    }

                    if let Some((success, message)) = &self.message {
                        ui.add_space(10.0);
                        ui.label(RichText::new(message).color(if *success {
                            ACCENT
                        } else {
                            Color32::LIGHT_RED
                        }));
                    }
                });
            });
    }
}

fn path_row(ui: &mut egui::Ui, label: &str, value: &mut String) -> bool {
    ui.label(RichText::new(label).color(MUTED));
    ui.horizontal(|ui| {
        ui.add(egui::TextEdit::singleline(value).desired_width(ui.available_width() - 105.0));
        ui.button("Browse...").clicked()
    })
    .inner
}

fn main() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("ReTerm ModMaker")
            .with_maximized(true)
            .with_inner_size([720.0, 780.0])
            .with_min_inner_size([580.0, 680.0]),
        ..Default::default()
    };
    eframe::run_native(
        "ReTerm ModMaker",
        options,
        Box::new(|cc| {
            let mut style = egui::Style::default();
            style.visuals = egui::Visuals::dark();
            style.visuals.panel_fill = BACKGROUND;
            style.visuals.window_fill = PANEL;
            cc.egui_ctx.set_style(style);
            Ok(Box::<ModMakerApp>::default())
        }),
    )
}
