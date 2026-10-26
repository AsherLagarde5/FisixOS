/* reterm.fse - ReTerm / Dosify for FisixOS, compiled with tools/fisix-gcc.
 * v0.3, 2026-10-25, at Derek's direction:
 *   - .mod files REMOVED. No modlist/modrun/modadd, no .comp,
 *     no module installer. ReTerm no longer uses .mod files.
 *   - Batch REMOVED. No .bat execution. If you type a .bat
 *     file, ReTerm refuses it and says to use Dosify instead.
 *   - Instead, ReTerm is now a Dosify-style CMD shell, a
 *     competitor to the Fisix batch terminal: same command
 *     style as the uploaded Dosify main.rs (Windows CMD
 *     commands mapped to Fisix), with a C:\> style prompt.
 *     Dosify's original is a Rust CMD emulator for Linux
 *     (colored, rustyline, which, regex); FisixOS .fse v1
 *     has no processes, no external commands, no directory
 *     listing, and no clock, so those parts are NOT ported
 *     verbatim. Commands that work here use only the .fse
 *     API: print, read_key, file_read, file_write.
 * Working here: help, echo, type <file>, copy <src> <dst>,
 * ver, cls, whoami, hostname, set/unset/env, path, calc,
 * dir (installed tools list, see below), cd (in-memory),
 * rem, exit. Not available in .fse v1, and said so honestly:
 * dir of an arbitrary folder, del, move/ren delete step,
 * date/time, ping/ipconfig and other network commands.
 * dir here lists the installed tools in SYSTEM/APPS/
 * (the install location), not an arbitrary folder, because
 * the .fse API has no directory listing.
 * Usage: run reterm.fse
 */
#include "../boot/fse.h"

static char g_line[256];
static char g_buf[4096];
static char g_env_names[8][32];
static char g_env_vals[8][64];
static unsigned int g_nenv = 0;
static char g_cwd[64] = "C:\\FISIX";
static char g_path[256] = "SYSTEM/APPS;SYSTEM/DRIVERS";

static void out(fisix_api_t *api, const char *s) { api->print(s); }
static void nl(fisix_api_t *api) { api->print_newline(); }
static unsigned int slen(const char *s) { unsigned int n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s) { unsigned int i = 0; while (s[i]) { d[i] = s[i]; i++; } d[i] = 0; }
static int seq(const char *a, const char *b) { unsigned int i = 0; while (a[i] || b[i]) { if (a[i] != b[i]) return 0; i++; } return 1; }
static int starts(const char *s, const char *p) { unsigned int i = 0; while (p[i]) { if (s[i] != p[i]) return 0; i++; } return 1; }
static int ends_bat(const char *s)
{
    unsigned int n = slen(s), i;
    for (i = 0; s[i] && s[i] != ' '; i++) {}
    if (i >= 4 && s[i - 4] == '.' && (s[i - 3] == 'b' || s[i - 3] == 'B') && (s[i - 2] == 'a' || s[i - 2] == 'A') && (s[i - 1] == 't' || s[i - 1] == 'T')) return 1;
    (void)n; return 0;
}
static int read_line(fisix_api_t *api, char *buf, unsigned int max)
{
    unsigned int n = 0;
    for (;;) {
        int k = api->read_key();
        if (k == 13) { nl(api); buf[n] = 0; return (int)n; }
        if (k == 3) return -1;
        if (k == 8 || k == 127) { if (n > 0) { n--; api->print("\b \b"); } continue; }
        if (k >= 32 && k < 127 && n + 1 < max) { char s[2]; s[0] = (char)k; s[1] = 0; buf[n++] = (char)k; api->print(s); }
    }
}
static long parse_num(const char *s, unsigned int *pos)
{
    long v = 0; int neg = 0;
    while (s[*pos] == ' ') (*pos)++;
    if (s[*pos] == '-') { neg = 1; (*pos)++; }
    while (s[*pos] >= '0' && s[*pos] <= '9') { v = v * 10 + (s[*pos] - '0'); (*pos)++; }
    return neg ? -v : v;
}
static void do_calc(fisix_api_t *api, const char *s)
{
    unsigned int p = 5; long a, b, r; char op;
    a = parse_num(s, &p);
    while (s[p] == ' ') p++;
    op = s[p++];
    b = parse_num(s, &p);
    if (op == '+') r = a + b;
    else if (op == '-') r = a - b;
    else if (op == '*') r = a * b;
    else if (op == '/') { if (b == 0) { out(api, "calc: divide by zero"); nl(api); return; } r = a / b; }
    else { out(api, "calc: use a+b, a-b, a*b, a/b"); nl(api); return; }
    out(api, "calc: "); api->print_u64((unsigned long long)r); nl(api);
}
static void env_set(const char *name, const char *val)
{
    unsigned int i, j;
    for (i = 0; i < g_nenv; i++) if (seq(g_env_names[i], name)) { scpy(g_env_vals[i], val); return; }
    if (g_nenv >= 8) return;
    scpy(g_env_names[g_nenv], name);
    for (j = 0; val[j] && j < 63; j++) g_env_vals[g_nenv][j] = val[j];
    g_env_vals[g_nenv][j] = 0; g_nenv++;
}
static int env_find(const char *name)
{
    unsigned int i;
    for (i = 0; i < g_nenv; i++) if (seq(g_env_names[i], name)) return (int)i;
    return -1;
}
static void print_help(fisix_api_t *api)
{
    out(api, "DOSIFY for FisixOS (ReTerm) - CMD-style shell, batch removed"); nl(api);
    out(api, "dir, cd, type <file>, copy <src> <dst>, echo <text>, cls"); nl(api);
    out(api, "ver, whoami, hostname, set, unset, env, path, calc <a+b>"); nl(api);
    out(api, "gui, rem, exit. .bat files are NOT run. .mod files are REMOVED."); nl(api);
}

int fse_main(fisix_api_t *api)
{
    int pr = api->file_read("SYSTEM\\PATH.TXT", g_path, sizeof(g_path));
    if (pr < 0) api->file_read("fisixpath.txt", g_path, sizeof(g_path));
    out(api, "DOSIFY shell active. Type 'exit' to leave. Type 'help' for commands."); nl(api);
    out(api, "Batch is removed here: use Dosify commands, not .bat files."); nl(api);
    for (;;) {
        int r;
        out(api, g_cwd); out(api, "> ");
        r = read_line(api, g_line, sizeof(g_line));
        if (r < 0 || seq(g_line, "exit") || seq(g_line, "quit")) break;
        if (seq(g_line, "gui")) { out(api, "Leaving Dosify. At the loader prompt, type gui to open the GUI."); nl(api); break; }
        if (ends_bat(g_line)) { out(api, "Batch removed: use Dosify commands instead, .bat files are not run."); nl(api); continue; }
        if (starts(g_line, "mod") || starts(g_line, "comp") || starts(g_line, "module")) { out(api, "Mod files removed: ReTerm no longer uses .mod/.comp files."); nl(api); continue; }
        if (seq(g_line, "help")) print_help(api);
        else if (starts(g_line, "echo ")) out(api, g_line + 5), nl(api);
        else if (seq(g_line, "echo")) nl(api);
        else if (seq(g_line, "ver")) { out(api, "DOSIFY for FisixOS 0.3 (ReTerm, .fse)"); nl(api); }
        else if (seq(g_line, "cls")) { int i; for (i = 0; i < 20; i++) nl(api); }
        else if (seq(g_line, "whoami")) { out(api, "fisix"); nl(api); }
        else if (seq(g_line, "hostname")) { out(api, "FISIXOS"); nl(api); }
        else if (seq(g_line, "dir")) {
            out(api, "Installed tools in SYSTEM/APPS/:"); nl(api);
            out(api, "BROWSY.FSE  PATHIFY.FSE  RETERM.FSE  Z.FSE  CLIPONG.FSE  NANO.FSE"); nl(api);
            out(api, "(dir of an arbitrary folder is not in the .fse API)"); nl(api);
        }
        else if (starts(g_line, "cd ") || starts(g_line, "chdir ")) {
            const char *d = g_line[3] == ' ' ? g_line + 3 : g_line + 6;
            while (*d == ' ') d++;
            if (d[0]) scpy(g_cwd, d);
            out(api, g_cwd); nl(api);
        }
        else if (seq(g_line, "cd") || seq(g_line, "chdir")) { out(api, g_cwd); nl(api); }
        else if (starts(g_line, "type ")) {
            int tr = api->file_read(g_line + 5, g_buf, sizeof(g_buf));
            if (tr < 0) { out(api, "type: could not read "); out(api, g_line + 5); nl(api); }
            else { out(api, g_buf); nl(api); }
        }
        else if (starts(g_line, "copy ")) {
            /* copy <src> <dst>: split at first space after src */
            unsigned int p = 5, s = 5, e; char src[64], dst[64]; int tr;
            while (g_line[p] == ' ') p++; s = p; while (g_line[p] && g_line[p] != ' ') p++; e = p;
            { unsigned int k; for (k = 0; k < e - s && k < 63; k++) src[k] = g_line[s + k]; src[k] = 0; }
            while (g_line[p] == ' ') p++;
            scpy(dst, g_line + p);
            tr = api->file_read(src, g_buf, sizeof(g_buf));
            if (tr < 0) { out(api, "copy: could not read "); out(api, src); nl(api); }
            else if (api->file_write(dst, g_buf, (unsigned int)tr) == 0) { out(api, "copy: 1 file copied"); nl(api); }
            else { out(api, "copy: write failed (Liveboot?)"); nl(api); }
        }
        else if (starts(g_line, "set ")) {
            unsigned int p = 4, st, e;
            while (g_line[p] == ' ') p++; st = p; while (g_line[p] && g_line[p] != ' ') p++; e = p;
            { char nm[32]; unsigned int k; for (k = 0; k < e - st && k < 31; k++) nm[k] = g_line[st + k]; nm[k] = 0; while (g_line[p] == ' ') p++; env_set(nm, g_line + p); }
            out(api, "set ok"); nl(api);
        }
        else if (seq(g_line, "set")) {
            unsigned int i; for (i = 0; i < g_nenv; i++) { out(api, g_env_names[i]); out(api, "="); out(api, g_env_vals[i]); nl(api); }
        }
        else if (starts(g_line, "unset ")) {
            int idx = env_find(g_line + 6);
            if (idx >= 0) { unsigned int i; for (i = (unsigned int)idx; i + 1 < g_nenv; i++) { scpy(g_env_names[i], g_env_names[i + 1]); scpy(g_env_vals[i], g_env_vals[i + 1]); } g_nenv--; out(api, "unset ok"); nl(api); }
            else { out(api, "unset: not set"); nl(api); }
        }
        else if (seq(g_line, "env")) {
            unsigned int i; for (i = 0; i < g_nenv; i++) { out(api, g_env_names[i]); out(api, "="); out(api, g_env_vals[i]); nl(api); }
            if (!g_nenv) { out(api, "(no env vars)"); nl(api); }
        }
        else if (seq(g_line, "path")) { out(api, g_path); nl(api); }
        else if (starts(g_line, "path ")) { scpy(g_path, g_line + 5); out(api, "path set (in memory)"); nl(api); }
        else if (starts(g_line, "calc ")) do_calc(api, g_line);
        else if (starts(g_line, "rem") || starts(g_line, "::")) { /* comment, no-op */ }
        else if (seq(g_line, "del") || starts(g_line, "del ") || seq(g_line, "move") || starts(g_line, "move ") || seq(g_line, "ren") || starts(g_line, "ren ")) { out(api, "Not in .fse API: no delete/rename call (copy works, see help)."); nl(api); }
        else if (g_line[0] == 0) { /* blank */ }
        else { out(api, "'"); out(api, g_line); out(api, "' is not recognized as an internal or external command,"); nl(api); out(api, "operable program or batch file."); nl(api); }
    }
    out(api, "Goodbye."); nl(api);
    return 0;
}
