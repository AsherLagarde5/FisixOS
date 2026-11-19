/* FisixOS Win8-style Start screen — see lvgl_win8.h. */
#include "lvgl_win8.h"
#include "lvgl_port.h"
#include "lvgl_apps.h"
#include "icons_generated.h"
#include "gui.h"

#include "lvgl.h"

static lv_obj_t *win8_toast = 0;
static lv_obj_t *win8_tv = 0;
static lv_obj_t *win8_live_bar = 0;

static void win8_toast_hide(lv_timer_t *t)
{
    (void)t;
    if (win8_toast) lv_obj_add_flag(win8_toast, LV_OBJ_FLAG_HIDDEN);
}

static void win8_toast_show(const char *name)
{
    char buf[64];
    int i = 0;
    if (!win8_toast) return;
    /* "OPENING <name>" — no libc snprintf in the kernel. */
    const char *p = "OPENING ";
    while (*p && i < 60) buf[i++] = *p++;
    p = name;
    while (*p && i < 60) buf[i++] = *p++;
    buf[i] = 0;
    lv_label_set_text(win8_toast, buf);
    lv_obj_clear_flag(win8_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(win8_toast_hide, 2000, 0);
}

static void win8_live_bar_anim(void *bar, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)bar, v, LV_ANIM_OFF);
}

static void win8_tile_event(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name) return;
    win8_toast_show(name);
    /* LVGL app screen first (Browsy/ReTerm/Pathify/Z/Nano/Clipong):
     * it owns the screen and returns 1. Otherwise hand the app to
     * the existing FisixOS GUI and stop LVGL drawing. */
    if (lvgl_apps_show(name)) return;
    lvgl_port_suspend();
    gui_open_app_public(name);
}

static lv_obj_t *win8_add_tile(lv_obj_t *parent, const char *display, const char *app,
                               const char *sub, uint32_t color,
                               int w, int h, int wide)
{
    lv_obj_t *btn, *lbl, *slbl;
    btn = lv_button_create(parent);
    lv_obj_set_size(btn, wide ? w * 2 + 12 : w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    /* Event user data is the app name, not the display label. */
    lv_obj_add_event_cb(btn, win8_tile_event, LV_EVENT_CLICKED, (void *)app);
    /* Icon from gui/icons.json -> .ico -> icons_generated.c (LVGL
     * cannot read .ico directly). White glyph on transparent,
     * 64x64, top area of the tile; sub-label above, name below. */
    {
        const lv_image_dsc_t *ic = fisix_icon_get(app);
        if (ic) {
            lv_obj_t *img = lv_image_create(btn);
            lv_image_set_src(img, ic);
            lv_obj_set_size(img, 64, 64);
            lv_obj_align(img, LV_ALIGN_TOP_LEFT, 12, 34);
        }
    }
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, display);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 12, -10);
    if (sub) {
        slbl = lv_label_create(btn);
        lv_label_set_text(slbl, sub);
        lv_obj_set_style_text_font(slbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(slbl, lv_color_hex(0xD0E8FF), 0);
        lv_obj_align(slbl, LV_ALIGN_TOP_LEFT, 12, 10);
    }
    return btn;
}

void lvgl_win8_init(void)
{
    lv_obj_t *scr, *tile0, *tile1, *strip, *lbl, *bar;
    lv_anim_t a;
    int32_t sw, sh, ts, gap;
    /* display = short label that fits a 150px tile at font 20;
     * app = name handed to gui_open_app_public(). */
    static const struct { const char *display; const char *app; const char *sub; uint32_t color; int wide; } tiles[] = {
        { "TERMINAL", "TERMINAL",   "FISIX SHELL",  0x0078D7, 1 },
        { "FILES",    "FILES",      "DISK",         0x107C10, 0 },
        { "NTFS",     "NTFS",       "WINDOWS DISK", 0x0078D7, 0 },
        { "NANO",     "NANO",       "EDITOR",       0x5C2D91, 0 },
        { "CALC",     "CALCULATOR", "MATH",         0xD83B01, 0 },
        { "GAMES",    "GAMES",      "PONG + MORE",  0x008272, 0 },
        { "PONG",     "PONG",       "ARCADE",       0x038387, 0 },
        { "MINES",    "MINESWEEPER","CLASSIC",      0xCA5010, 0 },
        { "SNAKE",    "SNAKE",      "ARCADE",       0x107C10, 0 },
        { "BROWSY",   "BROWSY",     "WEB",          0x0078D7, 0 },
        { "RETERM",   "RETERM",     "TERMINAL",     0x5C2D91, 0 },
        { "Z",        "Z",          "TOOL",         0x038387, 0 },
        { "PATHIFY",  "PATHIFY",    "FILES",        0xD83B01, 0 },
        { "SETTINGS", "SETTINGS",   "SYSTEM",       0x69797E, 0 },
        { "POWER",    "SHUTDOWN",   "SHUTDOWN",        0xE81123, 0 },
    };
    int i;
    scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B5A8C), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    sw = lv_display_get_horizontal_resolution(0);
    sh = lv_display_get_vertical_resolution(0);
    ts = sw < 1000 ? 120 : (sw < 1600 ? 150 : 180);
    gap = 12;

    win8_tv = lv_tileview_create(scr);
    lv_obj_set_size(win8_tv, sw, sh - 48);
    lv_obj_set_pos(win8_tv, 0, 0);
    lv_obj_set_scrollbar_mode(win8_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(win8_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(win8_tv, 0, 0);
    lv_obj_set_style_pad_all(win8_tv, 0, 0);

    /* Page 0: the Start tiles. */
    tile0 = lv_tileview_add_tile(win8_tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_set_flex_flow(tile0, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tile0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(tile0, 24, 0);
    lv_obj_set_style_pad_gap(tile0, gap, 0);
    lbl = lv_label_create(tile0);
    lv_label_set_text(lbl, "FISIXOS  -  START");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(lbl, sw - 48);
    for (i = 0; i < (int)(sizeof(tiles) / sizeof(tiles[0])); i++) {
        win8_add_tile(tile0, tiles[i].display, tiles[i].app, tiles[i].sub, tiles[i].color, ts, ts, tiles[i].wide);
    }
    /* Live bar on the Terminal tile area: animates 0..100 forever. */
    bar = lv_bar_create(tile0);
    lv_obj_set_size(bar, ts * 2 + gap, 18);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0B5A8C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00A4EF), LV_PART_INDICATOR);
    win8_live_bar = bar;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, win8_live_bar_anim);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 2000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, 400);
    lv_anim_start(&a);

    /* Page 1: swipe right for the rest (Win8-style second page). */
    tile1 = lv_tileview_add_tile(win8_tv, 1, 0, LV_DIR_LEFT);
    lv_obj_set_flex_flow(tile1, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tile1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(tile1, 24, 0);
    lv_obj_set_style_pad_gap(tile1, gap, 0);
    lbl = lv_label_create(tile1);
    lv_label_set_text(lbl, "ALL APPS  -  SWIPE LEFT TO GO BACK");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(lbl, sw - 48);
    win8_add_tile(tile1, "CLIPONG", "CLIPONG", "CLIPBOARD", 0x008272, ts, ts, 0);
    /* Debian GUI app, bridged natively (deb_gui.c): xcalc from
     * Debian x11-apps 7.7+9. No icon file for it yet; the tile
     * renders without one, like any icon-less tile. */
    win8_add_tile(tile1, "XCALC", "XCALC", "DEBIAN .DEB", 0xD83B01, ts, ts, 0);
    win8_add_tile(tile1, "NOTEPAD", "NOTEPAD", "NOTES", 0x5C2D91, ts, ts, 0);
    win8_add_tile(tile1, "CLOCK", "CLOCK", "TIME", 0x038387, ts, ts, 0);
    win8_add_tile(tile1, "MS-DOS", "MS-DOS", "DOS", 0x107C10, ts, ts, 0);
    win8_add_tile(tile1, "PAINT", "PAINT", "DRAW", 0xD83B01, ts, ts, 0);
    win8_add_tile(tile1, "ALL APPS", "ALL APPS", "55 APPS", 0x69797E, ts, ts, 0);

    /* Bottom status strip. */
    strip = lv_obj_create(scr);
    lv_obj_set_size(strip, sw, 48);
    lv_obj_set_pos(strip, 0, sh - 48);
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x0A2E4A), 0);
    lv_obj_set_style_radius(strip, 0, 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_pad_all(strip, 0, 0);
    lbl = lv_label_create(strip);
    lv_label_set_text(lbl, "FISIXOS");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);
    lbl = lv_label_create(strip);
    lv_label_set_text(lbl, "START  -  TILES: CLICK TO OPEN  -  ESC: BACK");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xBFE3FF), 0);
    lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -16, 0);

    /* Toast, hidden until a tile is pressed. */
    win8_toast = lv_label_create(scr);
    lv_label_set_text(win8_toast, "");
    lv_obj_set_style_text_font(win8_toast, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(win8_toast, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(win8_toast, lv_color_hex(0x0A2E4A), 0);
    lv_obj_set_style_bg_opa(win8_toast, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(win8_toast, 12, 0);
    lv_obj_align(win8_toast, LV_ALIGN_BOTTOM_MID, 0, -64);
    lv_obj_add_flag(win8_toast, LV_OBJ_FLAG_HIDDEN);
}

void lvgl_win8_show(void)
{
    lv_obj_invalidate(lv_screen_active());
    lv_timer_handler();
}

void lvgl_win8_resume(void)
{
    lvgl_port_resume();
    lvgl_win8_show();
}
