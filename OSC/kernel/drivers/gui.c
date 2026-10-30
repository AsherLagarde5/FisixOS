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
#include "generic.h"
#include "proc.h"
#include "fse_embedded.h"
#include "logo_embedded.h"
static void gui_run_fse(const char *name);

static uint32_t *gui_fb = 0;
static uint32_t gui_w = 0, gui_h = 0, gui_pitch = 0;
static int gui_ready = 0;
static int gui_font_px = 2;
static int gui_taskbar_h = 48;
static int gui_s(int v) { return v * gui_font_px / 2; }
static uint32_t gui_shade(uint32_t col, int num, int den) { int r=((col>>16)&255)*num/den, g=((col>>8)&255)*num/den, b=(col&255)*num/den; return ((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b; }
#define COL_TRANSPARENT 0xFFFFFFFFu
#define COL_W11_TASK 0x001E1E2Eu
#define COL_W11_TASK_BTN 0x002D2D3Au
/* Accent colour, changeable in SETTINGS, 2026-10-30. Default is dark purple, Asher's choice. */
static uint32_t gui_accent_col = 0x006B21A8u;
#define COL_W11_ACCENT gui_accent_col
#define COL_W11_TITLE_BG 0x00F3F3F3u
#define COL_W11_GREY 0x00666666u
#define COL_W11_SHADOW 0x00111111u
static int gui_mx = 640, gui_my = 400;
static int gui_prev_left = 0;
static int gui_start_open = 0;
static int gui_tick_count = 0;
static int gui_e0_prefix = 0;
static void gui_draw_desktop_icons(void);
static void gui_draw_taskbar(void);
static void gui_draw_window(void);
static void gui_draw_window_only(void);
static void gui_draw_content_only(void);
static void gui_draw_close_window(void);
static void gui_redraw_window_move(int ox,int oy,int ow,int oh);
static int gui_taskbar_btn_w(void);
static int gui_taskbar_btn_h(void);
static int gui_taskbar_start_x(void);
static int gui_taskbar_btn_x(int idx);
static void gui_move_to(int x, int y);
static void gui_draw_cursor(void);
static void gui_save_cursor_box(void);
static void gui_restore_cursor_box(void);
static void gui_draw_cursor_shape(void);
static void gui_open_app(const char *name);
static int gui_tab_idx = 0;

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
#define COL_ORANGE    0x00ED8936u
#define COL_YELLOW    0x00ECC94Bu
#define COL_PURPLE    0x00805AD5u
#define COL_PINK      0x00ED64A6u
#define COL_CYAN      0x0038B2ACu
#define COL_RAINBOW_R 0x00E53E3Eu
#define COL_RAINBOW_O 0x00DD6B20u
#define COL_RAINBOW_Y 0x00D69E2Eu
#define COL_RAINBOW_G 0x0038A169u
#define COL_RAINBOW_B 0x003183C9u
#define COL_RAINBOW_I 0x006B46C1u
#define COL_RAINBOW_V 0x00805AD5u

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
static void gui_fill_rounded(int x, int y, int w, int h, int r, uint32_t col)
{
    int dy, dx;
    if (r <= 0 || w <= 2*r || h <= 2*r) { gui_fill(x, y, w, h, col); return; }
    gui_fill(x+r, y, w-2*r, h, col); gui_fill(x, y+r, r, h-2*r, col); gui_fill(x+w-r, y+r, r, h-2*r, col);
    for (dy=0; dy<r; dy++) {
        int inside_x = r;
        for (dx=0; dx<r; dx++) { int ax=r-dx, ay=r-dy; if (ax*ax+ay*ay <= r*r) { inside_x=dx; break; } }
        gui_fill(x+inside_x, y+dy, r-inside_x, 1, col); gui_fill(x+w-r, y+dy, r-inside_x, 1, col);
        gui_fill(x+inside_x, y+h-1-dy, r-inside_x, 1, col); gui_fill(x+w-r, y+h-1-dy, r-inside_x, 1, col);
    }
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
    int cx = x, cw = 8 * gui_font_px;
    while (s && *s) {
        char c = *s; const uint8_t *g; int row, col;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c < ' ' || c > '~') { cx += cw; s++; continue; }
        g = gui_font[(int)c - ' '];
        if (bg != COL_TRANSPARENT) gui_fill(cx, y, cw, cw, bg);
        for (row = 0; row < 8; row++) for (col = 0; col < 8; col++) if (g[row] & (0x80 >> col)) gui_fill(cx + col * gui_font_px, y + row * gui_font_px, gui_font_px, gui_font_px, fg);
        cx += cw; s++;
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
    "RECORDER","RADIO","PODCAST","RAINBOW","DICTIONARY","TRANSLATE","NOTES","TASKS","CALENDAR","CONTACTS","BACKUP","UPDATE"
};
static const char *gui_app_desc(const char *name)
{
    if (!gui_strlen(name)) return "";
    if (name[0]=='B' && name[1]=='R' && name[2]=='O' && name[3]=='W' && name[4]=='S' && name[5]=='Y' && name[6]==0) return "BROWSY - BUILTIN - RUN BROWSY.FSE";
    if (name[0]=='B' && name[1]=='R' && name[2]=='O' && name[3]=='W' && name[4]=='S' && name[5]=='E' && name[6]=='R' && name[7]==0) return "BROWSER - RUN BROWSY.FSE";
    if (name[0]=='R' && name[1]=='E' && name[2]=='T') return "RETERM - BUILTIN - RUN RETERM.FSE";
    if (name[0]=='C' && name[1]=='L' && name[2]=='I' && name[3]=='P') return "CLIPONG - BUILTIN - RUN CLIPONG.FSE";
    if (name[0]=='T' && name[1]=='E') return "GUI TERMINAL - TYPE HELP";
    if (name[0]=='F' && name[1]=='I') return "FILES - OPEN TERMINAL - DIR";
    if (name[0]=='N' && name[1]=='A') return "NANO - SEE NANO.FSE";
    if (name[0]=='C' && name[1]=='A') return "CALCULATOR - TYPE 2+3";
    if (name[0]=='N' && name[1]=='O') return "NOTEPAD - TYPE TEXT";
    if (name[0]=='P' && name[1]=='A') return "PAINT - CLICK AND DRAG";
    if (name[0]=='J') return "JPEG VIEWER - DCT DECODE";
    if (name[0]=='S' && name[1]=='E') return "SETTINGS - ARROWS + ENTER";
    if (name[0]=='C' && name[1]=='L') return "CLOCK - GUI TICKS ONLY";
    if (name[0]=='M' && name[1]=='U') return "MUSIC - PC SPEAKER BEEP";
    if (name[0]=='R' && name[1]=='A' && name[2]=='I') return "RAINBOW - FOR MY SISTER";
    if (name[0]=='G' && name[1]=='A') return "GAMES - PONG MINES SNAKE";
    if (name[0]=='M' && name[1]=='I') return "MINESWEEPER - CLICK A CELL";
    if (name[0]=='S' && name[1]=='N') return "SNAKE - ARROWS TO MOVE";
    if (name[0]=='A' && name[1]=='L') return "ALL APPS - 55 APPS";
    return "OPENED - PRESS ESC TO CLOSE";
}

typedef enum { MODE_WINDOW, MODE_TERMINAL, MODE_CALC, MODE_NOTEPAD, MODE_PAINT, MODE_JPEG, MODE_ALLAPPS, MODE_SETTINGS, MODE_GAMES, MODE_PONG, MODE_MINES, MODE_SNAKE, MODE_RAINBOW, MODE_CLOCK, MODE_FILES } gui_mode_t;
static gui_mode_t gui_mode = MODE_WINDOW;
static int gui_win_visible = 1;
static const char *gui_win_title = "FISIXOS";
static const char *gui_win_lines[8] = { "HELLO FROM FISIXOS", "THIS IS A REAL GUI", "NOT ASCII - PIXELS", "TASKBAR BELOW", "CLICK START OR APPS", "", "", "" };
static int gui_win_count = 5;
static int gui_allapps_page = 0;
static int gui_win_x = 160, gui_win_y = 80, gui_win_w = 640, gui_win_h = 320;
static int gui_win_norm_x = 160, gui_win_norm_y = 80, gui_win_norm_w = 640, gui_win_norm_h = 320;
static int gui_dragging = 0, gui_drag_off_x = 0, gui_drag_off_y = 0;
static int gui_resizing = 0;
static int gui_snap_state = 0;
static uint32_t gui_bg_col = COL_DESKTOP;
static int gui_bg_idx = 0;
static int gui_rainbow_on = 1;
static const uint32_t gui_bg_palette[6] = { 0x002E7DC8u, 0x006B21A8u, 0x000F766Eu, 0x00165E2Bu, 0x009D174Du, 0x001A202Cu };
static const char *gui_bg_names[6] = { "BG BLUE", "BG PURPLE", "BG TEAL", "BG GREEN", "BG PINK", "BG BLACK" };
static const uint32_t gui_accent_palette[6] = { 0x006B21A8u, 0x00805AD5u, 0x000078D4u, 0x00C0392Bu, 0x0038A169u, 0x00ED64A6u };
static const char *gui_accent_names[6] = { "ACC DARK PURPLE", "ACC PURPLE", "ACC BLUE", "ACC RED", "ACC GREEN", "ACC PINK" };
static int gui_accent_idx = 0;
static const char *gui_desktop[15] = { "TERMINAL","FILES","NANO","CALCULATOR","NOTEPAD","PAINT","JPEG VIEWER","SETTINGS","CLOCK","ALL APPS","BROWSER","MUSIC","BROWSY","RAINBOW","CLIPONG" };
static int gui_desktop_icons_on = 0;
static int gui_menu_open = 0;
static const uint32_t gui_icon_col[15] = { 0x00E53E3Eu, 0x00DD6B20u, 0x00D69E2Eu, 0x0038A169u, 0x003183C9u, 0x006B46C1u, 0x00805AD5u, 0x00ED64A6u, 0x0038B2ACu, 0x00ED8936u, 0x00ECC94Bu, 0x0038A169u, 0x00E53E3Eu, 0x003183C9u, 0x00DD6B20u };
static int gui_pong_bx=400, gui_pong_by=200, gui_pong_vx=4, gui_pong_vy=3, gui_pong_py=200, gui_pong_ay=200, gui_pong_sl=0, gui_pong_sr=0, gui_pong_started=0, gui_pong_tick=0;
static void gui_pong_reset(void) { gui_pong_bx=(int)gui_w/2; gui_pong_by=(int)gui_h/2; gui_pong_vx=4; gui_pong_vy=3; gui_pong_sl=0; gui_pong_sr=0; gui_pong_started=0; }
static int gui_mines_mine[81], gui_mines_rev[81], gui_mines_flag[81], gui_mines_over=0, gui_mines_won=0, gui_mines_flag_mode=0;
static uint32_t gui_mines_seed = 0x12345678u;
static void gui_mines_init(void)
{
    int i, placed=0;
    for (i=0;i<81;i++) { gui_mines_mine[i]=0; gui_mines_rev[i]=0; gui_mines_flag[i]=0; }
    gui_mines_over=0; gui_mines_won=0; gui_mines_flag_mode=0; gui_mines_seed += 0x9E3779B9u;
    while (placed < 10) { gui_mines_seed = gui_mines_seed * 1664525u + 1013904223u; int c = (int)((gui_mines_seed >> 16) % 81); if (!gui_mines_mine[c]) { gui_mines_mine[c]=1; placed++; } }
}
static int gui_mines_count(int c)
{
    int x=c%9, y=c/9, n=0, dx, dy;
    for (dy=-1;dy<=1;dy++) for (dx=-1;dx<=1;dx++) { int nx=x+dx, ny=y+dy; if (nx>=0&&nx<9&&ny>=0&&ny<9&&gui_mines_mine[ny*9+nx]) n++; }
    return n;
}
static void gui_mines_reveal(int c)
{
    int stack[81], sp=0, i;
    if (gui_mines_over || gui_mines_rev[c] || gui_mines_flag[c]) return;
    if (gui_mines_mine[c]) { gui_mines_over=1; for (i=0;i<81;i++) if (gui_mines_mine[i]) gui_mines_rev[i]=1; return; }
    stack[sp++]=c;
    while (sp>0) { int cur=stack[--sp]; int x=cur%9, y=cur/9, dx, dy; if (gui_mines_rev[cur]) continue; gui_mines_rev[cur]=1; if (gui_mines_count(cur)!=0) continue; for (dy=-1;dy<=1;dy++) for (dx=-1;dx<=1;dx++) { int nx=x+dx, ny=y+dy, nc; if (nx<0||nx>=9||ny<0||ny>=9) continue; nc=ny*9+nx; if (!gui_mines_rev[nc] && !gui_mines_flag[nc] && sp<81) stack[sp++]=nc; } }
    { int rev=0; for (i=0;i<81;i++) if (gui_mines_rev[i] && !gui_mines_mine[i]) rev++; if (rev==71) gui_mines_won=1; }
}
static int gui_snake_x[64], gui_snake_y[64], gui_snake_len=3, gui_snake_dir=1, gui_snake_fx=10, gui_snake_fy=5, gui_snake_tick=0, gui_snake_over=0, gui_snake_score=0;
static void gui_snake_init(void) { int i; gui_snake_len=3; gui_snake_dir=1; gui_snake_over=0; gui_snake_score=0; for (i=0;i<3;i++) { gui_snake_x[i]=5-i; gui_snake_y[i]=5; } gui_snake_fx=10; gui_snake_fy=5; }

static char gui_input[128]; static int gui_input_len = 0;
static char gui_term_hist[16][128];
static int gui_term_hist_count = 0, gui_term_hist_nav = -1;
static void gui_term_hist_save(void)
{
    int i;
    if (!gui_input_len) return;
    if (gui_term_hist_count > 0) {
        int same = 1, j;
        for (j = 0; gui_input[j] && gui_term_hist[gui_term_hist_count-1][j]; j++) if (gui_input[j] != gui_term_hist[gui_term_hist_count-1][j]) same = 0;
        if (same && gui_input[j] == 0 && gui_term_hist[gui_term_hist_count-1][j] == 0) return;
    }
    if (gui_term_hist_count < 16) { int j=0; while (gui_input[j] && j<120) { gui_term_hist[gui_term_hist_count][j]=gui_input[j]; j++; } gui_term_hist[gui_term_hist_count][j]=0; gui_term_hist_count++; }
    else { for (i=0;i<15;i++) { int j=0; while (gui_term_hist[i+1][j]) { gui_term_hist[i][j]=gui_term_hist[i+1][j]; j++; } gui_term_hist[i][j]=0; } { int j=0; while (gui_input[j] && j<120) { gui_term_hist[15][j]=gui_input[j]; j++; } gui_term_hist[15][j]=0; } }
    gui_term_hist_nav = -1;
}
static void gui_term_hist_up(void)
{
    int j;
    if (!gui_term_hist_count) return;
    if (gui_term_hist_nav < 0) gui_term_hist_nav = gui_term_hist_count - 1;
    else if (gui_term_hist_nav > 0) gui_term_hist_nav--;
    gui_input_len = 0; for (j=0; gui_term_hist[gui_term_hist_nav][j] && j<120; j++) { gui_input[j]=gui_term_hist[gui_term_hist_nav][j]; gui_input_len++; } gui_input[gui_input_len]=0;
}
static void gui_term_hist_down(void)
{
    int j;
    if (gui_term_hist_nav < 0) return;
    gui_term_hist_nav++;
    if (gui_term_hist_nav >= gui_term_hist_count) { gui_term_hist_nav = -1; gui_input_len=0; gui_input[0]=0; return; }
    gui_input_len = 0; for (j=0; gui_term_hist[gui_term_hist_nav][j] && j<120; j++) { gui_input[j]=gui_term_hist[gui_term_hist_nav][j]; gui_input_len++; } gui_input[gui_input_len]=0;
}
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

static void gui_short_name(const char *name, char *out)
{
    int i=0;
    while (name[i] && i<5) { out[i]=name[i]; i++; }
    if (name[i]) { out[i++]='.'; out[i++]='.'; out[i++]='.'; }
    out[i]=0;
}
static void gui_button(int x,int y,int w,int h,const char *label,uint32_t col)
{
    int lum = (int)((col>>16)&255) + (int)((col>>8)&255) + (int)(col&255);
    gui_fill_rounded(x, y, w, h, gui_s(8), col);
    gui_text(x+gui_s(10), y+gui_s(8), label, lum < 380 ? COL_WHITE : COL_BLACK, COL_TRANSPARENT);
}
static void gui_draw_rainbow_stripe(int y)
{
    static const uint32_t cols[7] = { COL_RAINBOW_R, COL_RAINBOW_O, COL_RAINBOW_Y, COL_RAINBOW_G, COL_RAINBOW_B, COL_RAINBOW_I, COL_RAINBOW_V };
    int i; for (i=0;i<7;i++) gui_fill(0, y+i*3, (int)gui_w, 3, cols[i]);
}
static void gui_draw_fisix_logo(int x,int y)
{
    gui_fill(x, y, gui_s(4), gui_s(20), COL_RAINBOW_G);
    gui_fill(x, y, gui_s(16), gui_s(4), COL_RAINBOW_R);
    gui_fill(x, y+gui_s(8), gui_s(12), gui_s(4), COL_RAINBOW_Y);
    gui_fill(x, y+gui_s(16), gui_s(4), gui_s(4), COL_RAINBOW_B);
}
static void gui_snap_apply(int state)
{
    if (state == 0) { gui_win_x=gui_win_norm_x; gui_win_y=gui_win_norm_y; gui_win_w=gui_win_norm_w; gui_win_h=gui_win_norm_h; }
    else if (state == 1) { gui_win_x=0; gui_win_y=0; gui_win_w=(int)gui_w/2; gui_win_h=(int)gui_h-gui_taskbar_h; }
    else if (state == 2) { gui_win_x=(int)gui_w/2; gui_win_y=0; gui_win_w=(int)gui_w/2; gui_win_h=(int)gui_h-gui_taskbar_h; }
    else { gui_win_x=0; gui_win_y=0; gui_win_w=(int)gui_w; gui_win_h=(int)gui_h-gui_taskbar_h; }
    gui_snap_state = state;
}
static void gui_open_app(const char *name)
{
    int i; for (i=0;i<55;i++) if (gui_apps[i]==name) break;
    gui_start_open=0; gui_win_visible=1; gui_input_len=0; gui_input[0]=0;
    gui_dragging=0; gui_resizing=0;
    if (name[0]=='M' && name[1]=='S') { gui_mode=MODE_TERMINAL; gui_win_title="MS-DOS"; gui_term_lines=0; gui_term_push("MS-DOS STYLE TERMINAL"); gui_term_push("BLACK - KERNEL ONLY"); gui_term_push("TYPE HELP - UP = HISTORY"); return; }
    if (name[0]=='P' && name[1]=='O' && name[2]=='N') { gui_mode=MODE_PONG; gui_win_title="PONG - FULL SCREEN"; gui_win_norm_x=gui_win_x; gui_win_norm_y=gui_win_y; gui_win_norm_w=gui_win_w; gui_win_norm_h=gui_win_h; gui_snap_apply(3); gui_pong_reset(); return; }
    if ((name[0]=='B' && name[1]=='R') || (name[0]=='R' && name[1]=='E') || (name[0]=='N' && name[1]=='A') || (name[0]=='P' && name[1]=='A' && name[2]=='T') || (name[0]=='C' && name[1]=='L') || (name[0]=='Z' && name[1]==0)) { gui_run_fse(name); return; }
    if (name[0]=='M' && name[1]=='I' && name[2]=='N') { gui_mode=MODE_MINES; gui_win_title="MINESWEEPER"; gui_snap_apply(0); gui_mines_init(); return; }
    if (name[0]=='S' && name[1]=='N') { gui_mode=MODE_SNAKE; gui_win_title="SNAKE"; gui_snap_apply(0); gui_snake_init(); return; }
    if (name[0]=='R' && name[1]=='A' && name[2]=='I') { gui_mode=MODE_RAINBOW; gui_win_title="RAINBOW"; gui_snap_apply(0); return; }
    if (name[0]=='S' && name[1]=='E' && name[2]=='T') { gui_mode=MODE_SETTINGS; gui_win_title="SETTINGS"; gui_snap_apply(0); return; }
    if (name[0]=='G' && name[1]=='A') { gui_mode=MODE_GAMES; gui_win_title="GAMES"; gui_snap_apply(0); return; }
    if (name[0]=='C' && name[1]=='L' && name[2]=='O') { gui_mode=MODE_CLOCK; gui_win_title="CLOCK"; gui_snap_apply(0); return; }
    if (name[0]=='F' && name[1]=='I' && name[2]=='L') { gui_mode=MODE_FILES; gui_win_title="FILES"; gui_snap_apply(0); return; }
    if (name[0]=='T' && name[1]=='E') { gui_mode=MODE_TERMINAL; gui_win_title="TERMINAL"; gui_term_lines=0; gui_term_push("FISIX TERMINAL V2"); gui_term_push("TYPE HELP"); return; }
    if (name[0]=='C' && name[1]=='A') { gui_mode=MODE_CALC; gui_win_title="CALCULATOR"; gui_calc_result=0; return; }
    if (name[0]=='N' && name[1]=='O') { gui_mode=MODE_NOTEPAD; gui_win_title="NOTEPAD"; return; }
    if (name[0]=='P' && name[1]=='A' && name[2]=='I') { gui_mode=MODE_PAINT; gui_win_title="PAINT"; return; }
    if (name[0]=='J') { gui_mode=MODE_JPEG; gui_win_title="JPEG VIEWER"; return; }
    if (name[0]=='A' && name[1]=='L') { gui_mode=MODE_ALLAPPS; gui_win_title="ALL APPS"; gui_allapps_page=0; return; }
    gui_mode=MODE_WINDOW; gui_win_title=name; gui_win_lines[0]=gui_app_desc(name); gui_win_lines[1]="WINDOW OPENED - CLICK X"; gui_win_lines[2]="OR PRESS ESC TO CLOSE"; gui_win_lines[3]="ARROWS MOVE - ENTER CLICKS"; gui_win_count=4;
}
#define GUI_CUR_BOX 64
static uint32_t gui_cursor_saved[GUI_CUR_BOX * GUI_CUR_BOX];
static int gui_cursor_saved_x = 0, gui_cursor_saved_y = 0, gui_cursor_saved_valid = 0;
static void gui_save_cursor_box(void)
{
    int dx, dy;
    gui_cursor_saved_x = gui_mx - 2;
    gui_cursor_saved_y = gui_my - 2;
    for (dy = 0; dy < GUI_CUR_BOX; dy++)
        for (dx = 0; dx < GUI_CUR_BOX; dx++) {
            int x = gui_cursor_saved_x + dx, y = gui_cursor_saved_y + dy;
            gui_cursor_saved[dy * GUI_CUR_BOX + dx] =
                (x >= 0 && x < (int)gui_w && y >= 0 && y < (int)gui_h)
                ? gui_fb[(uint32_t)y * gui_pitch + (uint32_t)x] : 0;
        }
    gui_cursor_saved_valid = 1;
}
static void gui_restore_cursor_box(void)
{
    int dx, dy;
    if (!gui_cursor_saved_valid) return;
    for (dy = 0; dy < GUI_CUR_BOX; dy++)
        for (dx = 0; dx < GUI_CUR_BOX; dx++) {
            int x = gui_cursor_saved_x + dx, y = gui_cursor_saved_y + dy;
            if (x >= 0 && x < (int)gui_w && y >= 0 && y < (int)gui_h)
                gui_fb[(uint32_t)y * gui_pitch + (uint32_t)x] =
                    gui_cursor_saved[dy * GUI_CUR_BOX + dx];
        }
    gui_cursor_saved_valid = 0;
}
static void gui_draw_cursor_shape(void)
{
    static const char *arrow[12] = {
        "X", "XX", "XXX", "XXXX", "XXXXX", "XXXXXX",
        "XXXXXXX", "XXXX", "XX.XXX", "X..XXX", "....XXX", ".....XXX",
    };
    int x, y, ox, oy, sx, sy;
    for (y = 0; y < 12; y++)
        for (x = 0; arrow[y][x]; x++)
            if (arrow[y][x] == 'X')
                for (oy = -1; oy <= 1; oy++)
                    for (ox = -1; ox <= 1; ox++)
                        for (sy = 0; sy < gui_font_px; sy++)
                            for (sx = 0; sx < gui_font_px; sx++)
                                gui_put(gui_mx + x * gui_font_px + ox + sx, gui_my + y * gui_font_px + oy + sy, COL_BLACK);
    for (y = 0; y < 12; y++)
        for (x = 0; arrow[y][x]; x++)
            if (arrow[y][x] == 'X')
                for (sy = 0; sy < gui_font_px; sy++)
                    for (sx = 0; sx < gui_font_px; sx++)
                        gui_put(gui_mx + x * gui_font_px + sx, gui_my + y * gui_font_px + sy, COL_WHITE);
}
static void gui_draw_cursor(void)
{
    gui_save_cursor_box();
    gui_draw_cursor_shape();
}
static int gui_hover_code(void)
{
    int i, code = 0;
    if (gui_desktop_icons_on) for (i = 0; i < 15; i++) {
        int x = gui_s(16) + (i % 4) * gui_s(120), y = gui_s(120) + (i / 4) * gui_s(110);
        if (gui_mx >= x && gui_mx < x + gui_s(64) && gui_my >= y && gui_my < y + gui_s(64)) code = i + 1;
    }
    return code;
}
static void gui_move_to(int nx, int ny)
{
    int old_hover, new_hover, paint_px = -1, paint_py = 0;
    if (!gui_ready) return;
    if (nx < 0) nx = 0; if (ny < 0) ny = 0;
    if (nx >= (int)gui_w) nx = (int)gui_w - 1; if (ny >= (int)gui_h) ny = (int)gui_h - 1;
    if (nx == gui_mx && ny == gui_my) return;
    old_hover = gui_hover_code();
    gui_restore_cursor_box();
    gui_mx = nx; gui_my = ny;
    if (gui_mode == MODE_PAINT && gui_prev_left) {
        int wx = gui_win_x, wy = gui_win_y;
        paint_px = gui_mx - (wx + 24); paint_py = gui_my - (wy + 64);
        if (paint_px >= 0 && paint_px < 320 && paint_py >= 0 && paint_py < 200)
            gui_paint[paint_py * 320 + paint_px] = 1;
        else paint_px = -1;
    }
    if (gui_dragging) {
        int ox=gui_win_x, oy=gui_win_y, ow=gui_win_w, oh=gui_win_h;
        gui_win_x = gui_mx - gui_drag_off_x; gui_win_y = gui_my - gui_drag_off_y;
        if (gui_win_x < 0) gui_win_x = 0; if (gui_win_y < 0) gui_win_y = 0;
        if (gui_win_x > (int)gui_w - 100) gui_win_x = (int)gui_w - 100; if (gui_win_y > (int)gui_h - 100) gui_win_y = (int)gui_h - 100;
        gui_redraw_window_move(ox, oy, ow, oh); return;
    }
    if (gui_resizing) {
        gui_win_w = gui_mx - gui_win_x; gui_win_h = gui_my - gui_win_y;
        if (gui_win_w < 320) gui_win_w = 320; if (gui_win_h < 200) gui_win_h = 200;
        if (gui_win_x + gui_win_w > (int)gui_w) gui_win_w = (int)gui_w - gui_win_x; if (gui_win_y + gui_win_h > (int)gui_h - gui_taskbar_h) gui_win_h = (int)gui_h - gui_taskbar_h - gui_win_y;
        gui_draw_window_only(); return;
    }
    if (gui_mode == MODE_PONG) { gui_pong_py = gui_my - 80; if (gui_pong_py < 0) gui_pong_py = 0; if (gui_pong_py > (int)gui_h - 120) gui_pong_py = (int)gui_h - 120; }
    new_hover = gui_hover_code();
    if (new_hover != old_hover) { gui_restore_cursor_box(); gui_draw_desktop_icons(); gui_draw_taskbar(); gui_save_cursor_box(); gui_draw_cursor_shape(); return; }
    if (paint_px >= 0) gui_put(gui_win_x + 24 + paint_px, gui_win_y + 64 + paint_py, COL_RED);
    gui_save_cursor_box();
    gui_draw_cursor_shape();
}
static void gui_draw_desktop_icons(void)
{
    int i;
    for (i=0;i<15;i++) {
        int x = gui_s(16) + (i % 4) * gui_s(120); int y = gui_s(120) + (i / 4) * gui_s(110);
        int sz = gui_s(64);
        int hover = (gui_mx >= x && gui_mx < x+sz && gui_my >= y && gui_my < y+sz);
        char shortn[10]; gui_short_name(gui_desktop[i], shortn);
        gui_fill(x+gui_s(4), y+gui_s(4), sz, sz, COL_W11_SHADOW);
        gui_fill_rounded(x, y, sz, sz, gui_s(16), hover ? COL_W11_ACCENT : gui_icon_col[i]);
        gui_text(x+gui_s(8), y+gui_s(24), gui_desktop[i][0]==0 ? "?" : (char[2]){gui_desktop[i][0],0}, COL_WHITE, COL_TRANSPARENT);
        gui_text(x, y+sz+gui_s(6), shortn, COL_WHITE, COL_TRANSPARENT);
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
static int gui_taskbar_btn_w(void) { return gui_s(120); }
static int gui_taskbar_btn_h(void) { return gui_s(32); }
static int gui_taskbar_start_x(void) { int total = 5 * gui_taskbar_btn_w() + 4 * gui_s(8); return ((int)gui_w - total) / 2; }
static int gui_taskbar_btn_x(int idx) { return gui_taskbar_start_x() + idx * (gui_taskbar_btn_w() + gui_s(8)); }
static void gui_draw_taskbar(void)
{
    int ty = (int)gui_h - gui_taskbar_h; int i, bw = gui_taskbar_btn_w(), bh = gui_taskbar_btn_h(), by = ty + (gui_taskbar_h - bh) / 2;
    gui_fill(0, ty, (int)gui_w, gui_taskbar_h, COL_W11_TASK);
    { const char *labels[5] = {"START","TERMINAL","FILES","NANO","MS-DOS"}; for (i=0;i<5;i++) { int bx = gui_taskbar_btn_x(i); int hov = (gui_mx >= bx && gui_mx < bx+bw && gui_my >= by && gui_my < by+bh); gui_fill_rounded(bx, by, bw, bh, gui_s(8), hov ? COL_W11_ACCENT : COL_W11_TASK_BTN); if (i==0) { gui_draw_fisix_logo(bx+gui_s(10), by+gui_s(6)); gui_text(bx+gui_s(34), by+gui_s(8), "START", COL_WHITE, COL_TRANSPARENT); } else gui_text(bx+gui_s(10), by+gui_s(8), labels[i], COL_WHITE, COL_TRANSPARENT); } }
    { int tw = gui_s(180), tx = (int)gui_w - tw - gui_s(12); gui_fill_rounded(tx, by, tw, bh, gui_s(8), COL_W11_TASK_BTN); rtc_time_t rt; if (generic_rtc_read(&rt)) { char tb[8]; tb[0]=(char)('0'+rt.hour/10); tb[1]=(char)('0'+rt.hour%10); tb[2]=':'; tb[3]=(char)('0'+rt.minute/10); tb[4]=(char)('0'+rt.minute%10); tb[5]=0; gui_text(tx+gui_s(12), by+gui_s(8), tb, COL_WHITE, COL_TRANSPARENT); } else gui_text(tx+gui_s(12), by+gui_s(8), "FISIXOS", COL_WHITE, COL_TRANSPARENT); }
    if (gui_start_open) {
        int mw = gui_s(320), mx = ((int)gui_w - mw) / 2, mh = gui_s(300), my = ty - mh - gui_s(8); const char *items[6] = {"TERMINAL","FILES","NANO","CALCULATOR","ALL APPS","SHUTDOWN"};
        gui_fill(mx+gui_s(4), my+gui_s(4), mw, mh, COL_W11_SHADOW);
        gui_fill_rounded(mx, my, mw, mh, gui_s(12), COL_WHITE);
        for (i=0;i<6;i++) { int iy = my + gui_s(12) + i*gui_s(42); gui_text(mx+gui_s(16), iy, items[i], COL_BLACK, COL_TRANSPARENT); if (i<5) gui_fill(mx+gui_s(12), iy+gui_s(32), mw-gui_s(24), 1, COL_W11_GREY); }
    }
}
static void gui_draw_window_content(void)
{
    int wx=gui_win_x, wy=gui_win_y, ww=gui_win_w, wh=gui_win_h, i;
    if (gui_mode==MODE_SETTINGS) {
        gui_text(wx+24, wy+64, "PICK A BACKGROUND COLOUR", COL_BLACK, COL_WINDOW);
        for (i=0;i<6;i++) { gui_button(wx+24, wy+96+i*36, 200, 32, gui_bg_names[i], gui_bg_palette[i]); }
        gui_button(wx+260, wy+96, 200, 32, gui_rainbow_on ? "RAINBOW ON" : "RAINBOW OFF", COL_YELLOW);
        gui_text(wx+260, wy+136, "RAINBOW = SISTER", COL_BLACK, COL_WINDOW);
        gui_text(wx+260, wy+176, "PICK AN ACCENT COLOUR", COL_BLACK, COL_WINDOW);
        for (i=0;i<6;i++) { gui_button(wx+260, wy+208+i*36, 200, 32, gui_accent_names[i], gui_accent_palette[i]); }
        gui_fill(wx+260, wy+208+6*36+8, 200, 16, gui_accent_col);
        return;
    }
    if (gui_mode==MODE_GAMES) {
        gui_text(wx+24, wy+64, "GAMES - PICK ONE", COL_BLACK, COL_WINDOW);
        gui_button(wx+24, wy+96, 200, 32, "PONG", COL_CYAN);
        gui_button(wx+24, wy+136, 200, 32, "MINESWEEPER", COL_YELLOW);
        gui_button(wx+24, wy+176, 200, 32, "SNAKE", COL_GREEN);
        gui_button(wx+24, wy+216, 200, 32, "RAINBOW", COL_PINK);
        gui_text(wx+260, wy+96, "PONG IS FULL SCREEN", COL_BLACK, COL_WINDOW);
        gui_text(wx+260, wy+136, "MINES IS 9X9 10 MINES", COL_BLACK, COL_WINDOW);
        return;
    }
    if (gui_mode==MODE_RAINBOW) {
        static const uint32_t cols[7] = { COL_RAINBOW_R, COL_RAINBOW_O, COL_RAINBOW_Y, COL_RAINBOW_G, COL_RAINBOW_B, COL_RAINBOW_I, COL_RAINBOW_V };
        for (i=0;i<7;i++) gui_fill(wx+24, wy+64+i*28, ww-48, 28, cols[i]);
        gui_text(wx+24, wy+64+7*28+16, "RAINBOW", COL_BLACK, COL_WINDOW);
        return;
    }
    if (gui_mode==MODE_CLOCK) {
        rtc_time_t rt;
        gui_text(wx+24, wy+64, "CLOCK - CMOS RTC", COL_BLACK, COL_WINDOW);
        if (generic_rtc_read(&rt)) {
            char tb[16], db[16];
            tb[0]=(char)('0'+rt.hour/10); tb[1]=(char)('0'+rt.hour%10); tb[2]=':'; tb[3]=(char)('0'+rt.minute/10); tb[4]=(char)('0'+rt.minute%10); tb[5]=':'; tb[6]=(char)('0'+rt.second/10); tb[7]=(char)('0'+rt.second%10); tb[8]=0;
            db[0]=(char)('0'+rt.day/10); db[1]=(char)('0'+rt.day%10); db[2]='/'; db[3]=(char)('0'+rt.month/10); db[4]=(char)('0'+rt.month%10); db[5]='/'; db[6]=(char)('0'+(rt.year/1000)%10); db[7]=(char)('0'+(rt.year/100)%10); db[8]=(char)('0'+(rt.year/10)%10); db[9]=(char)('0'+rt.year%10); db[10]=0;
            gui_text(wx+24, wy+100, tb, COL_BLACK, COL_WINDOW);
            gui_text(wx+24, wy+136, db, COL_BLACK, COL_WINDOW);
            gui_text(wx+24, wy+172, "FROM CMOS BATTERY", COL_BLACK, COL_WINDOW);
        } else {
            gui_text(wx+24, wy+100, "CMOS RTC NOT READ", COL_BLACK, COL_WINDOW);
            gui_text(wx+24, wy+136, "CHECK CMOS BATTERY", COL_BLACK, COL_WINDOW);
        }
        gui_button(wx+24, wy+208, 200, 32, "REFRESH", COL_CYAN);
        return;
    }
    if (gui_mode==MODE_FILES) {
        gui_text(wx+24, wy+64, "FILES - BUILTIN FSE", COL_BLACK, COL_WINDOW);
        gui_text(wx+24, wy+100, "BROWSY.FSE", COL_BLACK, COL_WINDOW);
        gui_text(wx+24, wy+136, "CLIPONG.FSE", COL_BLACK, COL_WINDOW);
        gui_text(wx+24, wy+172, "NANO.FSE", COL_BLACK, COL_WINDOW);
        gui_text(wx+24, wy+208, "PATHIFY.FSE", COL_BLACK, COL_WINDOW);
        gui_text(wx+24, wy+244, "RETERM.FSE Z.FSE", COL_BLACK, COL_WINDOW);
        return;
    }
    if (gui_mode==MODE_PONG) {
        int fx=wx, fy=wy+40, fw=ww, fh=wh-40;
        gui_fill(fx, fy, fw, fh, COL_BLACK);
        gui_fill(fx+16, fy+gui_pong_py, 16, 80, COL_WHITE);
        gui_fill(fx+fw-32, fy+gui_pong_ay, 16, 80, COL_WHITE);
        gui_fill(fx+gui_pong_bx, fy+gui_pong_by, 16, 16, COL_WHITE);
        { char s[16]; s[0]=(char)('0'+(gui_pong_sl%10)); s[1]='-'; s[2]=(char)('0'+(gui_pong_sr%10)); s[3]=0; gui_text(fx+fw/2-24, fy+16, s, COL_WHITE, COL_BLACK); }
        if (!gui_pong_started) gui_text(fx+fw/2-160, fy+fh/2, "ENTER TO START PONG", COL_WHITE, COL_BLACK);
        else gui_text(fx+16, fy+16, "PONG", COL_WHITE, COL_BLACK);
        return;
    }
    if (gui_mode==MODE_MINES) {
        int gx=wx+24, gy=wy+96, cx, cy;
        gui_text(wx+24, wy+64, gui_mines_won ? "YOU WIN" : (gui_mines_over ? "BOOM - GAME OVER" : (gui_mines_flag_mode ? "FLAG MODE ON" : "CLICK A CELL")), COL_BLACK, COL_WINDOW);
        gui_button(wx+360, wy+56, 140, 32, "FLAG MODE", COL_YELLOW);
        gui_button(wx+520, wy+56, 100, 32, "RESTART", COL_CYAN);
        for (cy=0;cy<9;cy++) for (cx=0;cx<9;cx++) { int c=cy*9+cx, x=gx+cx*32, y=gy+cy*32;
            if (gui_mines_rev[c]) { gui_fill(x, y, 30, 30, COL_WHITE); gui_border(x, y, 30, 30, COL_BLACK); if (gui_mines_mine[c]) gui_text(x+8, y+8, "X", COL_RED, COL_WHITE); else if (gui_mines_count(c)>0) { char n[2]; n[0]=(char)('0'+gui_mines_count(c)); n[1]=0; gui_text(x+8, y+8, n, COL_BLACK, COL_WHITE); } }
            else if (gui_mines_flag[c]) { gui_fill(x, y, 30, 30, COL_YELLOW); gui_border(x, y, 30, 30, COL_BLACK); gui_text(x+8, y+8, "F", COL_BLACK, COL_YELLOW); }
            else { gui_fill(x, y, 30, 30, COL_BUTTON_HI); gui_border(x, y, 30, 30, COL_BLACK); } }
        return;
    }
    if (gui_mode==MODE_SNAKE) {
        int sx=wx+24, sy=wy+64, j;
        gui_fill(sx, sy, 320, 320, COL_BLACK);
        if (gui_snake_over) gui_text(sx+16, sy+16, "GAME OVER - ENTER", COL_WHITE, COL_BLACK);
        else { gui_fill(sx+gui_snake_fx*20, sy+gui_snake_fy*20, 18, 18, COL_RED); for (j=0;j<gui_snake_len;j++) gui_fill(sx+gui_snake_x[j]*20, sy+gui_snake_y[j]*20, 18, 18, COL_GREEN); }
        gui_text(wx+360, wy+64, "SNAKE - ARROWS", COL_BLACK, COL_WINDOW);
        return;
    }
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
        if (gui_mode==MODE_TERMINAL) { gui_fill(wx, wy+gui_s(40), ww, wh-gui_s(40), COL_BLACK); for (i=0;i<gui_term_lines;i++) gui_text(wx+24, wy+64+i*28, gui_term_out[i], COL_WHITE, COL_BLACK); gui_text(wx+24, wy+64+6*28, "> ", COL_GREEN, COL_BLACK); gui_text(wx+56, wy+64+6*28, gui_input, COL_WHITE, COL_BLACK); gui_text(wx+24, wy+64+7*28, "UP = LAST COMMAND", COL_GREEN, COL_BLACK); }
        else if (gui_mode==MODE_CALC) { char buf[32]; int j=0; long v=gui_calc_result; char tmp[24]; int k=0; if (v==0) tmp[k++]='0'; else { char rev[24]; int r=0; long vv=v; if (vv<0) { tmp[k++]='-'; vv=-vv; } while (vv>0 && r<20) { rev[r++]=(char)('0'+vv%10); vv/=10; } while (r>0) tmp[k++]=rev[--r]; } tmp[k]=0; (void)buf; (void)j; gui_text(wx+24, wy+64, "TYPE 2+3 ENTER", COL_BLACK, COL_WINDOW); gui_text(wx+24, wy+100, gui_input, COL_BLACK, COL_WINDOW); gui_text(wx+24, wy+136, "RESULT", COL_BLACK, COL_WINDOW); gui_text(wx+24, wy+172, tmp, COL_BLACK, COL_WINDOW); }
        else { gui_text(wx+24, wy+64, "TYPE TEXT", COL_BLACK, COL_WINDOW); gui_text(wx+24, wy+100, gui_note, COL_BLACK, COL_WINDOW); }
        return;
    }
    for (i=0;i<gui_win_count;i++) gui_text(wx+24, wy+64+i*36, gui_win_lines[i], COL_BLACK, COL_WINDOW);
    gui_button(wx+24, wy+wh-48, 120, 32, "CLOSE", COL_RED);
    gui_button(wx+160, wy+wh-48, 160, 32, "BACK TO GAMES", COL_CYAN);
}
static void gui_draw_window(void)
{
    int wx=gui_win_x, wy=gui_win_y, ww=gui_win_w, wh=gui_win_h, th=gui_s(40);
    if (!gui_win_visible) return;
    gui_fill(wx+gui_s(4), wy+gui_s(4), ww, wh, COL_W11_SHADOW);
    gui_fill_rounded(wx, wy, ww, wh, gui_s(12), COL_WINDOW);
    gui_fill(wx, wy+th, ww, 1, COL_W11_GREY);
    gui_text(wx+gui_s(16), wy+gui_s(12), gui_win_title, COL_BLACK, COL_TRANSPARENT);
    if (gui_dragging) gui_text(wx+ww-gui_s(260), wy+gui_s(12), "DRAGGING", COL_BLACK, COL_TRANSPARENT);
    gui_fill_rounded(wx+ww-gui_s(64), wy+gui_s(8), gui_s(24), gui_s(24), gui_s(6), COL_W11_TITLE_BG); gui_text(wx+ww-gui_s(60), wy+gui_s(12), "S", COL_BLACK, COL_TRANSPARENT);
    gui_fill_rounded(wx+ww-gui_s(36), wy+gui_s(8), gui_s(24), gui_s(24), gui_s(6), COL_RED); gui_text(wx+ww-gui_s(32), wy+gui_s(12), "X", COL_WHITE, COL_TRANSPARENT);
    gui_fill_rounded(wx+ww-gui_s(24), wy+wh-gui_s(24), gui_s(24), gui_s(24), gui_s(6), COL_W11_GREY);
    gui_draw_window_content();
}
/* Dirty-rect helpers, 2026-10-30: opening a window, selecting
 * an app, or typing in one used to call gui_draw(), repainting
 * the whole screen. Now most updates redraw only the area
 * that actually changed - the window, its content, the
 * taskbar, or the old window rect on a move/close - the
 * same idea as the cursor-only move fix. gui_draw() is
 * still used for the first draw and for a background
 * colour change, where the whole desktop really does change. */
static void gui_draw_window_only(void)
{
    if (!gui_ready || !gui_win_visible) return;
    gui_restore_cursor_box();
    gui_fill(gui_win_x, gui_win_y, gui_win_w, gui_win_h, COL_WINDOW);
    gui_draw_window();
    gui_save_cursor_box(); gui_draw_cursor_shape();
}
static void gui_draw_content_only(void)
{
    int wx=gui_win_x, wy=gui_win_y, ww=gui_win_w, wh=gui_win_h;
    if (!gui_ready || !gui_win_visible) return;
    gui_restore_cursor_box();
    gui_fill(wx, wy+gui_s(40), ww, wh-gui_s(40), COL_WINDOW);
    gui_draw_window_content();
    gui_save_cursor_box(); gui_draw_cursor_shape();
}
static void gui_draw_taskbar_only(void)
{
    if (!gui_ready) return;
    gui_restore_cursor_box(); gui_draw_taskbar(); gui_save_cursor_box(); gui_draw_cursor_shape();
}
static void gui_draw_close_window(void)
{
    int wx=gui_win_x, wy=gui_win_y, ww=gui_win_w, wh=gui_win_h;
    if (!gui_ready) return;
    gui_restore_cursor_box();
    gui_fill(wx, wy, ww, wh, gui_bg_col);
    if (gui_desktop_icons_on) gui_draw_desktop_icons();
    if (gui_rainbow_on) gui_draw_rainbow_stripe(gui_s(52));
    gui_save_cursor_box(); gui_draw_cursor_shape();
}
static void gui_redraw_window_move(int ox,int oy,int ow,int oh)
{
    if (!gui_ready) return;
    gui_restore_cursor_box();
    gui_fill(ox, oy, ow, oh, gui_bg_col);
    if (gui_desktop_icons_on) gui_draw_desktop_icons();
    gui_draw_window();
    gui_save_cursor_box(); gui_draw_cursor_shape();
}
void gui_debug_clear(void)
{
    if (!gui_ready) return;
    gui_fill(0, 0, (int)gui_w, (int)gui_h, COL_DESKTOP);
}
void gui_debug_text(int x, int y, const char *s)
{
    if (!gui_ready) return;
    gui_text(x, y, s, COL_WHITE, COL_DESKTOP);
}
static int gui_dbg_next_y = 32;
void gui_debug_line(const char *s)
{
    if (!gui_ready) return;
    if (gui_dbg_next_y > (int)gui_h - 40) {
        gui_debug_clear();
        gui_text(32, 12, "FISIXOS BOOT DEBUG - LAST LINE = STUCK STAGE", COL_WHITE, COL_DESKTOP);
        gui_dbg_next_y = 32;
    }
    gui_text(32, gui_dbg_next_y, s, COL_WHITE, COL_DESKTOP);
    gui_dbg_next_y += 20;
}
void gui_init(uint64_t fb_base, uint32_t width, uint32_t height, uint32_t pitch)
{
    if (!fb_base || width==0 || height==0) return;
    gui_fb=(uint32_t*)(uintptr_t)fb_base; gui_w=width; gui_h=height; gui_pitch=pitch?pitch:width; gui_mx=(int)width/2; gui_my=(int)height/2; gui_ready=1; gui_cursor_saved_valid=0;
    if (width >= 3840) gui_font_px = 4; else if (width >= 2560) gui_font_px = 3; else if (width >= 800) gui_font_px = 2; else gui_font_px = 1;
    gui_taskbar_h = gui_s(48);
    gui_win_x = (int)width/4; gui_win_y = (int)height/6; gui_win_w = (int)width/2; gui_win_h = (int)height/2;
    gui_win_norm_x = gui_win_x; gui_win_norm_y = gui_win_y; gui_win_norm_w = gui_win_w; gui_win_norm_h = gui_win_h;
    gui_serial("[FisixOS] GUI init\n");
}
int gui_is_ready(void) { return gui_ready; }
uint32_t gui_width(void) { return gui_w; }
uint32_t gui_height(void) { return gui_h; }
static const char *gui_menu_item(int menu,int idx)
{
    static const char *file_m[6] = { "TERMINAL","MS-DOS","FILES","NANO","NOTEPAD","SHUTDOWN" };
    static const char *apps_m[7] = { "CALCULATOR","PAINT","JPEG VIEWER","CLOCK","BROWSER","MUSIC","ALL APPS" };
    static const char *games_m[6] = { "PONG","MINESWEEPER","SNAKE","RAINBOW","CHESS","TETRIS" };
    static const char *sett_m[8] = { "BG BLUE","BG PURPLE","BG TEAL","BG GREEN","BG PINK","BG BLACK","RAINBOW ON/OFF","ICONS ON/OFF" };
    static const char *help_m[3] = { "ABOUT FISIXOS","HELP","RAINBOW" };
    if (menu==1 && idx>=0 && idx<6) return file_m[idx];
    if (menu==2 && idx>=0 && idx<7) return apps_m[idx];
    if (menu==3 && idx>=0 && idx<6) return games_m[idx];
    if (menu==4 && idx>=0 && idx<8) return sett_m[idx];
    if (menu==5 && idx>=0 && idx<3) return help_m[idx];
    return 0;
}
static int gui_menu_count(int menu) { if (menu==1) return 6; if (menu==2) return 7; if (menu==3) return 6; if (menu==4) return 8; if (menu==5) return 3; return 0; }
static int gui_menu_x(int menu) { if (menu==1) return 16; if (menu==2) return 120; if (menu==3) return 224; if (menu==4) return 328; return 432; }
static void gui_draw_menu_bar(void)
{
    static const char *titles[5] = { "FILE","APPS","GAMES","SETTINGS","HELP" };
    int i, my = gui_s(76), mh = gui_s(32);
    gui_fill(0, my, (int)gui_w, mh, COL_W11_TASK);
    for (i=0;i<5;i++) { int x=gui_s(gui_menu_x(i+1)); if (gui_menu_open==i+1) gui_fill_rounded(x, my+gui_s(2), gui_s(100), mh-gui_s(4), gui_s(8), COL_W11_ACCENT); gui_text(x+gui_s(10), my+gui_s(8), titles[i], COL_WHITE, COL_TRANSPARENT); }
    if (gui_menu_open) {
        int mx=gui_s(gui_menu_x(gui_menu_open)), cnt=gui_menu_count(gui_menu_open), j, mw=gui_s(240), mh2=cnt*gui_s(36);
        gui_fill(mx+gui_s(4), my+mh+gui_s(4), mw, mh2, COL_W11_SHADOW);
        gui_fill_rounded(mx, my+mh, mw, mh2, gui_s(12), COL_WHITE);
        for (j=0;j<cnt;j++) { const char *s=gui_menu_item(gui_menu_open, j); if (s) gui_text(mx+gui_s(12), my+mh+gui_s(8)+j*gui_s(36), s, COL_BLACK, COL_TRANSPARENT); }
    }
}
static void gui_draw_menu_only(void)
{
    if (!gui_ready) return;
    gui_restore_cursor_box(); gui_fill(0, gui_s(76), (int)gui_w, gui_s(32)+gui_s(300), gui_bg_col); gui_draw_menu_bar(); gui_save_cursor_box(); gui_draw_cursor_shape();
}
void gui_draw(void)
{
    if (!gui_ready) return;
    { int yy; uint32_t top = gui_bg_col, bot = gui_shade(gui_bg_col, 3, 5); for (yy=0; yy<(int)gui_h; yy+=gui_s(8)) { int tr=((top>>16)&255), tg=((top>>8)&255), tb=(top&255), br=((bot>>16)&255), bg=((bot>>8)&255), bb=(bot&255); int tt=yy*255/(int)gui_h; uint32_t c=((uint32_t)(tr+(br-tr)*tt/255)<<16)|((uint32_t)(tg+(bg-tg)*tt/255)<<8)|(uint32_t)(tb+(bb-tb)*tt/255); gui_fill(0, yy, (int)gui_w, gui_s(8), c); } }
    gui_text(gui_s(32),gui_s(32),"FISIXOS",COL_WHITE,COL_TRANSPARENT);
    if (gui_rainbow_on) gui_draw_rainbow_stripe(gui_s(52));
    if (gui_w > 900) gui_text((int)gui_w-gui_s(560),gui_s(32),"ARROWS MOVE ENTER CLICKS",COL_WHITE,COL_TRANSPARENT);
    gui_draw_menu_bar();
    if (gui_desktop_icons_on) gui_draw_desktop_icons(); gui_draw_window(); gui_draw_taskbar(); gui_draw_cursor();
}
void gui_show_boot_logo(void)
{
    /* Boot logo, 2026-10-30, Asher's white logo, 400x400 master.
     * Display size: screen width / 6, clamped to 250-400, and
     * never larger than the screen or the 400 master, so it is
     * only ever downscaled, never upscaled and blurred.
     * 1920 wide -> 320, 2560 -> 400 (clamped), 3840 -> 400,
     * 1366 -> 250 (clamped), small screens -> fit screen.
     * Centred, aspect kept (square), black background, white
     * logo. Box-average downscale, so thin circle/gears do
     * not drop out like nearest-neighbour would at 250.
     * Shows about 2 seconds (CMOS RTC), then the caller draws
     * the normal GUI over it. */
    int size, x0, y0, dx, dy;
    if (!gui_ready) return;
    size = (int)gui_w / 6;
    if (size < 250) size = 250;
    if (size > 400) size = 400;
    if (size > (int)gui_w - 20) size = (int)gui_w - 20;
    if (size > (int)gui_h - 20) size = (int)gui_h - 20;
    if (size < 1) return;
    x0 = ((int)gui_w - size) / 2; y0 = ((int)gui_h - size) / 2;
    gui_fill(0, 0, (int)gui_w, (int)gui_h, COL_BLACK);
    for (dy = 0; dy < size; dy++) {
        int sy0 = dy * 400 / size, sy1 = (dy + 1) * 400 / size; int sy;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (dx = 0; dx < size; dx++) {
            int sx0 = dx * 400 / size, sx1 = (dx + 1) * 400 / size; int sx, sum = 0, cnt = 0;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            for (sy = sy0; sy < sy1; sy++) for (sx = sx0; sx < sx1; sx++) { sum += fisix_boot_logo[sy * 400 + sx]; cnt++; }
            if (cnt) { int g = sum / cnt; uint32_t c = ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g; gui_put(x0 + dx, y0 + dy, c); }
        }
    }
    /* Wait about 2 RTC seconds, with a poll timeout fallback
     * so a broken RTC cannot hang boot forever. */
    {
        rtc_time_t rt0, rt; int start = -1, waited = 0;
        if (generic_rtc_read(&rt0)) {
            start = rt0.minute * 60 + rt0.second;
            while (waited < 5000000) {
                int now, diff;
                if (!generic_rtc_read(&rt)) break;
                now = rt.minute * 60 + rt.second; diff = now - start; if (diff < 0) diff += 3600;
                if (diff >= 2) break;
                waited++;
            }
        } else {
            volatile int i; for (i = 0; i < 20000000; i++) { __asm__ volatile ("pause"); }
        }
    }
}
void gui_mouse_move(int dx, int dy)
{
    /* Cursor-only (or full only on hover/paint change) - see
       gui_move_to. Do NOT add a gui_draw() here. */
    gui_move_to(gui_mx + dx, gui_my + dy);
}
void gui_usb_move(int dx, int dy, int buttons)
{
    gui_mouse_move(dx, dy);
    if (buttons & 1) { if (!gui_prev_left) gui_mouse_click(1); }
    else gui_prev_left = 0;
    /* No gui_draw() here: gui_mouse_move already updated the
       cursor, and gui_mouse_click already full-draws on a
       click. A full draw here was the 1 px = whole screen bug. */
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
    /* No final gui_draw(): gui_mouse_move already moved the
       cursor, and click/right/back above already drew. */
}
void gui_touch_set(int x, int y, int down)
{
    if (!gui_ready) return;
    gui_move_to(x, y);
    if (down) { if (!gui_prev_left) gui_mouse_click(1); }
    else gui_prev_left = 0;
    /* No gui_draw(): gui_move_to already updated the cursor,
       and a click already full-draws. */
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
    if (gui_input[0]=='B' || gui_input[0]=='b' || gui_input[0]=='R' || gui_input[0]=='r' || gui_input[0]=='N' || gui_input[0]=='n' || gui_input[0]=='P' || gui_input[0]=='p' || gui_input[0]=='C' || gui_input[0]=='c' || gui_input[0]=='Z' || gui_input[0]=='z') {
        const char *nm=gui_input; if ((nm[0]=='R' || nm[0]=='r') && (nm[1]=='U' || nm[1]=='u') && (nm[2]=='N' || nm[2]=='n') && nm[3]==' ') nm+=4;
        if ((nm[0]=='B' || nm[0]=='b') && (nm[1]=='R' || nm[1]=='r')) gui_run_fse("BROWSY");
        else if ((nm[0]=='R' || nm[0]=='r') && (nm[1]=='E' || nm[1]=='e')) gui_run_fse("RETERM");
        else if ((nm[0]=='N' || nm[0]=='n') && (nm[1]=='A' || nm[1]=='a')) gui_run_fse("NANO");
        else if ((nm[0]=='P' || nm[0]=='p') && (nm[1]=='A' || nm[1]=='a')) gui_run_fse("PATHIFY");
        else if ((nm[0]=='C' || nm[0]=='c') && (nm[1]=='L' || nm[1]=='l')) gui_run_fse("CLIPONG");
        else if ((nm[0]=='Z' || nm[0]=='z') && nm[1]==0) gui_run_fse("Z");
        else gui_term_push("UNKNOWN - TRY HELP");
        return;
    }
    if (gui_input[0]=='H' || gui_input[0]=='h') { gui_term_push("HELP ECHO VER APPS"); }
    else if (gui_input[0]=='V' || gui_input[0]=='v') { gui_term_push("FISIXOS GUI V2"); }
    else if (gui_input[0]=='A' || gui_input[0]=='a') { gui_term_push("55 APPS IN ALL APPS"); }
    else if (gui_input[0]=='E' && gui_input[1]=='C') { gui_term_push(gui_input+5); }
    else if (gui_input[0]=='C' && gui_input[1]=='L') { gui_term_lines=0; }
    else { gui_term_push("UNKNOWN - TRY HELP"); }
}
void gui_mouse_click(int left_down)
{
    int ty, wx, wy, ww, wh, i;
    if (!left_down || gui_prev_left) return;
    gui_prev_left=1; ty=(int)gui_h-gui_taskbar_h; wx=gui_win_x; wy=gui_win_y; ww=gui_win_w; wh=gui_win_h;
    /* Menu bar first (Windows 1.0 style): FILE / APPS / GAMES / SETTINGS / HELP */
    if (gui_my>=gui_s(76) && gui_my<gui_s(108)) {
        int m=0; if (gui_mx>=gui_s(16) && gui_mx<gui_s(116)) m=1; else if (gui_mx>=gui_s(120) && gui_mx<gui_s(220)) m=2; else if (gui_mx>=gui_s(224) && gui_mx<gui_s(324)) m=3; else if (gui_mx>=gui_s(328) && gui_mx<gui_s(428)) m=4; else if (gui_mx>=gui_s(432) && gui_mx<gui_s(532)) m=5;
        if (m) { gui_menu_open = (gui_menu_open==m) ? 0 : m; gui_draw_menu_only(); return; }
    }
    if (gui_menu_open) {
        int mx=gui_s(gui_menu_x(gui_menu_open)), cnt=gui_menu_count(gui_menu_open);
        if (gui_inside(gui_mx,gui_my,mx,gui_s(108),gui_s(240),cnt*gui_s(36))) {
            int idx=(gui_my-(gui_s(108)+gui_s(8)))/gui_s(36); const char *item=gui_menu_item(gui_menu_open, idx); int menu=gui_menu_open; gui_menu_open=0;
            if (item) {
                if (menu==4) {
                    if (idx>=0 && idx<6) { gui_bg_idx=idx; gui_bg_col=gui_bg_palette[idx]; gui_draw(); return; }
                    if (idx==6) { gui_rainbow_on=!gui_rainbow_on; gui_draw(); return; }
                    if (idx==7) { gui_desktop_icons_on=!gui_desktop_icons_on; gui_draw(); return; }
                } else if (menu==5 && idx==0) { gui_open_generic("ABOUT FISIXOS","FISIXOS GUI - MENUS","NOT MESSY ICONS","WINDOWS 1.0 STYLE",""); gui_draw_window_only(); return; }
                else if (menu==5 && idx==1) { gui_open_generic("HELP","ARROWS MOVE","ENTER CLICKS","ESC CLOSES",""); gui_draw_window_only(); return; }
                else if (idx>=0) {
                    if (item[0]=='S' && item[1]=='H') { gui_open_generic("SHUTDOWN","HALTING - REAL HW:","ACPI S5 POWER OFF,","VM FALLBACK: HALT",""); gui_draw_window_only(); return; }
                    gui_open_app(item); gui_draw_window_only(); return;
                }
            }
            gui_draw_menu_only(); return;
        } else { gui_menu_open=0; gui_draw_menu_only(); return; }
    }
    if (gui_start_open && gui_inside(gui_mx,gui_my,((int)gui_w-gui_s(320))/2,ty-gui_s(300)-gui_s(8),gui_s(320),gui_s(300))) {
        int idx=(gui_my-(ty-gui_s(300)-gui_s(8)+gui_s(12)))/gui_s(42);
        if (idx==0) gui_open_app("TERMINAL"); else if (idx==1) gui_open_app("FILES"); else if (idx==2) gui_open_app("NANO"); else if (idx==3) gui_open_app("CALCULATOR"); else if (idx==4) gui_open_app("ALL APPS"); else if (idx==5) { gui_open_generic("SHUTDOWN","HALTING - REAL HW:","ACPI S5 POWER OFF,","VM FALLBACK: HALT",""); }
        gui_draw_window_only(); return;
    }
    gui_start_open=0;
    if (gui_my>=ty) {
        int by = ty + (gui_taskbar_h - gui_taskbar_btn_h()) / 2, bw = gui_taskbar_btn_w(), bh = gui_taskbar_btn_h();
        if (gui_inside(gui_mx,gui_my,gui_taskbar_btn_x(0),by,bw,bh)) { gui_start_open=1; gui_draw_taskbar_only(); }
        else if (gui_inside(gui_mx,gui_my,gui_taskbar_btn_x(1),by,bw,bh)) { gui_open_app("TERMINAL"); gui_draw_window_only(); }
        else if (gui_inside(gui_mx,gui_my,gui_taskbar_btn_x(2),by,bw,bh)) { gui_open_app("FILES"); gui_draw_window_only(); }
        else if (gui_inside(gui_mx,gui_my,gui_taskbar_btn_x(3),by,bw,bh)) { gui_open_app("NANO"); gui_draw_window_only(); }
        else if (gui_inside(gui_mx,gui_my,gui_taskbar_btn_x(4),by,bw,bh)) { gui_open_app("MS-DOS"); gui_draw_window_only(); }
        else gui_draw_taskbar_only();
        return;
    }
    /* Desktop icons, only if they are turned on */
    if (gui_desktop_icons_on) { for (i=0;i<15;i++) { int x=gui_s(16)+(i%4)*gui_s(120); int y=gui_s(120)+(i/4)*gui_s(110); if (gui_inside(gui_mx,gui_my,x,y,gui_s(64),gui_s(64))) { gui_open_app(gui_desktop[i]); gui_draw_window_only(); return; } } }
    if (!gui_win_visible) return;
    /* Drop a dragged / resized window first */
    if (gui_dragging) {
        gui_dragging=0;
        if (gui_win_x < 60) gui_snap_apply(1); else if (gui_win_x + gui_win_w > (int)gui_w - 60) gui_snap_apply(2); else if (gui_win_y < 60) gui_snap_apply(3); else { gui_win_x=((gui_win_x+20)/40)*40; gui_win_y=((gui_win_y+20)/40)*40; gui_snap_state=0; }
        gui_draw_window_only(); return;
    }
    if (gui_resizing) { gui_resizing=0; gui_win_w=((gui_win_w+20)/40)*40; gui_win_h=((gui_win_h+20)/40)*40; if (gui_win_w<320) gui_win_w=320; if (gui_win_h<200) gui_win_h=200; gui_draw_window_only(); return; }
    /* Title bar: drag, snap, close */
    if (gui_inside(gui_mx,gui_my,wx,wy,ww,gui_s(40))) {
        if (gui_inside(gui_mx,gui_my,wx+ww-gui_s(36),wy+gui_s(8),gui_s(24),gui_s(24))) { int was_pong=(gui_mode==MODE_PONG); gui_win_visible=0; if (was_pong) gui_snap_apply(0); gui_draw_close_window(); return; }
        if (gui_inside(gui_mx,gui_my,wx+ww-gui_s(64),wy+gui_s(8),gui_s(24),gui_s(24))) { int ns=(gui_snap_state+1)%4; if (ns==0) { gui_win_x=gui_win_norm_x; gui_win_y=gui_win_norm_y; gui_win_w=gui_win_norm_w; gui_win_h=gui_win_norm_h; gui_snap_state=0; } else gui_snap_apply(ns); gui_draw_window_only(); return; }
        gui_dragging=1; gui_drag_off_x=gui_mx-gui_win_x; gui_drag_off_y=gui_my-gui_win_y; gui_draw_window_only(); return;
    }
    /* Resize handle, bottom-right */
    if (gui_inside(gui_mx,gui_my,wx+ww-gui_s(24),wy+wh-gui_s(24),gui_s(24),gui_s(24))) { gui_resizing=1; gui_draw_window_only(); return; }
    /* Window content buttons / lists */
    if (gui_mode==MODE_SETTINGS) {
        for (i=0;i<6;i++) if (gui_inside(gui_mx,gui_my,wx+24,wy+96+i*36,200,32)) { gui_bg_idx=i; gui_bg_col=gui_bg_palette[i]; gui_draw(); return; }
        if (gui_inside(gui_mx,gui_my,wx+260,wy+96,200,32)) { gui_rainbow_on=!gui_rainbow_on; gui_draw(); return; }
        for (i=0;i<6;i++) if (gui_inside(gui_mx,gui_my,wx+260,wy+208+i*36,200,32)) { gui_accent_idx=i; gui_accent_col=gui_accent_palette[i]; gui_draw(); return; }
        gui_draw_content_only(); return;
    }
    if (gui_mode==MODE_GAMES) {
        if (gui_inside(gui_mx,gui_my,wx+24,wy+96,200,32)) { gui_open_app("PONG"); gui_draw_window_only(); return; }
        if (gui_inside(gui_mx,gui_my,wx+24,wy+136,200,32)) { gui_open_app("MINESWEEPER"); gui_draw_window_only(); return; }
        if (gui_inside(gui_mx,gui_my,wx+24,wy+176,200,32)) { gui_open_app("SNAKE"); gui_draw_window_only(); return; }
        if (gui_inside(gui_mx,gui_my,wx+24,wy+216,200,32)) { gui_open_app("RAINBOW"); gui_draw_window_only(); return; }
        gui_draw_content_only(); return;
    }
    if (gui_mode==MODE_MINES) {
        if (gui_inside(gui_mx,gui_my,wx+360,wy+56,140,32)) { gui_mines_flag_mode=!gui_mines_flag_mode; gui_draw_content_only(); return; }
        if (gui_inside(gui_mx,gui_my,wx+520,wy+56,100,32)) { gui_mines_init(); gui_draw_content_only(); return; }
        { int gx=wx+24, gy=wy+96, cx=(gui_mx-gx)/32, cy=(gui_my-gy)/32; if (cx>=0&&cx<9&&cy>=0&&cy<9) { int c=cy*9+cx; if (gui_mines_flag_mode) gui_mines_flag[c]=!gui_mines_flag[c]; else gui_mines_reveal(c); gui_draw_content_only(); return; } }
        gui_draw_content_only(); return;
    }
    if (gui_mode==MODE_ALLAPPS && gui_inside(gui_mx,gui_my,wx+24,wy+64+5*36,200,32)) { gui_allapps_page++; if (gui_allapps_page*5>=55) gui_allapps_page=0; gui_draw_content_only(); return; }
    if (gui_mode==MODE_ALLAPPS) { for (i=0;i<5;i++) { int idx=gui_allapps_page*5+i; if (idx<55 && gui_inside(gui_mx,gui_my,wx+24,wy+64+i*36,300,32)) { gui_open_app(gui_apps[idx]); gui_draw_window_only(); return; } } }
    if (gui_mode==MODE_WINDOW) {
        if (gui_inside(gui_mx,gui_my,wx+24,wy+wh-48,120,32)) { gui_win_visible=0; gui_draw_close_window(); return; }
        if (gui_inside(gui_mx,gui_my,wx+160,wy+wh-48,160,32)) { gui_open_app("GAMES"); gui_draw_window_only(); return; }
    }
    if (gui_mode==MODE_FILES) {
        if (gui_inside(gui_mx,gui_my,wx+24,wy+100,300,32)) { gui_open_app("BROWSY"); gui_draw_window_only(); return; }
        if (gui_inside(gui_mx,gui_my,wx+24,wy+136,300,32)) { gui_open_app("CLIPONG"); gui_draw_window_only(); return; }
        if (gui_inside(gui_mx,gui_my,wx+24,wy+172,300,32)) { gui_open_app("NANO"); gui_draw_window_only(); return; }
        if (gui_inside(gui_mx,gui_my,wx+24,wy+208,300,32)) { gui_open_app("PATHIFY"); gui_draw_window_only(); return; }
        if (gui_inside(gui_mx,gui_my,wx+24,wy+244,160,32)) { gui_open_app("RETERM"); gui_draw_window_only(); return; }
        if (gui_inside(gui_mx,gui_my,wx+184,wy+244,160,32)) { gui_open_app("Z"); gui_draw_window_only(); return; }
    }
    if (gui_mode==MODE_CLOCK && gui_inside(gui_mx,gui_my,wx+24,wy+208,200,32)) { gui_draw_content_only(); return; }
    gui_draw_content_only();
}
static int gui_mouse_write(uint8_t cmd)
{
    if (!gui_wait_in_empty()) return 0; gui_outb(0x64,0xD4); if (!gui_wait_in_empty()) return 0; gui_outb(0x60,cmd); if (!gui_wait_out_full()) return 0; return gui_inb(0x60)==0xFA;
}
static int gui_mouse_init(void)
{
    if (!gui_wait_in_empty()) return 0; gui_outb(0x64,0xA8); if (!gui_mouse_write(0xFF)) return 0; if (gui_wait_out_full()) (void)gui_inb(0x60); if (gui_wait_out_full()) (void)gui_inb(0x60); if (!gui_mouse_write(0xF6)) return 0; if (!gui_mouse_write(0xF4)) return 0; /* 2026-10-30 hang fix: old drain loop was unbounded, and on a
     * controller with no PS/2 mouse, or one that keeps its output
     * buffer full, it never exits, freezing the first GUI poll
     * with the GUI drawn but dead. Drain at most 16 bytes. */
    { int drain = 0; while ((gui_inb(0x64) & 0x01) && drain++ < 16) (void)gui_inb(0x60); }
    return 1;
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

/* .fse in the GUI, 2026-10-30. Print goes to the black terminal
 * window (last 6 lines, like the GUI terminal), keys come from
 * the PS/2 keyboard directly, so an .fse program can run while
 * the GUI is not polling input for it. This is a synchronous
 * run: the program owns the terminal until it returns, then
 * the GUI window shows its exit code and ESC closes it.
 * File I/O to the install drive is NOT available in the kernel
 * yet (see proc.c: file_read/write return -1, dir_list 0),
 * so programs that need files will fail gracefully, not crash.
 * That is the next stage, not a fake run. */
static void gui_fse_print(const char *s)
{
    char line[64]; int n=0, i=0;
    if (!s) return;
    while (1) {
        char c=s[i++];
        if (c=='\n' || c=='\r' || c==0) {
            line[n]=0; if (n>0) gui_term_push(line); n=0;
            if (c==0) break;
            continue;
        }
        if (n<63) line[n++]=c;
    }
    gui_draw_content_only();
}
static int gui_fse_read_key(void)
{
    for (;;) {
        uint8_t st = gui_inb(0x64);
        if (!(st & 0x01)) continue;
        { uint8_t b = gui_inb(0x60);
          if (st & 0x20) continue; /* mouse packet, not a key */
          if (b==0xE0) {
              uint8_t b2; while (!(gui_inb(0x64) & 0x01)) { } b2=gui_inb(0x60);
              if (b2==0x48) return 0x10001; if (b2==0x50) return 0x10002; if (b2==0x4B) return 0x10003; if (b2==0x4D) return 0x10004; continue;
          }
          if (b>=0x80) continue; /* break code */
          if (b==0x1C) return '\r'; if (b==0x0E) return 0x08; if (b==0x01) return 0x1B; if (b==0x0F) return '\t';
          { char c=gui_scancode_to_ascii(b); if (c) return (int)c; }
        }
    }
}
static void gui_run_fse(const char *name)
{
    const uint8_t *data=0; uint32_t len=0; int rc;
    gui_mode=MODE_TERMINAL; gui_win_title=name; gui_win_visible=1; gui_term_lines=0; gui_input_len=0; gui_input[0]=0;
    gui_term_push("RUNNING FSE - REAL RUN"); gui_term_push(name);
    if (!fse_embedded_find(name, &data, &len)) { gui_term_push("FSE NOT EMBEDDED"); gui_draw_content_only(); return; }
    proc_set_gui_print(gui_fse_print); proc_set_gui_read_key(gui_fse_read_key);
    rc = proc_run_fse(data, len);
    proc_set_gui_print(0); proc_set_gui_read_key(0);
    { char buf[32]; int j=0, v=rc; char rev[16]; int r=0; if (v<0) { buf[j++]='-'; v=-v; } if (v==0) buf[j++]='0'; else { while (v>0 && r<12) { rev[r++]=(char)('0'+v%10); v/=10; } while (r>0) buf[j++]=rev[--r]; } buf[j]=0; gui_term_push("FSE EXITED CODE"); gui_term_push(buf); }
    gui_term_push("ESC TO CLOSE"); gui_draw_content_only();
}
static void gui_key_ascii(char c)
{
    if (c==0) return;
    if (gui_mode==MODE_TERMINAL || gui_mode==MODE_CALC) { if (gui_input_len<120) { gui_input[gui_input_len++]=c; gui_input[gui_input_len]=0; } gui_draw_content_only(); }
    else if (gui_mode==MODE_NOTEPAD) { if (gui_note_len<500) { gui_note[gui_note_len++]=c; gui_note[gui_note_len]=0; } gui_draw_content_only(); }
}
static void gui_key_enter(void)
{
    if (gui_mode==MODE_TERMINAL) { gui_term_hist_save(); gui_term_exec(); gui_input_len=0; gui_input[0]=0; gui_draw_content_only(); }
    else if (gui_mode==MODE_CALC) { gui_calc_eval(); gui_input_len=0; gui_input[0]=0; gui_draw_content_only(); }
    else if (gui_mode==MODE_NOTEPAD) { if (gui_note_len<500) { gui_note[gui_note_len++]=' '; gui_note[gui_note_len]=0; } gui_draw_content_only(); }
    else if (gui_mode==MODE_PONG) { gui_pong_started=1; gui_draw_content_only(); }
    else if (gui_mode==MODE_SNAKE) { if (gui_snake_over) gui_snake_init(); gui_draw_content_only(); }
    else if (gui_mode==MODE_MINES) { if (gui_mines_over || gui_mines_won) gui_mines_init(); gui_draw_content_only(); }
}
static int gui_mouse_started = 0;
int gui_poll_input(void)
{
    uint8_t status, byte; static uint8_t pkt[3]; static int pkt_idx=0;
    if (!gui_mouse_started) { gui_mouse_started=1; gui_serial("[FisixOS] GUI mouse init start\n"); if (gui_mouse_init()) gui_serial("[FisixOS] GUI mouse OK\n"); else gui_serial("[FisixOS] GUI mouse NOT present\n"); }
    status = gui_inb(0x64); if (!(status & 0x01)) return 0; byte = gui_inb(0x60);
    if (status & 0x20) {
        if (pkt_idx==0 && !(byte & 0x08)) return 0; pkt[pkt_idx++]=byte; if (pkt_idx<3) return 0; pkt_idx=0;
        { int left=pkt[0]&0x01; int dx=(int8_t)pkt[1]; int dy=(int8_t)pkt[2]; gui_mouse_move(dx,-dy); if (left && !gui_prev_left) gui_mouse_click(1); else if (!left) gui_prev_left=0; /* No gui_draw(): mouse_move already updated, click already drew. */ return 1; }
    } else {
        /* Extended scancodes: PS/2 arrows are E0 48/50/4B/4D.
         * The old code ignored E0, so arrows did nothing. */
        if (byte==0xE0) { gui_e0_prefix=1; return 0; }
        if (gui_e0_prefix) {
            int moved=0;
            gui_e0_prefix=0;
            if (gui_mode==MODE_TERMINAL && byte==0x48) { gui_term_hist_up(); gui_draw_content_only(); }
            else if (gui_mode==MODE_TERMINAL && byte==0x50) { gui_term_hist_down(); gui_draw_content_only(); }
            else if (gui_mode==MODE_PONG && byte==0x48) { gui_pong_py-=16; if (gui_pong_py<0) gui_pong_py=0; gui_draw_content_only(); }
            else if (gui_mode==MODE_PONG && byte==0x50) { gui_pong_py+=16; gui_draw_content_only(); }
            else if (gui_mode==MODE_SNAKE && byte==0x48) { if (gui_snake_dir!=2) gui_snake_dir=0; }
            else if (gui_mode==MODE_SNAKE && byte==0x50) { if (gui_snake_dir!=0) gui_snake_dir=2; }
            else if (gui_mode==MODE_SNAKE && byte==0x4B) { if (gui_snake_dir!=1) gui_snake_dir=3; }
            else if (gui_mode==MODE_SNAKE && byte==0x4D) { if (gui_snake_dir!=3) gui_snake_dir=1; }
            else if (byte==0x48) { gui_mouse_move(0,-16); moved=1; }
            else if (byte==0x50) { gui_mouse_move(0,16); moved=1; }
            else if (byte==0x4B) { gui_mouse_move(-16,0); moved=1; }
            else if (byte==0x4D) { gui_mouse_move(16,0); moved=1; }
            /* No gui_draw(): gui_mouse_move already moved the
               cursor, or full-drew on a hover change. */
            (void)moved;
            return 0;
        }
        if (byte==0x01) { /* Esc: close menu, then window, not just log it. */
            if (gui_menu_open) { gui_menu_open=0; gui_draw_menu_only(); }
            else if (gui_start_open) { gui_start_open=0; gui_draw_taskbar_only(); }
            else if (gui_win_visible) { int was_pong=(gui_mode==MODE_PONG); gui_win_visible=0; if (was_pong) gui_snap_apply(0); gui_draw_close_window(); }
            gui_serial("[FisixOS] GUI_KEY_ESC_CLOSE\n");
        }
        else if (byte==0x0F) { /* Tab: jump to menu, then START, then taskbar. */
            gui_tab_idx++;
            if (gui_tab_idx < 5) { gui_mx=gui_s(gui_menu_x(gui_tab_idx+1))+gui_s(50); gui_my=gui_s(92); }
            else if (gui_tab_idx==5) { gui_mx=gui_s(60); gui_my=(int)gui_h-gui_s(20); }
            else { gui_tab_idx=0; gui_mx=60; gui_my=92; }
            gui_draw_menu_only();
        }
        else if (byte==0x1C) {
            /* Enter: click where the cursor is, unless the cursor is
             * inside a text window (Terminal/Calc/Notepad) and NOT on
             * its X button - then Enter is text input, as before.
             * Forced click: reset prev_left first, or a stuck mouse
             * button state would swallow the keyboard click. */
            int over_x = gui_win_visible && gui_mx>=gui_win_x+gui_win_w-gui_s(36) && gui_mx<gui_win_x+gui_win_w-gui_s(12) && gui_my>=gui_win_y+gui_s(8) && gui_my<gui_win_y+gui_s(32);
            int in_win = gui_win_visible && gui_mx>=gui_win_x && gui_mx<gui_win_x+gui_win_w && gui_my>=gui_win_y && gui_my<gui_win_y+gui_win_h;
            int text_mode = (gui_mode==MODE_TERMINAL || gui_mode==MODE_CALC || gui_mode==MODE_NOTEPAD);
            if (text_mode && in_win && !over_x) gui_key_enter();
            else { gui_prev_left=0; gui_mouse_click(1); gui_prev_left=0; }
        }
        else if (byte==0x0E) { if (gui_mode==MODE_TERMINAL||gui_mode==MODE_CALC) { if (gui_input_len>0) gui_input[--gui_input_len]=0; gui_draw_content_only(); } else if (gui_mode==MODE_NOTEPAD) { if (gui_note_len>0) gui_note[--gui_note_len]=0; gui_draw_content_only(); } }
        else { char c=gui_scancode_to_ascii(byte); if (c) gui_key_ascii(c); }
        return 0;
    }
}
void gui_tick(void)
{
    gui_tick_count++;
    if (gui_mode==MODE_PONG && gui_pong_started) {
        gui_pong_tick++;
        if (gui_pong_tick % 2000 == 0) {
            gui_pong_bx += gui_pong_vx; gui_pong_by += gui_pong_vy;
            if (gui_pong_by < 0) { gui_pong_by=0; gui_pong_vy=-gui_pong_vy; }
            if (gui_pong_by > (int)gui_h-120) { gui_pong_by=(int)gui_h-120; gui_pong_vy=-gui_pong_vy; }
            if (gui_pong_bx < 40 && gui_pong_by+16 >= gui_pong_py && gui_pong_by <= gui_pong_py+80) { gui_pong_bx=40; gui_pong_vx=-gui_pong_vx; }
            if (gui_pong_bx > (int)gui_w-60 && gui_pong_by+16 >= gui_pong_ay && gui_pong_by <= gui_pong_ay+80) { gui_pong_bx=(int)gui_w-60; gui_pong_vx=-gui_pong_vx; }
            if (gui_pong_by < gui_pong_ay) gui_pong_ay-=3; else if (gui_pong_by > gui_pong_ay) gui_pong_ay+=3;
            if (gui_pong_bx < 0) { gui_pong_sr++; gui_pong_bx=400; gui_pong_by=200; }
            if (gui_pong_bx > (int)gui_w) { gui_pong_sl++; gui_pong_bx=400; gui_pong_by=200; }
            gui_draw_content_only();
        }
    }
    if (gui_mode==MODE_SNAKE && !gui_snake_over) {
        gui_snake_tick++;
        if (gui_snake_tick % 8000 == 0) {
            int j, nx=gui_snake_x[0], ny=gui_snake_y[0];
            if (gui_snake_dir==0) ny--; else if (gui_snake_dir==2) ny++; else if (gui_snake_dir==3) nx--; else nx++;
            if (nx<0||nx>=16||ny<0||ny>=16) { gui_snake_over=1; gui_draw_content_only(); return; }
            for (j=0;j<gui_snake_len;j++) if (gui_snake_x[j]==nx && gui_snake_y[j]==ny) { gui_snake_over=1; gui_draw_content_only(); return; }
            for (j=gui_snake_len;j>0;j--) { gui_snake_x[j]=gui_snake_x[j-1]; gui_snake_y[j]=gui_snake_y[j-1]; }
            gui_snake_x[0]=nx; gui_snake_y[0]=ny;
            if (nx==gui_snake_fx && ny==gui_snake_fy) { if (gui_snake_len<64) gui_snake_len++; gui_snake_score++; gui_snake_fx=(gui_snake_fx+7)%16; gui_snake_fy=(gui_snake_fy+5)%16; }
            gui_draw_content_only();
        }
    }
    if (gui_mode==MODE_CLOCK && gui_tick_count % 500000 == 0) gui_draw_content_only();
}
