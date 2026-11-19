/* deb_gui.c - Debian GUI app bridge for FisixOS. See deb_gui.h
 * and docs/DEB_GUI.md for the scope: option (a), native logic
 * rendered through LVGL. No X server is faked here.
 *
 * First app: xcalc (Debian x11-apps 7.7+9, /usr/bin/xcalc).
 * The real xcalc is a TI-30-style calculator; this bridge
 * re-implements its basic arithmetic UI and logic, not its
 * scientific keys and not its RPN mode.
 *
 * Calculator arithmetic is fixed-point, scale 1,000,000, in
 * int64_t only — no floating point (the freestanding kernel
 * has no libm / FP runtime to rely on) and no libc (no
 * snprintf; numbers are formatted by hand, like the rest of
 * the kernel). Limits, stated honestly on the screen and in
 * docs/DEB_GUI.md: entry is at most 12 digits, results are
 * capped at integer part 1,000,000 for * and / (wider for
 * + and -), 6 fractional digits, division by zero shows
 * "Error". Those limits keep every intermediate inside
 * int64_t without a 128-bit multiply the kernel cannot link.
 */
#include "deb_gui.h"
#include "lvgl_port.h"
#include "lvgl_win8.h"

#include "lvgl.h"

/* ---------- bridge registry ---------- */
typedef struct {
    const char *pkg;    /* Debian package name */
    const char *path;   /* installed path, no leading ./ or / */
    const char *app;    /* Start-screen name */
} deb_gui_entry_t;

static const deb_gui_entry_t deb_gui_table[] = {
    { "x11-apps", "usr/bin/xcalc", "XCALC" },
};
#define DEB_GUI_COUNT (int)(sizeof(deb_gui_table) / sizeof(deb_gui_table[0]))

/* Compare a .deb path against a registry path, ignoring a
 * leading "./" or "/" on the .deb side. */
static int deb_gui_path_eq(const char *path, const char *want)
{
    if (!path || !want) return 0;
    if (path[0] == '.' && path[1] == '/') path += 2;
    else if (path[0] == '/') path += 1;
    while (*path && *want && *path == *want) { path++; want++; }
    return *path == 0 && *want == 0;
}

static const deb_gui_entry_t *deb_gui_lookup(const char *pkg, const char *path)
{
    int i;
    if (!pkg || !path) return 0;
    for (i = 0; i < DEB_GUI_COUNT; i++)
        if (deb_gui_table[i].pkg[0] == pkg[0]) {
            const char *a = deb_gui_table[i].pkg, *b = pkg;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a == 0 && *b == 0 && deb_gui_path_eq(path, deb_gui_table[i].path))
                return &deb_gui_table[i];
        }
    return 0;
}

int deb_gui_is_bridged(const char *pkg, const char *path)
{
    return deb_gui_lookup(pkg, path) != 0;
}

/* ---------- xcalc calculator core (fixed point, scale 1e6) ---------- */
#define XCALC_SCALE 1000000LL
#define XCALC_MAX_INT 1000000LL   /* integer-part cap for * and / */
#define XCALC_MAX_DIGITS 12

static int64_t xcalc_acc = 0;       /* scaled accumulator */
static int64_t xcalc_digits = 0;    /* digits typed, unscaled */
static int xcalc_frac = -1;         /* -1: no dot yet, else frac digits typed */
static int xcalc_neg = 0;           /* entry is negative */
static int xcalc_typing = 0;        /* entering a number right now */
static int xcalc_pending = 0;       /* 0, '+', '-', '*', '/' */
static int xcalc_error = 0;
static int xcalc_result = 0;        /* last key was = : next digit starts fresh */
static lv_obj_t *xcalc_disp = 0;    /* display label */
static lv_obj_t *xcalc_start_scr = 0;
static lv_obj_t *xcalc_scr = 0;
static char xcalc_text[64];

static int64_t xcalc_abs64(int64_t v) { return v < 0 ? -v : v; }

static int64_t xcalc_pow10(int n)
{
    int64_t r = 1;
    while (n-- > 0) r *= 10;
    return r;
}

/* Scaled value of the number currently being typed. */
static int64_t xcalc_entry_value(void)
{
    int64_t v;
    if (xcalc_frac < 0) v = xcalc_digits * XCALC_SCALE;
    else v = xcalc_digits * XCALC_SCALE / xcalc_pow10(xcalc_frac);
    return xcalc_neg ? -v : v;
}

static void xcalc_clear(void)
{
    xcalc_acc = 0; xcalc_digits = 0; xcalc_frac = -1; xcalc_neg = 0;
    xcalc_typing = 0; xcalc_pending = 0; xcalc_error = 0; xcalc_result = 0;
}

/* Apply op to a and b (both scaled). Returns 1 and sets *out,
 * or 0 on overflow / divide-by-zero (caller shows Error). */
static int xcalc_apply(int64_t a, int op, int64_t b, int64_t *out)
{
    if (op == '+') {
        if (xcalc_abs64(a) > 4000000000000000000LL || xcalc_abs64(b) > 4000000000000000000LL) return 0;
        *out = a + b;
        return 1;
    }
    if (op == '-') {
        if (xcalc_abs64(a) > 4000000000000000000LL || xcalc_abs64(b) > 4000000000000000000LL) return 0;
        *out = a - b;
        return 1;
    }
    if (op == '*') {
        /* (A*S+af)*(B*S+bf)/S = A*B*S + A*bf + B*af + af*bf/S.
         * With |A|,|B| <= 1e6 every term fits in int64_t. */
        int64_t ai = a / XCALC_SCALE, bi = b / XCALC_SCALE;
        int64_t af = a % XCALC_SCALE, bf = b % XCALC_SCALE;
        int64_t t;
        if (xcalc_abs64(ai) > XCALC_MAX_INT || xcalc_abs64(bi) > XCALC_MAX_INT) return 0;
        t = ai * bi * XCALC_SCALE + ai * bf + bi * af + (af * bf) / XCALC_SCALE;
        *out = t;
        return 1;
    }
    if (op == '/') {
        int64_t q, r, frac = 0, babs, rabs;
        int k, sign;
        if (b == 0) return 0;
        q = a / b; /* integer part of the quotient (both scaled) */
        if (xcalc_abs64(q) > XCALC_MAX_INT) return 0;
        r = a % b;
        sign = (r < 0) != (b < 0) ? -1 : 1;
        babs = xcalc_abs64(b);
        rabs = xcalc_abs64(r);
        /* Long division for 6 fractional digits. If the divisor
         * is huge, scale r and b down by 1000 first so r*10
         * cannot overflow int64_t (loses low digits only). */
        if (babs > 1000000000000000LL) { rabs /= 1000; babs /= 1000; }
        for (k = 0; k < 6; k++) {
            int64_t d;
            rabs *= 10;
            d = babs ? rabs / babs : 0;
            frac = frac * 10 + d;
            rabs %= babs;
        }
        *out = q * XCALC_SCALE + (int64_t)sign * frac;
        return 1;
    }
    return 0;
}

/* Format a scaled value into xcalc_text. If typing, the entry
 * (digits + dot state) is formatted instead, so a trailing
 * "12." shows while the user is still typing the fraction. */
static void xcalc_format(void)
{
    char *o = xcalc_text;
    int64_t v, ip, fp;
    int i, ndig;
    char rev[24];
    if (xcalc_error) {
        const char *s = "Error";
        while (*s) *o++ = *s++;
        *o = 0;
        return;
    }
    if (xcalc_typing) {
        int64_t d = xcalc_digits;
        if (xcalc_neg) *o++ = '-';
        if (d == 0) { *o++ = '0'; }
        else {
            ndig = 0;
            while (d > 0) { rev[ndig++] = (char)('0' + d % 10); d /= 10; }
            for (i = ndig - 1; i >= 0; i--) *o++ = rev[i];
        }
        if (xcalc_frac >= 0) {
            int f;
            *o++ = '.';
            /* Digits already typed after the dot: the low
             * xcalc_frac digits of xcalc_digits, zero-padded. */
            for (f = xcalc_frac - 1; f >= 0; f--) {
                int64_t p = xcalc_pow10(f);
                *o++ = (char)('0' + (xcalc_digits / p) % 10);
            }
        }
        *o = 0;
        return;
    }
    v = xcalc_acc;
    if (v < 0) { *o++ = '-'; v = -v; }
    ip = v / XCALC_SCALE;
    fp = v % XCALC_SCALE;
    if (ip == 0) { *o++ = '0'; }
    else {
        ndig = 0;
        while (ip > 0) { rev[ndig++] = (char)('0' + ip % 10); ip /= 10; }
        for (i = ndig - 1; i >= 0; i--) *o++ = rev[i];
    }
    if (fp) {
        int f, last = 5;
        char fdig[6];
        for (f = 0; f < 6; f++) { fdig[5 - f] = (char)('0' + fp % 10); fp /= 10; }
        while (last >= 0 && fdig[last] == '0') last--;
        *o++ = '.';
        for (f = 0; f <= last; f++) *o++ = fdig[f];
    }
    *o = 0;
}

static void xcalc_refresh(void)
{
    xcalc_format();
    if (xcalc_disp) lv_label_set_text(xcalc_disp, xcalc_text);
}

void deb_gui_display_text(char *out, uint32_t cap)
{
    uint32_t i = 0;
    xcalc_format();
    if (!out || !cap) return;
    while (xcalc_text[i] && i + 1 < cap) { out[i] = xcalc_text[i]; i++; }
    out[i] = 0;
}

/* Commit the typed entry into the accumulator through any
 * pending operator, then clear the entry. */
static void xcalc_commit(void)
{
    int64_t cur, res;
    if (!xcalc_typing) return;
    cur = xcalc_entry_value();
    if (xcalc_pending) {
        if (!xcalc_apply(xcalc_acc, xcalc_pending, cur, &res)) {
            xcalc_error = 1; xcalc_typing = 0; xcalc_pending = 0;
            return;
        }
        xcalc_acc = res;
    } else {
        xcalc_acc = cur;
    }
    xcalc_typing = 0; xcalc_digits = 0; xcalc_frac = -1; xcalc_neg = 0;
}

static void xcalc_key_impl(char c)
{
    if (c == 'c' || c == 'C') { xcalc_clear(); xcalc_refresh(); return; }
    if (xcalc_error && c != 'c' && c != 'C') {
        /* Any key after Error starts fresh, like C then the key. */
        xcalc_clear();
    }
    if (c >= '0' && c <= '9') {
        int d = c - '0';
        if (xcalc_result) { xcalc_clear(); }
        if (!xcalc_typing) {
            xcalc_typing = 1; xcalc_digits = 0; xcalc_frac = -1; xcalc_neg = 0;
        }
        /* Count digits typed (integer + fractional). Cap 12. */
        {
            int64_t t = xcalc_digits;
            int n = 0;
            if (t == 0) n = 1;
            while (t > 0) { n++; t /= 10; }
            if (n >= XCALC_MAX_DIGITS) { xcalc_refresh(); return; }
        }
        xcalc_digits = xcalc_digits * 10 + d;
        if (xcalc_frac >= 0 && xcalc_frac < 6) xcalc_frac++;
        xcalc_refresh();
        return;
    }
    if (c == '.') {
        if (xcalc_result) { xcalc_clear(); }
        if (!xcalc_typing) {
            xcalc_typing = 1; xcalc_digits = 0; xcalc_neg = 0;
        }
        if (xcalc_frac < 0) xcalc_frac = 0;
        xcalc_refresh();
        return;
    }
    if (c == 'n') { /* +/- toggle */
        if (xcalc_typing) xcalc_neg = !xcalc_neg;
        else xcalc_acc = -xcalc_acc;
        xcalc_refresh();
        return;
    }
    if (c == '+' || c == '-' || c == '*' || c == '/') {
        xcalc_commit();
        xcalc_pending = c;
        xcalc_result = 0;
        xcalc_refresh();
        return;
    }
    if (c == '=' || c == '\n' || c == '\r') {
        xcalc_commit();
        xcalc_pending = 0;
        xcalc_result = 1;
        xcalc_refresh();
        return;
    }
}

int deb_gui_feed_key(char c)
{
    if (!xcalc_scr) return 0;
    if (c == 'x' || c == 'X') c = '*';
    xcalc_key_impl(c);
    return 1;
}

/* ---------- xcalc LVGL screen ---------- */
static void xcalc_back_event(lv_event_t *e)
{
    (void)e;
    if (xcalc_start_scr) {
        lv_screen_load(xcalc_start_scr);
        lvgl_win8_show();
    }
    xcalc_scr = 0;
    xcalc_disp = 0;
}

static void xcalc_button_event(lv_event_t *e)
{
    char c = (char)(intptr_t)lv_event_get_user_data(e);
    xcalc_key_impl(c);
}

static void xcalc_button(lv_obj_t *parent, const char *label, char key,
                         int x, int y, int w)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_t *l;
    lv_obj_set_size(b, w, 64);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x0067C0), 0);
    lv_obj_set_style_radius(b, 8, 0);
    l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, xcalc_button_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)key);
}

static void xcalc_show_screen(void)
{
    lv_obj_t *bar, *lbl, *back, *content, *note;
    int bx = 24, bw = 150, gap = 16, x, y;
    if (!xcalc_start_scr) xcalc_start_scr = lv_screen_active();
    xcalc_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(xcalc_scr, lv_color_hex(0x1B1B1B), 0);
    lv_obj_set_scrollbar_mode(xcalc_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(xcalc_scr, 0, 0);
    bar = lv_obj_create(xcalc_scr);
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
    lv_obj_add_event_cb(back, xcalc_back_event, LV_EVENT_CLICKED, 0);
    lbl = lv_label_create(bar);
    lv_label_set_text(lbl, "XCALC  -  DEBIAN GUI APP (LVGL BRIDGE)");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 112, 0);
    content = lv_obj_create(xcalc_scr);
    lv_obj_set_size(content, 1280, 664);
    lv_obj_set_pos(content, 0, 56);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1B1B1B), 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_screen_load(xcalc_scr);

    /* Display, TI-30 style: right-aligned readout. */
    xcalc_disp = lv_label_create(content);
    lv_label_set_text(xcalc_disp, "0");
    lv_obj_set_pos(xcalc_disp, 24, 16);
    lv_obj_set_size(xcalc_disp, 700, 64);
    lv_obj_set_style_text_font(xcalc_disp, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(xcalc_disp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(xcalc_disp, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(xcalc_disp, 12, 0);
    lv_label_set_long_mode(xcalc_disp, LV_LABEL_LONG_CLIP);

    /* Keypad: 4 columns, same layout as the real xcalc's
     * basic keys (digits, ., +, -, *, /, =, C, +/-). */
    x = bx; y = 110;
    xcalc_button(content, "C", 'c', x, y, bw);
    xcalc_button(content, "+/-", 'n', x + (bw + gap), y, bw);
    xcalc_button(content, "/", '/', x + 2 * (bw + gap), y, bw);
    xcalc_button(content, "*", '*', x + 3 * (bw + gap), y, bw);
    y += 80;
    xcalc_button(content, "7", '7', x, y, bw);
    xcalc_button(content, "8", '8', x + (bw + gap), y, bw);
    xcalc_button(content, "9", '9', x + 2 * (bw + gap), y, bw);
    xcalc_button(content, "-", '-', x + 3 * (bw + gap), y, bw);
    y += 80;
    xcalc_button(content, "4", '4', x, y, bw);
    xcalc_button(content, "5", '5', x + (bw + gap), y, bw);
    xcalc_button(content, "6", '6', x + 2 * (bw + gap), y, bw);
    xcalc_button(content, "+", '+', x + 3 * (bw + gap), y, bw);
    y += 80;
    xcalc_button(content, "1", '1', x, y, bw);
    xcalc_button(content, "2", '2', x + (bw + gap), y, bw);
    xcalc_button(content, "3", '3', x + 2 * (bw + gap), y, bw);
    xcalc_button(content, "=", '=', x + 3 * (bw + gap), y, bw);
    y += 80;
    xcalc_button(content, "0", '0', x, y, bw * 2 + gap);
    xcalc_button(content, ".", '.', x + 2 * (bw + gap), y, bw);
    xcalc_button(content, "=", '=', x + 3 * (bw + gap), y, bw);

    note = lv_label_create(content);
    lv_label_set_text(note,
        "Native LVGL bridge for xcalc (Debian x11-apps 7.7+9, /usr/bin/xcalc).\n"
        "The real binary is a dynamic X11 ELF and cannot run on FisixOS;\n"
        "this screen re-implements its calculator UI and basic logic.\n"
        "Basic keys only: no sqrt / trig / log, no RPN mode.\n"
        "Fixed-point math: 12 digits in, 6 fractional digits, * and /\n"
        "capped at integer part 1,000,000. Division by zero: Error.");
    lv_obj_set_pos(note, 760, 110);
    lv_obj_set_size(note, 480, 400);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(0xE8E8E8), 0);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);

    xcalc_clear();
    xcalc_refresh();
}

int deb_gui_show(const char *pkg, const char *path)
{
    const deb_gui_entry_t *e = deb_gui_lookup(pkg, path);
    if (!e) return 0;
    if (e->app[0] == 'X' && e->app[1] == 'C') { xcalc_show_screen(); return 1; }
    return 0;
}

int deb_gui_show_app(const char *app)
{
    int i;
    if (!app) return 0;
    for (i = 0; i < DEB_GUI_COUNT; i++) {
        const char *a = deb_gui_table[i].app, *b = app;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0)
            return deb_gui_show(deb_gui_table[i].pkg, deb_gui_table[i].path);
    }
    return 0;
}

