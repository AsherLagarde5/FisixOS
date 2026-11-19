/* FisixOS LVGL app screens — the REAL app GUIs.
 *
 * What this is, honestly:
 *  The original apps cannot be compiled into the freestanding kernel
 *  as-is — see gui/REAL-APP-GUIS.md for the per-app table:
 *   - Browsy original GUI is Win32/GDI+ (real_browser.cpp, HWND/DWM);
 *     its engine is C++ (engine_html/css/layout/renderer).
 *   - Pathify original GUI is C# WinForms (PathifyGui.cs).
 *   - Z original is a Rust CLI compiler with no GUI at all.
 *   - ReTerm original is a Rust CMD shell; its egui tools are .mod
 *     launchers, and .mod files were removed from ReTerm.
 *  None of those run on a freestanding kernel with no Win32, no
 *  .NET, no Rust std, no libc and no process loader.
 *
 *  What these screens ARE: LVGL screens that reproduce each original
 *  app's real UI layout (Browsy's tab bar / toolbar / address pill /
 *  bookmarks bar, Pathify's dark panel + capsule buttons, ReTerm's
 *  Dosify CMD prompt, Z's Z\ prompt, Nano's editor) and drive the
 *  real Fisix-native functional core from OSC/apps/*.c — the .fse
 *  ports Asher's team already wrote (browsy.c HTML parser,
 *  reterm.c Dosify shell, pathify.c PATH manager, z.c interpreter,
 *  nano.c editor, clipong.c game). The command buttons on these
 *  screens run the same commands, with the same semantics, as those
 *  .fse programs. A "FULL <APP>" button on each screen launches the
 *  real .fse program via gui_open_app_public(), for full typing.
 *
 *  Keyboard typing inside an LVGL screen is NOT wired yet: PS/2
 *  keyboard input goes through gui.c, not LVGL, so these screens are
 *  button/tap driven. Full typing lives in the .fse program.
 *
 *  Native FisixOS apps (Terminal, Files, Calculator, Notepad, Paint,
 *  Clock, Settings, Games, Pong, Minesweeper, Snake, MS-DOS) are NOT
 *  here: they are real native modes in gui.c already, and tiles for
 *  them fall through to gui_open_app_public().
 */
#ifndef FISIX_LVGL_APPS_H
#define FISIX_LVGL_APPS_H

/* Show the LVGL screen for an app. Returns 1 if this module owns
 * the app (screen shown), 0 if the tile should fall through to the
 * old gui.c path. */
int lvgl_apps_show(const char *app);
/* Return to the Start screen from an app screen. */
void lvgl_apps_back(void);

#endif
