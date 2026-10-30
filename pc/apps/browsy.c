/* browsy.fse - Browsy for FisixOS, compiled with tools/fisix-gcc.
 * This is NOT the full Windows browsybrowser (Win32/GDI+/WinHTTP).
 * FisixOS .fse v1 has no network API and no GUI toolkit, so a
 * verbatim port is impossible today. This is a real Fisix-native
 * port of the core engine job: open a local HTML file, parse it
 * (tags, entities, <title>, <a href>), and print the readable
 * text plus the links found. Network fetching is NOT claimed:
 * FisixOS networking lives in the kernel (e1000/DHCP), not in
 * the .fse API, so browsy.fse works on local pages for now.
 * Usage in the loader terminal, after copying browsy.fse and
 * a page (e.g. page.html) to the ESP/disk:
 *   run browsy.fse
 * Ported engine pieces (from browsybrowser's HTML/CSS/layout
 * headers, adapted to text): HTML tag stripping, entity
 * decoding, <title>, <a href> link collection, block layout
 * (h1-h6/p/div/li get line breaks, li gets a bullet), and
 * <style>/<script> content skipped (CSS/JS are not rendered
 * as page text). <img alt> is shown as [image: alt].
 */
#include "../boot/fse.h"

static char g_buf[8192];
static char g_file[64];
static char g_title[128];
static char g_href[128];

static unsigned int my_strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}
static void out(fisix_api_t *api, const char *s) { api->print(s); }
static void nl(fisix_api_t *api) { api->print_newline(); }
static int read_line(fisix_api_t *api, char *buf, unsigned int max)
{
    unsigned int n = 0;
    for (;;) {
        int k = api->read_key();
        if (k == 13) { nl(api); buf[n] = 0; return (int)n; }
        if (k == 3) return -1;
        if (k == 8 || k == 127) { if (n > 0) { n--; api->print("\b \b"); } continue; }
        if (k >= 32 && k < 127 && n + 1 < max) {
            char s[2]; s[0] = (char)k; s[1] = 0; buf[n++] = (char)k; api->print(s);
        }
    }
}
static int starts(const char *s, const char *p)
{
    unsigned int i = 0;
    while (p[i]) { if (s[i] != p[i]) return 0; i++; }
    return 1;
}
static void copy_token(const char *s, unsigned int start, unsigned int end, char *dst, unsigned int max)
{
    unsigned int i = 0;
    while (start + i < end && i + 1 < max) { dst[i] = s[start + i]; i++; }
    dst[i] = 0;
}

int fse_main(fisix_api_t *api)
{
    int r, i, in_tag = 0, in_title = 0, in_skip = 0, link_no = 0;
    unsigned int n = 0, ti = 0;
    out(api, "Browsy for FisixOS - local HTML reader");
    nl(api);
    out(api, "HTML file (blank = browsy://newtab): ");
    r = read_line(api, g_file, sizeof(g_file));
    if (r < 0) return 0;
    if (g_file[0] == 0) {
        out(api, "browsy://newtab"); nl(api);
        out(api, "Welcome to Browsy on FisixOS."); nl(api);
        out(api, "Open a local page with run browsy.fse and a .html file."); nl(api);
        out(api, "Network fetching is not in the .fse API yet."); nl(api);
        return 0;
    }
    r = api->file_read(g_file, g_buf, sizeof(g_buf));
    if (r < 0) { out(api, "Browsy: could not read "); out(api, g_file); nl(api); return 0; }
    n = (unsigned int)r; g_buf[n] = 0;
    /* Parse: strip tags, decode a few entities, capture title + hrefs. */
    for (i = 0; g_buf[i]; i++) {
        char c = g_buf[i];
        if (c == '<') {
            in_tag = 1;
            if (starts(g_buf + i + 1, "title")) in_title = 1;
            if (starts(g_buf + i + 1, "/title")) in_title = 0;
            if (starts(g_buf + i + 1, "style") || starts(g_buf + i + 1, "script")) in_skip = 1;
            if (starts(g_buf + i + 1, "/style") || starts(g_buf + i + 1, "/script")) in_skip = 0;
            if (starts(g_buf + i + 1, "h1") || starts(g_buf + i + 1, "h2") || starts(g_buf + i + 1, "h3") ||
                starts(g_buf + i + 1, "p") || starts(g_buf + i + 1, "div") || starts(g_buf + i + 1, "br")) nl(api);
            if (starts(g_buf + i + 1, "li")) { nl(api); out(api, "- "); }
            if (starts(g_buf + i + 1, "img")) {
                int j = i, q = 0; unsigned int hs = 0, he = 0;
                while (g_buf[j] && g_buf[j] != '>') {
                    if (starts(g_buf + j, "alt=\"")) { hs = (unsigned int)j + 5; q = 1; break; }
                    j++;
                }
                if (q) {
                    he = hs; while (g_buf[he] && g_buf[he] != '"') he++;
                    copy_token(g_buf, hs, he, g_href, sizeof(g_href));
                    out(api, "[image: "); out(api, g_href); out(api, "]"); nl(api);
                }
            }
            if (starts(g_buf + i + 1, "a ") || starts(g_buf + i + 1, "a\t")) {
                int j = i, q = 0; unsigned int hs = 0, he = 0;
                while (g_buf[j] && g_buf[j] != '>') {
                    if (starts(g_buf + j, "href=\"")) { hs = (unsigned int)j + 6; q = 1; break; }
                    j++;
                }
                if (q) {
                    he = hs; while (g_buf[he] && g_buf[he] != '"') he++;
                    copy_token(g_buf, hs, he, g_href, sizeof(g_href));
                    link_no++; out(api, "[link "); api->print_u64((unsigned long long)link_no); out(api, "] "); out(api, g_href); nl(api);
                }
            }
            continue;
        }
        if (c == '>') { in_tag = 0; continue; }
        if (in_tag || in_skip) continue;
        if (c == '&') {
            if (starts(g_buf + i, "&amp;")) { api->print("&"); i += 4; continue; }
            if (starts(g_buf + i, "&lt;")) { api->print("<"); i += 3; continue; }
            if (starts(g_buf + i, "&gt;")) { api->print(">"); i += 3; continue; }
            if (starts(g_buf + i, "&nbsp;")) { api->print(" "); i += 5; continue; }
            if (starts(g_buf + i, "&quot;")) { api->print("\""); i += 5; continue; }
        }
        if (in_title && ti + 1 < sizeof(g_title)) g_title[ti++] = c;
        { char s[2]; s[0] = c; s[1] = 0; api->print(s); }
    }
    g_title[ti] = 0; nl(api);
    if (ti) { out(api, "Title: "); out(api, g_title); nl(api); }
    out(api, "Links found: "); api->print_u64((unsigned long long)link_no); nl(api);
    (void)my_strlen;
    return 0;
}
