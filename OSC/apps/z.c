/* z.fse - Z language for FisixOS, compiled with tools/fisix-gcc.
 * Original Z is a Rust compiler that emits Windows x86-64 PE
 * executables. FisixOS .fse v1 cannot emit or run PE files, so
 * this is a Fisix-native Z interpreter for the line language:
 *   Z\burn("text")          print text
 *   Z\spam("text")          print as an info dialog line
 *   Z\legalshit("text")     print as a warning dialog line
 *   Z\let name = 21 * 2     integer variable (also Z\set)
 *   Z\burn("Answer: {name}")  {name} is replaced by the variable
 * It also reads a .z file (file_read) or, with no file, reads
 * Z lines interactively until a blank line. Dialogs are printed
 * lines here: FisixOS has no MessageBoxW, so that part is an
 * honest print, not a popup.
 * Usage: run z.fse
 */
#include "../boot/fse.h"

static char g_buf[8192];
static char g_line[256];
static char g_names[8][32];
static long g_vals[8];
static unsigned int g_nvars = 0;

static void out(fisix_api_t *api, const char *s) { api->print(s); }
static void nl(fisix_api_t *api) { api->print_newline(); }
static unsigned int slen(const char *s) { unsigned int n = 0; while (s[n]) n++; return n; }
static int seq_n(const char *a, const char *b, unsigned int n) { unsigned int i = 0; for (i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }
static int starts(const char *s, const char *p) { unsigned int i = 0; while (p[i]) { if (s[i] != p[i]) return 0; i++; } return 1; }
static void trim(char *s)
{
    unsigned int i = 0, j;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) { j = 0; while (s[i]) s[j++] = s[i++]; s[j] = 0; }
    j = slen(s); while (j && (s[j - 1] == ' ' || s[j - 1] == '\t' || s[j - 1] == '\r')) s[--j] = 0;
}
static int find_var(const char *name, unsigned int len)
{
    unsigned int i, j;
    for (i = 0; i < g_nvars; i++) {
        if (slen(g_names[i]) != len) continue;
        for (j = 0; j < len; j++) if (g_names[i][j] != name[j]) break;
        if (j == len) return (int)i;
    }
    return -1;
}
static void set_var(const char *name, unsigned int len, long v)
{
    int idx = find_var(name, len);
    unsigned int i;
    if (idx < 0) {
        if (g_nvars >= 8) return;
        idx = (int)g_nvars++;
        for (i = 0; i < len && i < 31; i++) g_names[idx][i] = name[i];
        g_names[idx][i] = 0;
    }
    g_vals[idx] = v;
}
static long parse_expr(const char *s, unsigned int *pos)
{
    long a, b; char op;
    while (s[*pos] == ' ') (*pos)++;
    if (s[*pos] >= '0' && s[*pos] <= '9') {
        a = 0; while (s[*pos] >= '0' && s[*pos] <= '9') { a = a * 10 + (s[*pos] - '0'); (*pos)++; }
    } else if ((s[*pos] >= 'a' && s[*pos] <= 'z') || (s[*pos] >= 'A' && s[*pos] <= 'Z') || s[*pos] == '_') {
        unsigned int st = *pos; while ((s[*pos] >= 'a' && s[*pos] <= 'z') || (s[*pos] >= 'A' && s[*pos] <= 'Z') || (s[*pos] >= '0' && s[*pos] <= '9') || s[*pos] == '_') (*pos)++;
        int idx = find_var(s + st, *pos - st); a = idx >= 0 ? g_vals[idx] : 0;
    } else a = 0;
    while (s[*pos] == ' ') (*pos)++;
    op = s[*pos];
    if (op == '+' || op == '-' || op == '*' || op == '/') {
        (*pos)++; b = parse_expr(s, pos);
        if (op == '+') return a + b; if (op == '-') return a - b; if (op == '*') return a * b; return b ? a / b : 0;
    }
    return a;
}
static void print_burn(fisix_api_t *api, const char *s)
{
    unsigned int i = 0;
    while (s[i]) {
        if (s[i] == '{') {
            unsigned int st = i + 1, e = st; while (s[e] && s[e] != '}') e++;
            if (s[e] == '}') { int idx = find_var(s + st, e - st); if (idx >= 0) api->print_u64((unsigned long long)g_vals[idx]); i = e + 1; continue; }
        }
        { char c[2]; c[0] = s[i]; c[1] = 0; api->print(c); i++; }
    }
    nl(api);
}
static void run_line(fisix_api_t *api, char *line)
{
    trim(line);
    if (!line[0] || starts(line, "//")) return;
    if (starts(line, "Z\\burn(") || starts(line, "Z\\spam(") || starts(line, "Z\\legalshit(")) {
        int is_spam = starts(line, "Z\\spam(");
        int is_warn = starts(line, "Z\\legalshit(");
        unsigned int i = 0; char buf[512]; unsigned int n = 0;
        while (line[i] && line[i] != '"') i++;
        if (line[i] == '"') {
            i++;
            while (line[i] && line[i] != '"' && n + 1 < sizeof(buf)) buf[n++] = line[i++];
            buf[n] = 0;
            if (is_spam) out(api, "[dialog info] ");
            if (is_warn) out(api, "[dialog warning] ");
            print_burn(api, buf);
        }
        return;
    }
    if (starts(line, "Z\\let ") || starts(line, "Z\\set ")) {
        unsigned int p = 6, st, e; long v;
        while (line[p] == ' ') p++;
        st = p; while ((line[p] >= 'a' && line[p] <= 'z') || (line[p] >= 'A' && line[p] <= 'Z') || (line[p] >= '0' && line[p] <= '9') || line[p] == '_') p++;
        e = p; while (line[p] == ' ' || line[p] == '=') p++;
        v = parse_expr(line, &p);
        set_var(line + st, e - st, v);
        return;
    }
    out(api, "z: unsupported line: "); out(api, line); nl(api);
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

int fse_main(fisix_api_t *api)
{
    int r;
    out(api, "Z for FisixOS - interpreter, not a PE compiler"); nl(api);
    out(api, "Z file (blank = type lines, blank line to finish): ");
    r = read_line(api, g_line, sizeof(g_line));
    if (r < 0) return 0;
    if (g_line[0]) {
        r = api->file_read(g_line, g_buf, sizeof(g_buf));
        if (r < 0) { out(api, "z: could not read "); out(api, g_line); nl(api); return 0; }
        {
            unsigned int i = 0, n = 0;
            for (;;) {
                if (g_buf[i] == '\n' || g_buf[i] == 0) {
                    g_line[n] = 0; run_line(api, g_line); n = 0;
                    if (g_buf[i] == 0) break;
                } else if (g_buf[i] != '\r' && n + 1 < sizeof(g_line)) g_line[n++] = g_buf[i];
                i++;
            }
        }
    } else {
        for (;;) {
            out(api, "z> ");
            r = read_line(api, g_line, sizeof(g_line));
            if (r < 0 || g_line[0] == 0) break;
            run_line(api, g_line);
        }
    }
    out(api, "z done."); nl(api);
    return 0;
}
