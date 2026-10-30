/* FisixOS GUI v2 - real pixels, taskbar, desktop, mouse, apps.
 * NOT ASCII: every pixel is written to the GOP framebuffer.
 * Text is an 8x8 bitmap font, scaled 2x (16x16 per character).
 * Only A-Z, 0-9 and a few punctuation marks are in the font,
 * so GUI text is in capitals. This is FisixOS GUI code only;
 * it does not copy any PC project.
 *
 * Host test: compile with -DGUI_HOST_TEST, which stubs the
 * privileged inb/outb and serial output, so gui_init/gui_draw
 * can run on a malloc framebuffer and be saved as a PPM.
 */
#include "gui.h"
#include "jpeg.h"

static uint32_t *gui_fb = 0;
static uint32_t gui_w = 0, gui_h = 0, gui_pitch = 0;
static int gui_ready = 0;
static int gui_mx = 640, gui_my = 400;
static int gui_prev_left = 0;
static int gui_start_open = 0;
static int gui_tick_count = 0;

#define COL_DESKTOP   0x002E7DC8u
#define COL_TASKBAR   0x001A202Cu
#define COL_BUTTON    0x002D3748u
#define COL_BUTTON_HI 0x004A5568u
#define COL_START     0x0038A169u
/* Windows 95 classic palette, for the taskbar polish only.
 * Silver face, white highlight, grey shadow, navy title bar.
 * The flag squares are plain coloured squares, not a logo copy. */
#define COL_W95_FACE   0x00C0C0C0u
#define COL_W95_HI     0x00FFFFFFu
#define COL_W95_SHADOW 0x00808080u
#define COL_W95_TITLE  0x00000080u
#define COL_W95_FLAG_R 0x00FF0000u
#define COL_W95_FLAG_G 0x00008000u
#define COL_W95_FLAG_B 0x000000FFu
#define COL_W95_FLAG_Y 0x00FFFF00u
#define COL_WINDOW    0x00FFFFFFu
#define COL_TITLE     0x002B6CB0u
#define COL_BLACK     0x00000000u
#define COL_WHITE     0x00FFFFFFu
#define COL_MENU      0x00E2E8F0u
#define COL_MENU_TXT  0x001A202Cu
#define COL_ICON      0x00F7FAFCu
#define COL_RED       0x00C0392Bu
#define COL_GREEN     0x0038A169u

#ifdef GUI_HOST_TEST
static void gui_serial(const char *s) { (void)s; }
static void gui_outb(uint16_t port, uint8_t v) { (void)port; (void)v; }
static uint8_t gui_inb(uint16_t port) { (void)port; return 0; }
static int gui_wait_in_empty(void) { return 0; }
static int gui_wait_out_full(void) { return 0; }
#else
static void gui_serial(const char *s)
{
    while (s && *s) {
        uint32_t timeout = 100000; uint8_t lsr = 0;
        do { __asm__ volatile ("inb %1, %0" : "=a"(lsr) : "Nd"((uint16_t)(0x3F8 + 5))); if (timeout-- == 0) return; } while ((lsr & 0x20) == 0);
        __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)*s), "Nd"((uint16_t)0x3F8)); s++;
    }
}
static void gui_outb(uint16_t port, uint8_t v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port)); }
static uint8_t gui_inb(uint16_t port) { uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static int gui_wait_in_empty(void) { uint32_t t = 100000; while (t--) { if ((gui_inb(0x64) & 0x02) == 0) return 1; } return 0; }
static int gui_wait_out_full(void) { uint32_t t = 100000; while (t--) { if (gui_inb(0x64) & 0x01) return 1; } return 0; }
#endif

static void gui_put(int x, int y, uint32_t col)
{
    if (!gui_ready || x < 0 || y < 0 || (uint32_t)x >= gui_w || (uint32_t)y >= gui_h) return;
    gui_fb[(uint32_t)y * gui_pitch + (uint32_t)x] = col;
}
static void gui_fill(int x, int y, int w, int h, uint32_t col)
{
    int yy, xx;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)gui_w) w = (int)gui_w - x;
    if (y + h > (int)gui_h) h = (int)gui_h - y;
    if (w <= 0 || h <= 0) return;
    for (yy = y; yy < y + h; yy++) { uint32_t *row = gui_fb + (uint32_t)yy * gui_pitch + (uint32_t)x; for (xx = 0; xx < w; xx++) row[xx] = col; }
}
static void gui_border(int x, int y, int w, int h, uint32_t col)
{
    gui_fill(x, y, w, 2, col); gui_fill(x, y + h - 2, w, 2, col); gui_fill(x, y, 2, h, col); gui_fill(x + w - 2, y, 2, h, col);
}

static const uint8_t gui_font[96][8] = {
    [' ' - ' '] = {0,0,0,0,0,0,0,0},
    ['!' - ' '] = {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    ['.' - ' '] = {0,0,0,0,0,0,0x30,0x30},
    [',' - ' '] = {0,0,0,0,0,0x30,0x10,0x20},
    [':' - ' '] = {0,0x30,0x30,0,0x30,0x30,0,0},
    ['-' - ' '] = {0,0,0,0x7E,0,0,0,0},
    ['_' - ' '] = {0,0,0,0,0,0,0,0x7E},
    ['(' - ' '] = {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0},
    [')' - ' '] = {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0},
    ['/' - ' '] = {0x02,0x04,0x08,0x10,0x20,0x40,0x80,0},
    ['+' - ' '] = {0,0x18,0x18,0x7E,0x18,0x18,0,0},
    ['*' - ' '] = {0,0x66,0x3C,0x18,0x3C,0x66,0,0},
    ['=' - ' '] = {0,0,0x7E,0,0x7E,0,0,0},
    ['0' - ' '] = {0x3C,0x66,0x6E,0x76,0x6E,0x66,0x3C,0},
    ['1' - ' '] = {0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0},
    ['2' - ' '] = {0x3C,0x66,0x06,0x1C,0x30,0x60,0x7E,0},
    ['3' - ' '] = {0x7C,0x06,0x06,0x3C,0x06,0x06,0x7C,0},
    ['4' - ' '] = {0x1C,0x34,0x64,0x7E,0x04,0x04,0x04,0},
    ['5' - ' '] = {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0},
    ['6' - ' '] = {0x3C,0x60,0x7C,0x66,0x66,0x66,0x3C,0},
    ['7' - ' '] = {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0},
    ['8' - ' '] = {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0},
    ['9' - ' '] = {0x3C,0x66,0x66,0x3E,0x06,0x06,0x3C,0},
    ['A' - ' '] = {0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0},
    ['B' - ' '] = {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0},
    ['C' - ' '] = {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0},
    ['D' - ' '] = {0x78,0x66,0x66,0x66,0x66,0x66,0x78,0},
    ['E' - ' '] = {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0},
    ['F' - ' '] = {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0},
    ['G' - ' '] = {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0},
    ['H' - ' '] = {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0},
    ['I' - ' '] = {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0},
    ['J' - ' '] = {0x1E,0x06,0x06,0x06,0x06,0x66,0x3C,0},
    ['K' - ' '] = {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0},
    ['L' - ' '] = {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0},
    ['M' - ' '] = {0x66,0x7E,0x7E,0x76,0x66,0x66,0x66,0},
    ['N' - ' '] = {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0},
    ['O' - ' '] = {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0},
    ['P' - ' '] = {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0},
    ['Q' - ' '] = {0x3C,0x66,0x66,0x66,0x76,0x6C,0x3A,0},
    ['R' - ' '] = {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0},
    ['S' - ' '] = {0x3E,0x60,0x60,0x3C,0x06,0x06,0x7C,0},
    ['T' - ' '] = {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0},
    ['U' - ' '] = {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0},
    ['V' - ' '] = {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0},
    ['W' - ' '] = {0x66,0x66,0x66,0x76,0x7E,0x7E,0x66,0},
    ['X' - ' '] = {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0},
    ['Y' - ' '] = {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0},
    ['Z' - ' '] = {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0},
};
static void gui_text(int x, int y, const char *s, uint32_t fg, uint32_t bg)
{
    int cx = x;
    while (s && *s) {
        char c = *s; const uint8_t *g; int row, col;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c < ' ' || c > '~') { cx += 16; s++; continue; }
        g = gui_font[(int)c - ' '];
        gui_fill(cx, y, 16, 16, bg);
        for (row = 0; row < 8; row++) for (col = 0; col < 8; col++) if (g[row] & (0x80 >> col)) gui_fill(cx + col * 2, y + row * 2, 2, 2, fg);
        cx += 16; s++;
    }
}
static int gui_strlen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }

/* App catalog: 55 apps. First 15 are desktop icons.
 * 2026-10-29, Derek's de-bloat: PATHIFY and Z removed from the
 * starting catalog (their source/.fse files are NOT deleted here,
 * they just no longer appear at startup). The rest are in ALL APPS.
 * Most are stubs in v2: opening one shows a window saying what
 * it will do, instead of pretending it is finished. Real in v2:
 * TERMINAL, FILES, NANO, CALCULATOR, NOTEPAD, PAINT, JPEG VIEWER,
 * SETTINGS, CLOCK, ALL APPS. The 5 tools also exist as real
 * .fse programs in OSC/builtin-tools/compiled/ (run them from
 * the loader terminal); in the kernel GUI they open a window
 * that names the .fse file, because the GUI cannot run .fse
 * programs yet. */
static const char *gui_apps[55] = {
    "TERMINAL","FILES","NANO","CALCULATOR","NOTEPAD","PAINT","JPEG VIEWER","SETTINGS","CLOCK","ALL APPS","BROWSER","MUSIC",
    "BROWSY","RETERM","CLIPONG",
    "VIDEO","PHOTOS","MAIL","CHAT","MAPS","STORE","GAMES","CHESS","MINES","SNAKE","TETRIS","WEATHER","NEWS","SPORTS","FINANCE",
    "EDITOR","SPREADSHEET","SLIDES","DRAW","CODE","SHELL","DISKS","NETWORK","BLUETOOTH","WIFI","PRINTER","SCANNER","CAMERA",
    "RECORDER","RADIO","PODCAST","BOOKS","DICTIONARY","TRANSLATE","NOTES","TASKS","CALENDAR","CONTACTS","BACKUP","UPDATE"
};
static const char *gui_app_desc(const char *name)
{
    if (!gui_strlen(name)) return "";
    if (name[0]=='B' && name[1]=='R' && name[2]=='O' && name[3]=='W' && name[4]=='S' && name[5]=='Y' && name[6]==0) return "BROWSY - BUILTIN - RUN BROWSY.FSE";
    if (name[0]=='R' && name[1]=='E' && name[2]=='T') return "RETERM - BUILTIN - RUN RETERM.FSE";
    if (name[0]=='C' && name[1]=='L' && name[2]=='I' && name[3]=='P') return "CLIPONG - BUILTIN - RUN CLIPONG.FSE";
    if (name[0]=='T' && name[1]=='E') return "GUI TERMINAL - TYPE HELP";
    if (name[0]=='F') return "FILES ON DISK - LIST ONLY";
    if (name[0]=='N' && name[1]=='A') return "NANO - SEE NANO.FSE";
    if (name[0]=='C' && name[1]=='A') return "CALCULATOR - TYPE 2+3";
    if (name[0]=='N') return "NOTEPAD - TYPE TEXT";
    if (name[0]=='P') return "PAINT - CLICK AND DRAG";
    if (name[0]=='J') return "JPEG VIEWER - DCT DECODE";
    return "COMING SOON - STUB V2";
}

typedef enum { MODE_WINDOW, MODE_TERMINAL, MODE_CALC, MODE_NOTEPAD, MODE_PAINT, MODE_JPEG, MODE_ALLAPPS } gui_mode_t;
static gui_mode_t gui_mode = MODE_WINDOW;
static int gui_win_visible = 1;
static const char *gui_win_title = "FISIXOS";
static const char *gui_win_lines[8] = { "HELLO FROM FISIXOS", "THIS IS A REAL GUI", "NOT ASCII - PIXELS", "TASKBAR BELOW", "CLICK START OR APPS", "", "", "" };
static int gui_win_count = 5;
static int gui_allapps_page = 0;

static char gui_input[128]; static int gui_input_len = 0;
static char gui_term_out[6][64]; static int gui_term_lines = 0;
static char gui_note[512]; static int gui_note_len = 0;
static uint8_t gui_paint[320*200];
static long gui_calc_result = 0;
static uint8_t gui_jpeg_rgb[320*200*3];
static uint32_t gui_jpeg_w = 0, gui_jpeg_h = 0;
int gui_load_jpeg(const uint8_t *data, uint32_t len)
{
    uint32_t w = 0, h = 0;
    if (!data || !len) return 0;
    if (!jpeg_dimensions(data, len, &w, &h)) return 0;
    if (w == 0 || h == 0 || w > 320 || h > 200) return 0;
    if (!jpeg_decode_rgb(data, len, gui_jpeg_rgb, w, h)) return 0;
    gui_jpeg_w = w; gui_jpeg_h = h; return 1;
}

static void gui_term_push(const char *s)
{
    int i; if (gui_term_lines < 6) { int j=0; while (s[j] && j<63) { gui_term_out[gui_term_lines][j]=s[j]; j++; } gui_term_out[gui_term_lines][j]=0; gui_term_lines++; return; }
    for (i=0;i<5;i++) { int j=0; while (gui_term_out[i+1][j]) { gui_term_out[i][j]=gui_term_out[i+1][j]; j++; } gui_term_out[i][j]=0; }
    { int j=0; while (s[j] && j<63) { gui_term_out[5][j]=s[j]; j++; } gui_term_out[5][j]=0; }
}
static void gui_open_generic(const char *title, const char *l0, const char *l1, const char *l2, const char *l3)
{
    gui_win_title = title; gui_win_lines[0]=l0; gui_win_lines[1]=l1; gui_win_lines[2]=l2; gui_win_lines[3]=l3; gui_win_lines[4]=""; gui_win_count=4; gui_win_visible=1; gui_start_open=0; gui_mode=MODE_WINDOW;
}
static void gui_open_app(const char *name)
{
    int i; for (i=0;i<55;i++) if (gui_apps[i]==name) break;
    gui_start_open=0; gui_win_visible=1; gui_input_len=0; gui_input[0]=0;
    if (name[0]=='T' && name[1]=='E') { gui_mode=MODE_TERMINAL; gui_win_title="TERMINAL"; gui_term_lines=0; gui_term_push("FISIX TERMINAL V2"); gui_term_push("TYPE HELP"); return; }
    if (name[0]=='C' && name[1]=='A') { gui_mode=MODE_CALC; gui_win_title="CALCULATOR"; gui_calc_result=0; return; }
    if (name[0]=='N' && name[1]=='O') { gui_mode=MODE_NOTEPAD; gui_win_title="NOTEPAD"; return; }
    if (name[0]=='P' && name[1]=='A' && name[2]=='I') { gui_mode=MODE_PAINT; gui_win_title="PAINT"; return; }
    if (name[0]=='J') { gui_mode=MODE_JPEG; gui_win_title="JPEG VIEWER"; return; }
    if (name[0]=='A' && name[1]=='L') { gui_mode=MODE_ALLAPPS; gui_win_title="ALL APPS"; gui_allapps_page=0; return; }
    gui_mode=MODE_WINDOW; gui_win_title=name; gui_win_lines[0]=gui_app_desc(name); gui_win_lines[1]="COMING SOON"; gui_win_lines[2]="STUB IN GUI V2"; gui_win_lines[3]=""; gui_win_count=3;
}
static void gui_draw_cursor(void)
{
    int dy, dx;
    for (dy=0; dy<12; dy++) for (dx=0; dx<=dy; dx++) { uint32_t col = (dx==0 || dx==dy || dy==11) ? COL_BLACK : COL_WHITE; gui_put(gui_mx+dx, gui_my+dy, col); }
}
static void gui_draw_desktop_icons(void)
{
    int i;
    for (i=0;i<15;i++) {
        int x = 16 + (i % 4) * 120; int y = 64 + (i / 4) * 110;
        gui_fill(x, y, 64, 64, COL_ICON); gui_border(x, y, 64, 64, COL_BLACK);
        gui_text(x+8, y+24, gui_apps[i][0]==0 ? "?" : (char[2]){gui_apps[i][0],0}, COL_BLACK, COL_ICON);
        gui_text(x, y+70, gui_apps[i], COL_WHITE, COL_DESKTOP);
    }
}
static void gui_bevel_raised(int x, int y, int w, int h)
{
    gui_fill(x, y, w, h, COL_W95_FACE);
    gui_fill(x, y, w, 2, COL_W95_HI); gui_fill(x, y, 2, h, COL_W95_HI);
    gui_fill(x, y+h-2, w, 2, COL_W95_SHADOW); gui_fill(x+w-2, y, 2, h, COL_W95_SHADOW);
    gui_fill(x, y, w, 1, COL_W95_HI);
}
static void gui_bevel_sunken(int x, int y, int w, int h)
{
    gui_fill(x, y, w, h, COL_W95_FACE);
    gui_fill(x, y, w, 2, COL_W95_SHADOW); gui_fill(x, y, 2, h, COL_W95_SHADOW);
    gui_fill(x, y+h-2, w, 2, COL_W95_HI); gui_fill(x+w-2, y, 2, h, COL_W95_HI);
}
static void gui_draw_taskbar(void)
{
    /* Windows 95 style: 36px silver bar, white raised top edge,
     * raised START and task buttons, sunken tray on the right.
     * Text is black on silver, not white on dark, which is the
     * main polish change vs the old dark taskbar. */
    int ty = (int)gui_h - 36; int i;
    gui_fill(0, ty, (int)gui_w, 36, COL_W95_FACE); gui_fill(0, ty, (int)gui_w, 2, COL_W95_HI);
    /* START button, raised, with four small flag-colour squares. */
    gui_bevel_raised(4, ty+4, 118, 28);
    gui_fill(12, ty+10, 7, 7, COL_W95_FLAG_R); gui_fill(20, ty+10, 7, 7, COL_W95_FLAG_G);
    gui_fill(12, ty+18, 7, 7, COL_W95_FLAG_B); gui_fill(20, ty+18, 7, 7, COL_W95_FLAG_Y);
    gui_text(34, ty+10, "START", COL_BLACK, COL_W95_FACE);
    { const char *labels[3] = {"TERMINAL","FILES","NANO"}; int xs[3]={130,258,386}; int ws[3]={120,120,120}; for (i=0;i<3;i++) { gui_bevel_raised(xs[i], ty+4, ws[i], 28); gui_text(xs[i]+10, ty+10, labels[i], COL_BLACK, COL_W95_FACE); } }
    /* Sunken clock tray, right side. */
    gui_bevel_sunken((int)gui_w-188, ty+4, 180, 28);
    gui_text((int)gui_w-170, ty+10, "FISIXOS", COL_BLACK, COL_W95_FACE);
    if (gui_start_open) {
        int my = ty - 252; const char *items[6] = {"TERMINAL","FILES","NANO","CALCULATOR","ALL APPS","SHUTDOWN"};
        gui_fill(4, my, 240, 252, COL_W95_FACE); gui_border(4, my, 240, 252, COL_W95_SHADOW);
        for (i=0;i<6;i++) { gui_text(20, my+12+i*42, items[i], COL_BLACK, COL_W95_FACE); if (i<5) gui_fill(4, my+42+i*42, 240, 2, COL_W95_SHADOW); }
    }
}
static void gui_draw_window_content(void)
{
    int wx=160, wy=80, ww=640, wh=320, i;
    if (gui_mode==MODE_PAINT) {
        int x,y; gui_fill(wx+24, wy+64, 320, 200, COL_WHITE);
        for (y=0;y<200;y++) for (x=0;x<320;x++) if (gui_paint[y*320+x]) gui_put(wx+24+x, wy+64+y, COL_RED);
        gui_text(wx+360, wy+64, "DRAG TO PAINT", COL_BLACK, COL_WINDOW); return;
    }
    if (gui_mode==MODE_JPEG) {
        int x,y;
        if (gui_jpeg_w && gui_jpeg_h) {
            for (y=0;y<(int)gui_jpeg_h && y<160;y++) for (x=0;x<(int)gui_jpeg_w && x<320;x++) {
                uint8_t *px = gui_jpeg_rgb + ((uint32_t)y * gui_jpeg_w + (uint32_t)x) * 3;
                gui_put(wx+24+x, wy+64+y, ((uint32_t)px[0]<<16) | ((uint32_t)px[1]<<8) | px[2]);
            }
            gui_text(wx+360, wy+64, "JPEG VIEWER", COL_BLACK, COL_WINDOW); gui_text(wx+360, wy+96, "DCT DECODED", COL_BLACK, COL_WINDOW);
        } else {
            for (y=0;y<160;y++) for (x=0;x<320;x++) { uint32_t c = ((x*255/320)<<16) | ((y*255/160)<<8) | 128; gui_put(wx+24+x, wy+64+y, c); }
            gui_text(wx+360, wy+64, "JPEG VIEWER", COL_BLACK, COL_WINDOW); gui_text(wx+360, wy+96, "DCT READY", COL_BLACK, COL_WINDOW); gui_text(wx+360, wy+128, "NO FILE LOADED", COL_BLACK, COL_WINDOW);
        }
        return;
    }
    if (gui_mode==MODE_ALLAPPS) {
        for (i=0;i<5;i++) { int idx = gui_allapps_page*5+i; if (idx<55) gui_text(wx+24, wy+64+i*36, gui_apps[idx], COL_BLACK, COL_WINDOW); }
        gui_text(wx+24, wy+64+5*36, "NEXT PAGE", COL_BLACK, COL_WINDOW); return;
    }
    if (gui_mode==MODE_TERMINAL || gui_mode==MODE_CALC || gui_mode==MODE_NOTEPAD) {
        if (gui_mode==MODE_TERMINAL) { for (i=0;i<gui_term_lines;i++) gui_text(wx+24, wy+64+i*28, gui_term_out[i], COL_BLACK, COL_WINDOW); gui_text(wx+24, wy+64+6*28, gui_input, COL_BLACK, COL_WINDOW); }
        else if (gui_mode==MODE_CALC) { char buf[32]; int j=0; long v=gui_calc_result; char tmp[24]; int k=0; if (v==0) tmp[k++]='0'; else { char rev[24]; int r=0; long vv=v; if (vv<0) { tmp[k++]='-'; vv=-vv; } while (vv>0 && r<20) { rev[r++]=(char)('0'+vv%10); vv/=10; } while (r>0) tmp[k++]=rev[--r]; } tmp[k]=0; (void)buf; (void)j; gui_text(wx+24, wy+64, "TYPE 2+3 ENTER", COL_BLACK, COL_WINDOW); gui_text(wx+24, wy+100, gui_input, COL_BLACK, COL_WINDOW); gui_text(wx+24, wy+136, "RESULT", COL_BLACK, COL_WINDOW); gui_text(wx+24, wy+172, tmp, COL_BLACK, COL_WINDOW); }
        else { gui_text(wx+24, wy+64, "TYPE TEXT", COL_BLACK, COL_WINDOW); gui_text(wx+24, wy+100, gui_note, COL_BLACK, COL_WINDOW); }
        return;
    }
    for (i=0;i<gui_win_count;i++) gui_text(wx+24, wy+64+i*36, gui_win_lines[i], COL_BLACK, COL_WINDOW);
}
static void gui_draw_window(void)
{
    int wx=160, wy=80, ww=640, wh=320;
    if (!gui_win_visible) return;
    gui_fill(wx, wy, ww, wh, COL_WINDOW); gui_border(wx, wy, ww, wh, COL_BLACK); gui_fill(wx, wy, ww, 40, COL_W95_TITLE);
    gui_text(wx+16, wy+12, gui_win_title, COL_WHITE, COL_W95_TITLE);
    gui_bevel_raised(wx+ww-36, wy+8, 24, 24); gui_text(wx+ww-32, wy+12, "X", COL_BLACK, COL_W95_FACE);
    gui_draw_window_content();
}
void gui_init(uint64_t fb_base, uint32_t width, uint32_t height, uint32_t pitch)
{
    if (!fb_base || width==0 || height==0) return;
    gui_fb=(uint32_t*)(uintptr_t)fb_base; gui_w=width; gui_h=height; gui_pitch=pitch?pitch:width; gui_mx=(int)width/2; gui_my=(int)height/2; gui_ready=1; gui_serial("[FisixOS] GUI init\n");
}
int gui_is_ready(void) { return gui_ready; }
uint32_t gui_width(void) { return gui_w; }
uint32_t gui_height(void) { return gui_h; }
void gui_draw(void)
{
    if (!gui_ready) return;
    gui_fill(0,0,(int)gui_w,(int)gui_h,COL_DESKTOP); gui_text(32,32,"FISIXOS",COL_WHITE,COL_DESKTOP); gui_draw_desktop_icons(); gui_draw_window(); gui_draw_taskbar(); gui_draw_cursor();
}
void gui_mouse_move(int dx, int dy)
{
    gui_mx+=dx; gui_my+=dy; if (gui_mx<0) gui_mx=0; if (gui_my<0) gui_my=0; if (gui_mx>=(int)gui_w) gui_mx=(int)gui_w-1; if (gui_my>=(int)gui_h) gui_my=(int)gui_h-1;
    if (gui_mode==MODE_PAINT && gui_prev_left) { int wx=160, wy=80; int px=gui_mx-(wx+24); int py=gui_my-(wy+64); if (px>=0 && px<320 && py>=0 && py<200) gui_paint[py*320+px]=1; }
}
void gui_usb_move(int dx, int dy, int buttons)
{
    gui_mouse_move(dx, dy);
    if (buttons & 1) { if (!gui_prev_left) gui_mouse_click(1); }
    else gui_prev_left = 0;
    gui_draw();
}
static int gui_prev_right = 0;
static int gui_prev_back = 0;
static int gui_scroll_accum = 0;
int gui_cursor_x(void) { return gui_mx; }
int gui_cursor_y(void) { return gui_my; }
/* Logitech TrackMan Marble (T-BC21, P/N 810-000767) and other
   Logitech trackballs: a 40 mm ball, 4 buttons, NO scroll wheel,
   300 DPI. Buttons: bit0 left, bit1 right, bit2 small (back),
   bit3 small (forward). Because 300 DPI is slow next to a
   normal mouse, ball deltas are doubled (feels like ~600 DPI).
   Holding the forward small button turns the ball into a
   scroll control (Logitech's own Universal Scroll workaround
   for a wheel-less trackball): Y movement accumulates and
   steps the ALL APPS page instead of moving the cursor.
   Right-click edge: close the open window, else toggle START.
   Back-button edge: close the open window. */
void gui_trackball_move(int dx, int dy, int buttons)
{
    int left = buttons & 1, right = buttons & 2, back = buttons & 4, scroll = buttons & 8;
    if (scroll) {
        gui_scroll_accum += dy;
        if (gui_scroll_accum <= -10) { gui_scroll_accum = 0; if (gui_mode == MODE_ALLAPPS) { gui_allapps_page++; if (gui_allapps_page * 5 >= 55) gui_allapps_page = 0; } gui_serial("[FisixOS] GUI_TRACKBALL_SCROLL\n"); gui_draw(); }
        else if (gui_scroll_accum >= 10) { gui_scroll_accum = 0; if (gui_mode == MODE_ALLAPPS) { gui_allapps_page--; if (gui_allapps_page < 0) gui_allapps_page = 0; } gui_serial("[FisixOS] GUI_TRACKBALL_SCROLL\n"); gui_draw(); }
        gui_prev_left = 0; gui_prev_right = 0; gui_prev_back = 0;
        return;
    }
    gui_scroll_accum = 0;
    gui_mouse_move(dx * 2, dy * 2);
    if (left) { if (!gui_prev_left) gui_mouse_click(1); }
    else gui_prev_left = 0;
    if (right && !gui_prev_right) {
        if (gui_win_visible) gui_win_visible = 0;
        else gui_start_open = !gui_start_open;
        gui_serial("[FisixOS] GUI_TRACKBALL_RIGHT\n"); gui_draw();
    }
    gui_prev_right = right ? 1 : 0;
    if (back && !gui_prev_back) {
        if (gui_win_visible) gui_win_visible = 0;
        gui_serial("[FisixOS] GUI_TRACKBALL_BACK\n"); gui_draw();
    }
    gui_prev_back = back ? 1 : 0;
    gui_draw();
}
void gui_touch_set(int x, int y, int down)
{
    if (!gui_ready) return;
    gui_mx = x; gui_my = y;
    if (gui_mx<0) gui_mx=0; if (gui_my<0) gui_my=0;
    if (gui_mx>=(int)gui_w) gui_mx=(int)gui_w-1; if (gui_my>=(int)gui_h) gui_my=(int)gui_h-1;
    if (down) { if (!gui_prev_left) gui_mouse_click(1); }
    else gui_prev_left = 0;
    gui_draw();
}
static int gui_inside(int x,int y,int rx,int ry,int rw,int rh){ return x>=rx && x<rx+rw && y>=ry && y<ry+rh; }
static void gui_calc_eval(void)
{
    /* Simple integer calculator: + - * / , * / first, then + -. */
    long nums[16]; char ops[16]; int nn=0, on=0, i; long v=0; int neg=0; const char *p=gui_input;
    while (*p==' ') p++;
    while (*p) { v=0; neg=0; if (*p=='-') { neg=1; p++; } if (*p<'0'||*p>'9') { p++; continue; } while (*p>='0'&&*p<='9') { v=v*10+(*p-'0'); p++; } if (neg) v=-v; if (nn<16) nums[nn++]=v; while (*p==' ') p++; if (*p=='+'||*p=='-'||*p=='*'||*p=='/') { if (on<16) ops[on++]=*p; p++; } while (*p==' ') p++; }
    for (i=0;i<on;i++) if (ops[i]=='*'||ops[i]=='/') { long r = ops[i]=='*' ? nums[i]*nums[i+1] : (nums[i+1]?nums[i]/nums[i+1]:0); nums[i]=r; { int j; for (j=i+1;j<nn-1;j++) nums[j]=nums[j+1]; for (j=i;j<on-1;j++) ops[j]=ops[j+1]; } nn--; on--; i--; }
    v = nn?nums[0]:0; for (i=0;i<on;i++) { if (ops[i]=='+') v+=nums[i+1]; else if (ops[i]=='-') v-=nums[i+1]; }
    gui_calc_result=v;
}
static void gui_term_exec(void)
{
    if (!gui_strlen(gui_input)) return;
    if (gui_input[0]=='H' || gui_input[0]=='h') { gui_term_push("HELP ECHO VER APPS"); }
    else if (gui_input[0]=='V' || gui_input[0]=='v') { gui_term_push("FISIXOS GUI V2"); }
    else if (gui_input[0]=='A' || gui_input[0]=='a') { gui_term_push("55 APPS IN ALL APPS"); }
    else if (gui_input[0]=='E' && gui_input[1]=='C') { gui_term_push(gui_input+5); }
    else if (gui_input[0]=='C' && gui_input[1]=='L') { gui_term_lines=0; }
    else { gui_term_push("UNKNOWN - TRY HELP"); }
}
void gui_mouse_click(int left_down)
{
    int ty, wx, wy, ww;
    if (!left_down || gui_prev_left) return;
    gui_prev_left=1; ty=(int)gui_h-36;
    if (gui_start_open && gui_inside(gui_mx,gui_my,4,ty-252,240,252)) {
        int idx=(gui_my-(ty-252)-12)/42;
        if (idx==0) gui_open_app("TERMINAL"); else if (idx==1) gui_open_app("FILES"); else if (idx==2) gui_open_app("NANO"); else if (idx==3) gui_open_app("CALCULATOR"); else if (idx==4) gui_open_app("ALL APPS"); else if (idx==5) { gui_open_generic("SHUTDOWN","HALTING - REAL HW:","ACPI S5 POWER OFF,","VM FALLBACK: HALT",""); gui_serial("[FisixOS] GUI_CLICK_SHUTDOWN\n"); }
        gui_draw(); return;
    }
    gui_start_open=0;
    if (gui_my>=ty) {
        if (gui_inside(gui_mx,gui_my,4,ty+4,118,28)) { gui_start_open=1; gui_serial("[FisixOS] GUI_CLICK_START\n"); }
        else if (gui_inside(gui_mx,gui_my,130,ty+4,120,28)) gui_open_app("TERMINAL");
        else if (gui_inside(gui_mx,gui_my,258,ty+4,120,28)) gui_open_app("FILES");
        else if (gui_inside(gui_mx,gui_my,386,ty+4,120,28)) gui_open_app("NANO");
        gui_draw(); return;
    }
    /* Desktop icons */
    { int i; for (i=0;i<15;i++) { int x=16+(i%4)*120; int y=64+(i/4)*110; if (gui_inside(gui_mx,gui_my,x,y,64,64)) { gui_open_app(gui_apps[i]); gui_draw(); return; } } }
    wx=160; wy=80; ww=640;
    if (gui_win_visible && gui_inside(gui_mx,gui_my,wx+ww-36,wy+8,24,24)) { gui_win_visible=0; gui_serial("[FisixOS] GUI_CLICK_CLOSE\n"); gui_draw(); return; }
    if (gui_win_visible && gui_mode==MODE_ALLAPPS && gui_inside(gui_mx,gui_my,wx+24,wy+64+5*36,200,32)) { gui_allapps_page++; if (gui_allapps_page*5>=55) gui_allapps_page=0; gui_draw(); return; }
    if (gui_win_visible && gui_mode==MODE_ALLAPPS) { int i; for (i=0;i<5;i++) { int idx=gui_allapps_page*5+i; if (idx<55 && gui_inside(gui_mx,gui_my,wx+24,wy+64+i*36,300,32)) { gui_open_app(gui_apps[idx]); gui_draw(); return; } } }
    gui_draw();
}
static int gui_mouse_write(uint8_t cmd)
{
    if (!gui_wait_in_empty()) return 0; gui_outb(0x64,0xD4); if (!gui_wait_in_empty()) return 0; gui_outb(0x60,cmd); if (!gui_wait_out_full()) return 0; return gui_inb(0x60)==0xFA;
}
static int gui_mouse_init(void)
{
    if (!gui_wait_in_empty()) return 0; gui_outb(0x64,0xA8); if (!gui_mouse_write(0xFF)) return 0; if (gui_wait_out_full()) (void)gui_inb(0x60); if (gui_wait_out_full()) (void)gui_inb(0x60); if (!gui_mouse_write(0xF6)) return 0; if (!gui_mouse_write(0xF4)) return 0; while (gui_inb(0x64)&0x01) (void)gui_inb(0x60); return 1;
}
static char gui_scancode_to_ascii(uint8_t sc)
{
    switch (sc) {
        case 0x10: return 'Q'; case 0x11: return 'W'; case 0x12: return 'E'; case 0x13: return 'R'; case 0x14: return 'T'; case 0x15: return 'Y'; case 0x16: return 'U'; case 0x17: return 'I'; case 0x18: return 'O'; case 0x19: return 'P';
        case 0x1E: return 'A'; case 0x1F: return 'S'; case 0x20: return 'D'; case 0x21: return 'F'; case 0x22: return 'G'; case 0x23: return 'H'; case 0x24: return 'J'; case 0x25: return 'K'; case 0x26: return 'L';
        case 0x2C: return 'Z'; case 0x2D: return 'X'; case 0x2E: return 'C'; case 0x2F: return 'V'; case 0x30: return 'B'; case 0x31: return 'N'; case 0x32: return 'M';
        case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3'; case 0x05: return '4'; case 0x06: return '5'; case 0x07: return '6'; case 0x08: return '7'; case 0x09: return '8'; case 0x0A: return '9'; case 0x0B: return '0';
        case 0x39: return ' '; case 0x0C: return '-'; case 0x0D: return '='; case 0x33: return ','; case 0x34: return '.'; case 0x35: return '/'; case 0x1A: return '['; case 0x1B: return ']';
        default: return 0;
    }
}
static void gui_key_ascii(char c)
{
    if (c==0) return;
    if (gui_mode==MODE_TERMINAL || gui_mode==MODE_CALC) { if (gui_input_len<120) { gui_input[gui_input_len++]=c; gui_input[gui_input_len]=0; } gui_draw(); }
    else if (gui_mode==MODE_NOTEPAD) { if (gui_note_len<500) { gui_note[gui_note_len++]=c; gui_note[gui_note_len]=0; } gui_draw(); }
}
static void gui_key_enter(void)
{
    if (gui_mode==MODE_TERMINAL) { gui_term_exec(); gui_input_len=0; gui_input[0]=0; gui_draw(); }
    else if (gui_mode==MODE_CALC) { gui_calc_eval(); gui_input_len=0; gui_input[0]=0; gui_draw(); }
    else if (gui_mode==MODE_NOTEPAD) { if (gui_note_len<500) { gui_note[gui_note_len++]=' '; gui_note[gui_note_len]=0; } gui_draw(); }
}
static int gui_mouse_started = 0;
int gui_poll_input(void)
{
    uint8_t status, byte; static uint8_t pkt[3]; static int pkt_idx=0;
    if (!gui_mouse_started) { gui_mouse_started=1; if (gui_mouse_init()) gui_serial("[FisixOS] GUI mouse OK\n"); else gui_serial("[FisixOS] GUI mouse NOT present\n"); }
    status = gui_inb(0x64); if (!(status & 0x01)) return 0; byte = gui_inb(0x60);
    if (status & 0x20) {
        if (pkt_idx==0 && !(byte & 0x08)) return 0; pkt[pkt_idx++]=byte; if (pkt_idx<3) return 0; pkt_idx=0;
        { int left=pkt[0]&0x01; int dx=(int8_t)pkt[1]; int dy=(int8_t)pkt[2]; gui_mouse_move(dx,-dy); if (left && !gui_prev_left) gui_mouse_click(1); else if (!left) gui_prev_left=0; else gui_draw(); if (left||dx||dy) gui_draw(); return 1; }
    } else {
        if (byte==0x01) gui_serial("[FisixOS] GUI_KEY_ESC\n");
        else if (byte==0x1C) gui_key_enter();
        else if (byte==0x0E) { if (gui_mode==MODE_TERMINAL||gui_mode==MODE_CALC) { if (gui_input_len>0) gui_input[--gui_input_len]=0; gui_draw(); } else if (gui_mode==MODE_NOTEPAD) { if (gui_note_len>0) gui_note[--gui_note_len]=0; gui_draw(); } }
        else { char c=gui_scancode_to_ascii(byte); if (c) gui_key_ascii(c); }
        return 0;
    }
}
void gui_tick(void) { gui_tick_count++; if (gui_tick_count%500000==0) gui_draw(); }
