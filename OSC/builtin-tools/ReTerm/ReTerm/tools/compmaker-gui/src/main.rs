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
struct MakerApp {
    directory: String,
    main_file: String,
    name: String,
    callby: String,
    version: String,
    description: String,
    message: Option<(bool, String)>,
}

impl MakerApp {
    fn pick_directory(&mut self) {
        if let Some(path) = rfd::FileDialog::new().pick_folder() {
            self.directory = path.to_string_lossy().into_owned();
            if !self.main_file.is_empty() && !PathBuf::from(&self.main_file).starts_with(&path) {
                self.main_file.clear();
            }
        }
    }

    fn pick_main_file(&mut self) {
        let mut dialog = rfd::FileDialog::new()
            .add_filter("Component entrypoint", &["exe", "bat", "cmd", "vbs"]);
        if !self.directory.is_empty() {
            dialog = dialog.set_directory(&self.directory);
        }
        if let Some(path) = dialog.pick_file() {
            if self.directory.is_empty() {
                if let Some(parent) = path.parent() {
                    self.directory = parent.to_string_lossy().into_owned();
                }
            }
            self.main_file = path.to_string_lossy().into_owned();
        }
    }

    fn create_component(&mut self) {
        let result = maker_core::create_from_fields(
            &PathBuf::from(&self.directory),
            &PathBuf::from(&self.main_file),
            self.name.clone(),
            self.callby.clone(),
            self.version.clone(),
            self.description.clone(),
        );
        self.message = Some(match result {
            Ok(path) => (true, format!("Created {}", path.display())),
            Err(error) => (false, error.to_string()),
        });
    }
}

impl eframe::App for MakerApp {
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
                        RichText::new("ReTerm Component Maker")
                            .size(24.0)
                            .strong()
                            .color(TEXT),
                    );
                    ui.add_space(4.0);
                    ui.label(
                        RichText::new("Package a program as an encrypted .comp component.")
                            .color(MUTED),
                    );
                    ui.add_space(22.0);

                    egui::Frame::new()
                        .fill(PANEL)
                        .stroke(Stroke::new(1.0_f32, BORDER))
                        .corner_radius(10)
                        .inner_margin(egui::Margin::same(18))
                        .show(ui, |ui| {
                            let browse_directory =
                                path_row(ui, "Component folder", &mut self.directory);
                            if browse_directory {
                                self.pick_directory();
                            }
                            ui.add_space(10.0);
                            let browse_main_file = path_row(ui, "Main file", &mut self.main_file);
                            if browse_main_file {
                                self.pick_main_file();
                            }
                        });
                    ui.add_space(14.0);

                    egui::Frame::new()
                        .fill(PANEL)
                        .stroke(Stroke::new(1.0_f32, BORDER))
                        .corner_radius(10)
                        .inner_margin(egui::Margin::same(18))
                        .show(ui, |ui| {
                            ui.label(RichText::new("Component details").strong().color(TEXT));
                            ui.add_space(8.0);
                            ui.label(RichText::new("Name").color(MUTED));
                            ui.text_edit_singleline(&mut self.name);
                            ui.add_space(8.0);
                            ui.label(RichText::new("Command name (callby)").color(MUTED));
                            ui.text_edit_singleline(&mut self.callby);
                            ui.add_space(8.0);
                            ui.label(RichText::new("Version").color(MUTED));
                            ui.text_edit_singleline(&mut self.version);
                            ui.add_space(8.0);
                            ui.label(RichText::new("Description").color(MUTED));
                            ui.add(
                                egui::TextEdit::multiline(&mut self.description)
                                    .desired_rows(3)
                                    .desired_width(f32::INFINITY),
                            );
                        });
                    ui.add_space(18.0);

                    let ready = !self.directory.trim().is_empty()
                        && !self.main_file.trim().is_empty()
                        && !self.name.trim().is_empty()
                        && !self.callby.trim().is_empty()
                        && !self.version.trim().is_empty()
                        && !self.description.trim().is_empty();
                    if ui
                        .add_enabled(
                            ready,
                            egui::Button::new(
                                RichText::new("Create .comp")
                                    .strong()
                                    .color(Color32::from_rgb(12, 25, 25)),
                            )
                            .fill(ACCENT)
                            .min_size(Vec2::new(150.0, 40.0)),
                        )
                        .clicked()
                    {
                        self.create_component();
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
            .with_title("ReTerm Component Maker")
            .with_maximized(true)
            .with_inner_size([700.0, 760.0])
            .with_min_inner_size([580.0, 660.0]),
        ..Default::default()
    };
    eframe::run_native(
        "ReTerm Component Maker",
        options,
        Box::new(|cc| {
            let mut style = egui::Style::default();
            style.visuals = egui::Visuals::dark();
            style.visuals.panel_fill = BACKGROUND;
            style.visuals.window_fill = PANEL;
            cc.egui_ctx.set_style(style);
            Ok(Box::<MakerApp>::default())
        }),
    )
}
