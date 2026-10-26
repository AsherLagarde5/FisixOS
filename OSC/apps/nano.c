/* nano.fse - Fisix Nano, a Nano-like editor as a .fse program.
 * This is NOT the full GNU Nano source yet (see tools/PORTING-NANO.md
 * for why GNU Nano needs a POSIX libc + ncurses + a kernel syscall
 * ABI first). This is a working Fisix-native editor with Nano
 * keybindings, built as a .fse with GCC/Rust, so you can actually
 * edit files on FisixOS today:
 *   run nano.fse
 *   File to edit: notes.txt
 *   (existing contents are shown, then type new lines)
 *   Ctrl-X = save and exit, Ctrl-C = exit without saving.
 * Build: tools/fisix-gcc apps/nano.c -o nano.fse
 */
#include "../boot/fse.h"

static char g_file[64];
static char g_buf[8192];
static unsigned int g_len = 0;

static void print_char(fisix_api_t *api, char c)
{
    char s[2];
    s[0] = c; s[1] = 0;
    api->print(s);
}
static int read_line(fisix_api_t *api, char *out, unsigned int max)
{
    unsigned int n = 0;
    for (;;) {
        int k = api->read_key();
        if (k == 13) { api->print_newline(); out[n] = 0; return (int)n; }
        if (k == 24) return -2; /* Ctrl-X */
        if (k == 3) return -3;  /* Ctrl-C */
        if (k == 8 || k == 127) {
            if (n > 0) { n--; api->print("\b \b"); }
            continue;
        }
        if (k >= 32 && k < 127 && n + 1 < max) {
            out[n++] = (char)k;
            print_char(api, (char)k);
        }
    }
}

int fse_main(fisix_api_t *api)
{
    char line[256];
    int r;
    api->print("Fisix Nano 0.1 - Ctrl-X save+exit, Ctrl-C exit without saving");
    api->print_newline();
    api->print("File to edit: ");
    r = read_line(api, g_file, sizeof(g_file));
    if (r < 0 || g_file[0] == 0) { api->print("No file."); api->print_newline(); return 0; }
    r = api->file_read(g_file, g_buf, sizeof(g_buf));
    if (r < 0) {
        api->print("(new file)");
        api->print_newline();
        g_len = 0;
    } else {
        g_len = (unsigned int)r;
        api->print("--- contents ---");
        api->print_newline();
        api->print(g_buf);
        api->print_newline();
        api->print("--- end, type new lines below ---");
        api->print_newline();
    }
    for (;;) {
        r = read_line(api, line, sizeof(line));
        if (r == -2) {
            if (api->file_write(g_file, g_buf, g_len) == 0) {
                api->print("Saved ");
                api->print(g_file);
                api->print_newline();
            } else {
                api->print("Save failed (liveboot or read-only media?).");
                api->print_newline();
            }
            return 0;
        }
        if (r == -3) { api->print("Exited without saving."); api->print_newline(); return 0; }
        if (r < 0) continue;
        if (g_len + (unsigned int)r + 1 < sizeof(g_buf)) {
            unsigned int i;
            for (i = 0; i < (unsigned int)r; i++) g_buf[g_len++] = line[i];
            g_buf[g_len++] = '\n';
            g_buf[g_len] = 0;
        }
    }
}
