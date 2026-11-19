/* deb_gui.h - Debian GUI app bridge for FisixOS.
 *
 * Path chosen: (a) run the app's logic natively and render through
 * LVGL. See docs/DEB_GUI.md for the full reasoning; the short
 * version: option (b), an X11/Wayland-compatible shim that runs
 * the real Debian binary, is not achievable on FisixOS today.
 * The real binaries are dynamic Linux ELFs that need
 * /lib64/ld-linux-x86-64.so.2, glibc, libX11/libXt/libXaw and a
 * running X server, and FisixOS has none of those: no ring-3
 * jump, no dynamic linker, no Linux syscalls, no X server.
 * deb_run() already refuses those binaries with the exact reason
 * (see docs/DEB.md). What this module does instead: for a small
 * set of registered Debian GUI apps, it re-implements the app's
 * UI (as LVGL widgets, in the same style as lvgl_apps.c) and its
 * logic (native C, freestanding, no libc, no floating point)
 * and shows that screen. The .deb still matters: it is installed
 * with the real deb.c installer, the binary is staged and
 * classified, and deb_run() hands a registered app to this
 * bridge instead of refusing it.
 *
 * First bridged app: xcalc from Debian package x11-apps 7.7+9
 * (bookworm, amd64), binary /usr/bin/xcalc — a TI-30-style
 * scientific calculator accessory. Only its basic arithmetic
 * UI is bridged (digits, decimal point, + - * /, =, C, +/-);
 * its scientific keys (sqrt, trig, log, RPN mode) are not.
 *
 * No profanity anywhere in this file: FisixOS is the
 * professional-vibe project.
 */
#ifndef FISIX_DEB_GUI_H
#define FISIX_DEB_GUI_H
#include <stdint.h>

/* 1 if this package/binary has a native LVGL bridge in this
 * module. pkg is the Debian package name (e.g. "x11-apps");
 * path is the installed path as in the .deb, with or without a
 * leading "./" or "/" (e.g. "./usr/bin/xcalc"). */
int deb_gui_is_bridged(const char *pkg, const char *path);

/* Show the bridged LVGL screen for a registered package/binary.
 * Returns 1 if a screen was shown, 0 if not bridged (caller
 * should fall through to the normal deb_run() refusal). */
int deb_gui_show(const char *pkg, const char *path);

/* Show a bridged app by its Start-screen name (e.g. "XCALC").
 * Used by lvgl_apps_show() for the tile. Returns 1 if shown. */
int deb_gui_show_app(const char *app);

/* Feed one key to the currently shown bridged app, as if the
 * user had tapped the matching button: '0'-'9', '.', '+', '-',
 * '*', 'x', '/', '=', '\n' (equals), 'c'/'C' (clear).
 * Returns 1 if a bridged screen consumed the key, 0 otherwise.
 * The kernel PS/2 path does not feed LVGL yet (see
 * lvgl_apps.h), so today this is driven by the host test and
 * is the hook the kernel keyboard path should call. */
int deb_gui_feed_key(char c);

/* Copy the bridged app's current display text (e.g. the xcalc
 * readout) into out, NUL-terminated. Used by the host test to
 * check results without reading pixels. */
void deb_gui_display_text(char *out, uint32_t cap);

#endif
