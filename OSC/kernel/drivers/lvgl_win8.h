/* FisixOS Win8-style Start screen, built on LVGL.
 *
 * Full-screen tile view (Windows 8 Start screen): coloured live
 * tiles in a wrapping grid, a second tile page for the rest of the
 * apps, a status strip at the bottom, press animation (the LVGL
 * default theme grows a pressed button), and a live bar on the
 * Terminal tile.
 *
 * Wired vs stubbed (honest):
 *  - WIRED: a tile press shows a toast and calls
 *    gui_open_app_public(name), the existing FisixOS app path
 *    (Terminal/Files/Nano/Calculator/Games/Pong/Minesweeper/Snake
 *    and the .fse apps Browsy/ReTerm/Nano/Pathify/Clipong/Z).
 *    Opening an app suspends LVGL; closing the old-GUI window
 *    (X button or Esc) resumes the Start screen.
 *  - STUBBED: Shutdown only opens the generic old-GUI window for
 *    the name (there is no ACPI S5 call from a tile yet); the
 *    "live" content on tiles is an animating bar, not live app
 *    data; there is no Start-screen search, no user picture, and
 *    no second-monitor support.
 */
#ifndef FISIX_LVGL_WIN8_H
#define FISIX_LVGL_WIN8_H

void lvgl_win8_init(void);
void lvgl_win8_show(void);   /* (re)build + force a full redraw */
void lvgl_win8_resume(void); /* called when an old-GUI app window closes */

#endif
