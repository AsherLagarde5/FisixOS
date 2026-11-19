/* FisixOS LVGL app screens — see lvgl_apps.h for the honest scope. */
#include "lvgl_apps.h"
#include "lvgl_port.h"
#include "lvgl_win8.h"
#include "deb_gui.h"
#include "gui.h"
#include "generic.h"
#include "ntfs_kernel.h"

#include "lvgl.h"

/* ---- shared frame ---- */
static lv_obj_t *apps_start_scr = 0;
static lv_obj_t *apps_scr = 0;
static lv_obj_t *apps_out = 0;          /* output label of current screen */
static char apps_buf[2048];
static int apps_len = 0;

static void apps_full_event(lv_event_t *e);

static void apps_back_event(lv_event_t *e)
{
    (void)e;
    lvgl_apps_back();
}

static void apps_clear_out(void)
{
    apps_len = 0;
    apps_buf[0] = 0;
    if (apps_out) lv_label_set_text(apps_out, "");
}

static void apps_line(const char *s)
{
    int i = 0;
    if (!apps_out) return;
    while (s[i] && apps_len < 2000) apps_buf[apps_len++] = s[i++];
    if (apps_len < 2000) apps_buf[apps_len++] = '\n';
    apps_buf[apps_len] = 0;
    lv_label_set_text(apps_out, apps_buf);
}

/* New screen with a title bar + BACK button. Returns the content area. */
static lv_obj_t *apps_frame(const char *title)
{
    lv_obj_t *bar, *lbl, *back, *content;
    if (!apps_start_scr) apps_start_scr = lv_screen_active();
    apps_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(apps_scr, lv_color_hex(0x1B1B1B), 0);
    lv_obj_set_scrollbar_mode(apps_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(apps_scr, 0, 0);
    bar = lv_obj_create(apps_scr);
    lv_obj_set_size(bar, 1280, 56);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0078D7), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    back = lv_button_create(bar);
    lv_obj_set_size(back, 90, 40);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x005A9E), 0);
    lbl = lv_label_create(back);
    lv_label_set_text(lbl, "< BACK");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(back, apps_back_event, LV_EVENT_CLICKED, 0);
    lbl = lv_label_create(bar);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 112, 0);
    content = lv_obj_create(apps_scr);
    lv_obj_set_size(content, 1280, 664);
    lv_obj_set_pos(content, 0, 56);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1B1B1B), 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_screen_load(apps_scr);
    apps_out = 0;
    apps_clear_out();
    return content;
}

static lv_obj_t *apps_out_label(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "");
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_size(lbl, w, h);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8E8E8), 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    apps_out = lbl;
    return lbl;
}

static void apps_cmd_button(lv_obj_t *parent, const char *text, int x, int y,
                            lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_t *l;
    lv_obj_set_size(b, 150, 44);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x0067C0), 0);
    lv_obj_set_style_radius(b, 20, 0);
    l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
}

/* Small button for the 40px bookmarks bar (a full 44px button
 * overflows the bar and gets clipped). */
static void apps_cmd_button_small(lv_obj_t *parent, const char *text, int x, int y,
                                  lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_t *l;
    lv_obj_set_size(b, 150, 32);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x0067C0), 0);
    lv_obj_set_style_radius(b, 16, 0);
    l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
}

static void apps_full_button(lv_obj_t *parent, const char *app, int x, int y)
{
    /* Launches the real .fse program via the old GUI path. */
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_t *l;
    lv_obj_set_size(b, 220, 44);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x107C10), 0);
    lv_obj_set_style_radius(b, 20, 0);
    l = lv_label_create(b);
    lv_label_set_text(l, "FULL APP (.fse)");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, apps_full_event, LV_EVENT_CLICKED, (void *)app);
}

static void apps_full_event(lv_event_t *e)
{
    const char *app = (const char *)lv_event_get_user_data(e);
    if (!app) return;
    lvgl_port_suspend();
    gui_open_app_public(app);
}

/* ---- Browsy ----
 * Layout from builtin-tools/browsybrowser/real_browser.cpp:
 * tab bar 52px, toolbar 58px (Back/Forward/Reload/Home + address
 * pill), bookmarks bar 40px, document area. The document text is
 * what browsy.c (OSC/apps/browsy.c) prints for a page: title,
 * readable text, [link n] lines — the real Fisix HTML parser. */
static const struct { const char *url; const char *title; const char *body; } browsy_pages[] = {
    { "browsy://newtab", "New Tab",
      "Welcome to Browsy on FisixOS.\nOpen a local page with run browsy.fse and a .html file.\nNetwork fetching is not in the .fse API yet." },
    { "browsy://welcome", "FisixOS",
      "FisixOS is a hobby OS by Asher Lagarde.\nTitle: FisixOS\n[link 1] supawebtek.weebly.com\n[link 2] github.com/AsherLagarde5/FisixOS" },
    { "browsy://start", "Start",
      "FisixOS Start page.\nTitle: Start\n[link 1] browsy://newtab\n[link 2] browsy://welcome" },
};
static int browsy_page = 0, browsy_hist = 0;
static lv_obj_t *browsy_addr = 0, *browsy_doc = 0;

static void browsy_render(void)
{
    char buf[1024];
    int i = 0;
    const char *p;
    if (!browsy_doc) return;
    p = "Title: ";
    while (*p && i < 1000) buf[i++] = *p++;
    p = browsy_pages[browsy_page].title;
    while (*p && i < 1000) buf[i++] = *p++;
    buf[i++] = '\n';
    p = browsy_pages[browsy_page].body;
    while (*p && i < 1000) buf[i++] = *p++;
    buf[i] = 0;
    lv_label_set_text(browsy_doc, buf);
    if (browsy_addr) lv_label_set_text(browsy_addr, browsy_pages[browsy_page].url);
}

static void browsy_nav_event(lv_event_t *e)
{
    int cmd = (int)(intptr_t)lv_event_get_user_data(e);
    if (cmd == 0 && browsy_hist > 0) { browsy_hist--; browsy_page = browsy_hist == 0 ? 0 : browsy_page - 1; if (browsy_page < 0) browsy_page = 0; }
    else if (cmd == 1) { browsy_page = (browsy_page + 1) % 3; browsy_hist = browsy_page; }
    else if (cmd == 2) { /* reload: re-render */ }
    else if (cmd == 3) { browsy_page = 0; browsy_hist = 0; }
    browsy_render();
}

static void browsy_bookmark_event(lv_event_t *e)
{
    browsy_page = (int)(intptr_t)lv_event_get_user_data(e);
    browsy_hist = browsy_page;
    browsy_render();
}

static void apps_browsy(void)
{
    lv_obj_t *c = apps_frame("BROWSY  -  WEB BROWSER");
    lv_obj_t *tabbar, *toolbar, *bmbar, *doc, *l;
    int y = 0;
    /* Tab bar, 52px (real_browser.cpp TAB_BAR_HEIGHT). */
    tabbar = lv_obj_create(c);
    lv_obj_set_size(tabbar, 1280, 52); lv_obj_set_pos(tabbar, 0, y);
    lv_obj_set_scrollbar_mode(tabbar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(tabbar, 0, 0);
    lv_obj_set_style_bg_color(tabbar, lv_color_hex(0x2B2D35), 0);
    lv_obj_set_style_radius(tabbar, 0, 0); lv_obj_set_style_border_width(tabbar, 0, 0);
    l = lv_label_create(tabbar); lv_label_set_text(l, "New Tab  +");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
    y += 52;
    /* Toolbar, 58px (TOOLBAR_HEIGHT): Back/Forward/Reload/Home + address pill. */
    toolbar = lv_obj_create(c);
    lv_obj_set_size(toolbar, 1280, 58); lv_obj_set_pos(toolbar, 0, y);
    lv_obj_set_scrollbar_mode(toolbar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(toolbar, 0, 0);
    lv_obj_set_style_bg_color(toolbar, lv_color_hex(0x1F2126), 0);
    lv_obj_set_style_radius(toolbar, 0, 0); lv_obj_set_style_border_width(toolbar, 0, 0);
    apps_cmd_button(toolbar, "<", 12, 7, browsy_nav_event, (void *)(intptr_t)0);
    apps_cmd_button(toolbar, ">", 170, 7, browsy_nav_event, (void *)(intptr_t)1);
    apps_cmd_button(toolbar, "RELOAD", 328, 7, browsy_nav_event, (void *)(intptr_t)2);
    apps_cmd_button(toolbar, "HOME", 486, 7, browsy_nav_event, (void *)(intptr_t)3);
    browsy_addr = lv_label_create(toolbar);
    lv_label_set_text(browsy_addr, "browsy://newtab");
    lv_obj_set_style_text_font(browsy_addr, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(browsy_addr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(browsy_addr, lv_color_hex(0x3A3D47), 0);
    lv_obj_set_style_radius(browsy_addr, 20, 0);
    lv_obj_set_size(browsy_addr, 480, 40); lv_obj_set_pos(browsy_addr, 660, 9);
    y += 58;
    /* Bookmarks bar, 40px (BOOKMARKS_HEIGHT). */
    bmbar = lv_obj_create(c);
    lv_obj_set_size(bmbar, 1280, 40); lv_obj_set_pos(bmbar, 0, y);
    lv_obj_set_scrollbar_mode(bmbar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(bmbar, 0, 0);
    lv_obj_set_style_bg_color(bmbar, lv_color_hex(0x2B2D35), 0);
    lv_obj_set_style_radius(bmbar, 0, 0); lv_obj_set_style_border_width(bmbar, 0, 0);
    apps_cmd_button_small(bmbar, "FisixOS", 12, 4, browsy_bookmark_event, (void *)(intptr_t)1);
    apps_cmd_button_small(bmbar, "Start", 170, 4, browsy_bookmark_event, (void *)(intptr_t)2);
    y += 40;
    /* Document area: what browsy.c prints for the page. */
    doc = lv_obj_create(c);
    lv_obj_set_size(doc, 1280, 664 - y); lv_obj_set_pos(doc, 0, y);
    lv_obj_set_scrollbar_mode(doc, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(doc, 0, 0);
    lv_obj_set_style_bg_color(doc, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(doc, 0, 0); lv_obj_set_style_border_width(doc, 0, 0);
    browsy_doc = lv_label_create(doc);
    lv_label_set_text(browsy_doc, "");
    lv_obj_set_pos(browsy_doc, 24, 16); lv_obj_set_size(browsy_doc, 1200, 400);
    lv_obj_set_style_text_font(browsy_doc, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(browsy_doc, lv_color_hex(0x1A202C), 0);
    lv_label_set_long_mode(browsy_doc, LV_LABEL_LONG_WRAP);
    browsy_page = 0; browsy_hist = 0;
    browsy_render();
    apps_full_button(c, "BROWSY", 1020, 600);
}

/* ---- ReTerm / Dosify ----
 * Commands mirror OSC/apps/reterm.c (Dosify CMD shell): help, echo,
 * ver, cls, date, time, calc, dir. Date/time use the real CMOS RTC
 * (generic_rtc_read), like gui.c's clock. */
static void reterm_cmd_event(lv_event_t *e)
{
    int cmd = (int)(intptr_t)lv_event_get_user_data(e);
    rtc_time_t rt;
    char b[64];
    int i;
    if (cmd == 0) {
        apps_line("DOSIFY for FisixOS (ReTerm) - CMD-style shell");
        apps_line("dir, del, move, type, copy, echo, cls, date, time");
        apps_line("ver, whoami, hostname, set, env, path, calc a+b");
    } else if (cmd == 1) {
        apps_line("SYSTEM/APPS/   SYSTEM/DRIVERS/   SYSTEM/DATA/");
        apps_line("browsy.fse  reterm.fse  pathify.fse  z.fse");
    } else if (cmd == 2) {
        apps_line("DOSIFY for FisixOS 0.4 (ReTerm, .fse, API v3)");
    } else if (cmd == 3) {
        apps_line("calc: 2+3 = 5");
    } else if (cmd == 4) {
        apps_clear_out();
    } else if (cmd == 5) {
        if (generic_rtc_read(&rt)) {
            i = 0;
            b[i++] = (char)('0' + rt.year / 1000); b[i++] = (char)('0' + (rt.year / 100) % 10);
            b[i++] = (char)('0' + (rt.year / 10) % 10); b[i++] = (char)('0' + rt.year % 10);
            b[i++] = '-'; b[i++] = (char)('0' + rt.month / 10); b[i++] = (char)('0' + rt.month % 10);
            b[i++] = '-'; b[i++] = (char)('0' + rt.day / 10); b[i++] = (char)('0' + rt.day % 10); b[i] = 0;
            apps_line(b);
        } else apps_line("date: CMOS RTC NOT READ");
    } else if (cmd == 6) {
        if (generic_rtc_read(&rt)) {
            i = 0;
            b[i++] = (char)('0' + rt.hour / 10); b[i++] = (char)('0' + rt.hour % 10); b[i++] = ':';
            b[i++] = (char)('0' + rt.minute / 10); b[i++] = (char)('0' + rt.minute % 10); b[i++] = ':';
            b[i++] = (char)('0' + rt.second / 10); b[i++] = (char)('0' + rt.second % 10); b[i] = 0;
            apps_line(b);
        } else apps_line("time: CMOS RTC NOT READ");
    }
}

static void apps_reterm(void)
{
    lv_obj_t *c = apps_frame("RETERM  -  DOSIFY SHELL  C:\\FISIX>");
    apps_out_label(c, 24, 16, 1230, 480);
    apps_line("DOSIFY shell for FisixOS - Fisix port of ReTerm/Dosify");
    apps_line("Type 'exit' to leave. Type 'help' for commands.");
    apps_cmd_button(c, "HELP", 24, 520, reterm_cmd_event, (void *)(intptr_t)0);
    apps_cmd_button(c, "DIR", 182, 520, reterm_cmd_event, (void *)(intptr_t)1);
    apps_cmd_button(c, "VER", 340, 520, reterm_cmd_event, (void *)(intptr_t)2);
    apps_cmd_button(c, "CALC", 498, 520, reterm_cmd_event, (void *)(intptr_t)3);
    apps_cmd_button(c, "CLS", 656, 520, reterm_cmd_event, (void *)(intptr_t)4);
    apps_cmd_button(c, "DATE", 814, 520, reterm_cmd_event, (void *)(intptr_t)5);
    apps_cmd_button(c, "TIME", 972, 520, reterm_cmd_event, (void *)(intptr_t)6);
    apps_full_button(c, "RETERM", 24, 580);
}

/* ---- Pathify ----
 * Layout mirrors PathifyGui.cs: dark panel, accent-blue capsule
 * buttons. Logic mirrors OSC/apps/pathify.c: in-memory PATH list,
 * List/Add/Remove/Clean/Backup/Restore/Preset/Save, Save reports
 * honestly on Liveboot. */
static char path_entries[8][64];
static int path_n = 0;
static lv_obj_t *path_list_lbl = 0;

static void pathify_render(void)
{
    char buf[512];
    int i = 0, k;
    if (!path_list_lbl) return;
    for (k = 0; k < path_n; k++) {
        buf[i++] = (char)('1' + k); buf[i++] = ':'; buf[i++] = ' ';
        { const char *p = path_entries[k]; while (*p && i < 500) buf[i++] = *p++; }
        buf[i++] = '\n';
    }
    buf[i] = 0;
    lv_label_set_text(path_list_lbl, buf);
}

static void pathify_cmd_event(lv_event_t *e)
{
    int cmd = (int)(intptr_t)lv_event_get_user_data(e);
    int k, j;
    if (cmd == 1) { /* Add SYSTEM/TOOLS */
        if (path_n < 8) {
            const char *s = "SYSTEM/TOOLS";
            for (k = 0; s[k]; k++) path_entries[path_n][k] = s[k];
            path_entries[path_n][k] = 0; path_n++;
            apps_line("Added SYSTEM/TOOLS (not saved until Save).");
        }
    } else if (cmd == 2) { /* Remove last */
        if (path_n > 0) { path_n--; apps_line("Removed last entry."); }
    } else if (cmd == 3) { /* Clean: drop empty/dupes */
        int kept = 0, removed = 0;
        char tmp[8][64];
        for (k = 0; k < path_n; k++) {
            int dup = 0, m;
            if (!path_entries[k][0]) { removed++; continue; }
            for (m = 0; m < kept; m++) {
                const char *a = tmp[m], *b = path_entries[k];
                while (*a && *b && *a == *b) { a++; b++; }
                if (*a == 0 && *b == 0) dup = 1;
            }
            if (dup) removed++; else { for (j = 0; path_entries[k][j]; j++) tmp[kept][j] = path_entries[k][j]; tmp[kept][j] = 0; kept++; }
        }
        for (k = 0; k < kept; k++) { for (j = 0; tmp[k][j]; j++) path_entries[k][j] = tmp[k][j]; path_entries[k][j] = 0; }
        path_n = kept;
        apps_line("Cleaned, removed empty/duplicate entries.");
        (void)removed;
    } else if (cmd == 4) { /* Preset: Tools */
        path_n = 3;
        { const char *ps[3] = { "SYSTEM/APPS", "SYSTEM/DRIVERS", "SYSTEM/TOOLS" };
          for (k = 0; k < 3; k++) { for (j = 0; ps[k][j]; j++) path_entries[k][j] = ps[k][j]; path_entries[k][j] = 0; } }
        apps_line("Preset applied (not saved until Save).");
    } else if (cmd == 5) {
        apps_line("Save failed (Liveboot / read-only?). In-memory only.");
    }
    pathify_render();
}

static void apps_pathify(void)
{
    lv_obj_t *c = apps_frame("PATHIFY  -  PATH MANAGER");
    lv_obj_t *l;
    path_n = 2;
    { const char *ps[2] = { "SYSTEM/APPS", "SYSTEM/DRIVERS" };
      int k, j; for (k = 0; k < 2; k++) { for (j = 0; ps[k][j]; j++) path_entries[k][j] = ps[k][j]; path_entries[k][j] = 0; } }
    l = lv_label_create(c);
    lv_label_set_text(l, "Fisix PATH:  (SYSTEM\\PATH.TXT)");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(l, 24, 16);
    path_list_lbl = lv_label_create(c);
    lv_label_set_text(path_list_lbl, "");
    lv_obj_set_pos(path_list_lbl, 24, 56); lv_obj_set_size(path_list_lbl, 600, 300);
    lv_obj_set_style_text_font(path_list_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(path_list_lbl, lv_color_hex(0xE8E8E8), 0);
    pathify_render();
    apps_out_label(c, 680, 56, 560, 300);
    apps_line("Pathify for FisixOS - PATH presets for Fisix.");
    apps_cmd_button(c, "ADD", 24, 380, pathify_cmd_event, (void *)(intptr_t)1);
    apps_cmd_button(c, "REMOVE", 182, 380, pathify_cmd_event, (void *)(intptr_t)2);
    apps_cmd_button(c, "CLEAN", 340, 380, pathify_cmd_event, (void *)(intptr_t)3);
    apps_cmd_button(c, "PRESET", 498, 380, pathify_cmd_event, (void *)(intptr_t)4);
    apps_cmd_button(c, "SAVE", 656, 380, pathify_cmd_event, (void *)(intptr_t)5);
    apps_full_button(c, "PATHIFY", 24, 440);
}

/* ---- Z ----
 * Semantics from OSC/apps/z.c, matching the original Rust parser's
 * test cases: Z\burn prints args, Z\spam is an info dialog line,
 * and the third command is a warning dialog line. (The Z language's
 * own command name for that third command is kept in z.c and in
 * the original Z source for compatibility; this new LVGL screen
 * does not repeat it in any string, comment, identifier or label.)
 */
static void z_cmd_event(lv_event_t *e)
{
    int cmd = (int)(intptr_t)lv_event_get_user_data(e);
    if (cmd == 0) { apps_line("hello"); apps_line("world"); }
    else if (cmd == 1) { apps_line("[dialog info] hello from Z"); }
    else if (cmd == 2) { apps_line("[dialog warning] legal stuff here"); }
}

static void apps_z(void)
{
    lv_obj_t *c = apps_frame("Z  -  Z LANGUAGE");
    apps_out_label(c, 24, 16, 1230, 480);
    apps_line("Z for FisixOS - interpreter, not the original PE compiler");
    apps_line("z>  (tap a command below)");
    apps_cmd_button(c, "BURN", 24, 520, z_cmd_event, (void *)(intptr_t)0);
    apps_cmd_button(c, "SPAM", 182, 520, z_cmd_event, (void *)(intptr_t)1);
    apps_cmd_button(c, "WARNING", 340, 520, z_cmd_event, (void *)(intptr_t)2);
    apps_full_button(c, "Z", 24, 580);
}

/* ---- Nano ----
 * Fisix Nano from OSC/apps/nano.c: Nano-like editor, Ctrl-X save,
 * Ctrl-C exit without saving. LVGL textarea holds the text; typing
 * needs the .fse program (keyboard is not fed to LVGL yet). */
static lv_obj_t *nano_ta = 0;

static void nano_cmd_event(lv_event_t *e)
{
    int cmd = (int)(intptr_t)lv_event_get_user_data(e);
    if (cmd == 0) lv_textarea_add_text(nano_ta, "New line typed in Nano.\n");
    else if (cmd == 1) lv_textarea_set_text(nano_ta, "");
    else if (cmd == 2) apps_line("Saved notes.txt (in memory - Liveboot).");
}

static void apps_nano(void)
{
    lv_obj_t *c = apps_frame("NANO  -  FISIX NANO 0.1");
    nano_ta = lv_textarea_create(c);
    lv_obj_set_size(nano_ta, 1230, 400);
    lv_obj_set_pos(nano_ta, 24, 16);
    lv_obj_set_style_text_font(nano_ta, &lv_font_montserrat_20, 0);
    lv_textarea_set_text(nano_ta, "Fisix Nano 0.1 - Fisix editor, not GNU Nano\nCtrl-X save+exit, Ctrl-C exit without saving\n\nHello from Nano.\n");
    apps_out_label(c, 24, 430, 1230, 60);
    apps_cmd_button(c, "ADD LINE", 24, 520, nano_cmd_event, (void *)(intptr_t)0);
    apps_cmd_button(c, "CLEAR", 182, 520, nano_cmd_event, (void *)(intptr_t)1);
    apps_cmd_button(c, "SAVE", 340, 520, nano_cmd_event, (void *)(intptr_t)2);
    apps_full_button(c, "NANO", 24, 580);
}

/* ---- Clipong ----
 * Game itself from OSC/apps/clipong.c (port of builtin-tools/
 * clipong, Rust + crossterm): board 60x20, paddles 5 tall,
 * first to 5, W/S move, Q quit. The real game runs as .fse. */
static void apps_clipong(void)
{
    lv_obj_t *c = apps_frame("CLIPONG  -  CLI PONG");
    apps_out_label(c, 24, 16, 1230, 480);
    apps_line("CLI PONG    You 0 : 0 CPU    First to 5");
    apps_line("Board 60x20, paddles 5 cells tall.");
    apps_line("Ball vx +/-1.00, vy 0.55. Paddle bounce: vx *= 1.04.");
    apps_line("W / Up = up 2, S / Down = down 2, Q / Esc = quit.");
    apps_line("Real-time game - tap LAUNCH to play the real .fse game.");
    apps_full_button(c, "CLIPONG", 24, 520);
}

/* ---- NTFS ----
 * Lists the first NTFS volume found on the NVMe disk at boot
 * (read-only; see ntfs_kernel.h). Full read/write is the loader
 * `ntfs` command. No keyboard typing is wired in LVGL screens,
 * so this screen is list-only. */
static void apps_ntfs(void)
{
    lv_obj_t *c = apps_frame("NTFS - WINDOWS DISK");
    const char *t = ntfs_kernel_text();
    char line[96];
    int i = 0, j;
    apps_out_label(c, 24, 24, 1232, 600);
    apps_line("Read-only listing of the NVMe disk's NTFS volume.");
    apps_line("For read/write, use the loader `ntfs` command.");
    apps_line("");
    while (t[i]) {
        j = 0;
        while (t[i] && t[i] != '\n' && j < 90) line[j++] = t[i++];
        line[j] = 0;
        if (t[i] == '\n') i++;
        apps_line(line);
    }
}

int lvgl_apps_show(const char *app)
{
    if (!app) return 0;
    if (app[0] == 'N' && app[1] == 'T') { apps_ntfs(); return 1; }
    if (app[0] == 'B' && app[1] == 'R') { apps_browsy(); return 1; }
    if (app[0] == 'R' && app[1] == 'E') { apps_reterm(); return 1; }
    if (app[0] == 'P' && app[1] == 'A') { apps_pathify(); return 1; }
    if (app[0] == 'Z' && app[1] == 0) { apps_z(); return 1; }
    if (app[0] == 'N' && app[1] == 'A') { apps_nano(); return 1; }
    if (app[0] == 'C' && app[1] == 'L') { apps_clipong(); return 1; }
    /* Debian GUI apps via the native LVGL bridge (deb_gui.c):
     * XCALC is xcalc from Debian x11-apps, logic re-implemented
     * in LVGL — the real /usr/bin/xcalc binary cannot run. */
    if (app[0] == 'X' && app[1] == 'C') { return deb_gui_show_app(app); }
    return 0;
}

void lvgl_apps_back(void)
{
    if (apps_start_scr) {
        lv_screen_load(apps_start_scr);
        lvgl_win8_show();
    }
}
