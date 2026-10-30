/* pathify.fse - Pathify for FisixOS, compiled with tools/fisix-gcc.
 * Windows Pathify manages PATH presets via the registry. FisixOS
 * has no registry and no Windows PATH, so this is a Fisix-native
 * port of the same job: a path list stored in SYSTEM\PATH.TXT
 * on the boot disk (fallback: fisixpath.txt at the root, for
 * disks made before SYSTEM/ existed), with List / Add / Remove
 * / Clean / Backup / Restore / Preset, in memory first, saved
 * only when you choose Save. Backups go to SYSTEM\DATA\PATH.BAK
 * (fallback: fisixpath.bak).
 * Fisix locations (see OSC/SYSTEM-LOCATIONS.md):
 *   SYSTEM/APPS/  = install location, Program Files equivalent
 *   SYSTEM/DATA/  = AppData equivalent, per-tool data files
 *   SYSTEM/PATH.TXT = the PATH itself, ;-separated
 * On Liveboot, file_write fails by design, so changes stay in
 * memory for that boot and Save reports the failure honestly
 * instead of pretending it wrote.
 * Usage: run pathify.fse
 */
#include "../boot/fse.h"

static char g_path[1024];
static char g_line[256];
static char g_tmp[1024];

static void out(fisix_api_t *api, const char *s) { api->print(s); }
static void nl(fisix_api_t *api) { api->print_newline(); }
static unsigned int slen(const char *s) { unsigned int n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s) { unsigned int i = 0; while (s[i]) { d[i] = s[i]; i++; } d[i] = 0; }
static int seq(const char *a, const char *b) { unsigned int i = 0; while (a[i] || b[i]) { if (a[i] != b[i]) return 0; i++; } return 1; }
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
static void list_path(fisix_api_t *api)
{
    unsigned int i = 0, idx = 1, start = 0;
    out(api, "Fisix PATH:"); nl(api);
    for (;;) {
        if (g_path[i] == ';' || g_path[i] == 0) {
            unsigned int j;
            api->print_u64((unsigned long long)idx); out(api, ": ");
            for (j = start; j < i; j++) { char s[2]; s[0] = g_path[j]; s[1] = 0; api->print(s); }
            nl(api); idx++; start = i + 1;
            if (g_path[i] == 0) break;
        }
        i++;
    }
}
static void save_path(fisix_api_t *api)
{
    if (api->file_write("SYSTEM\\PATH.TXT", g_path, slen(g_path)) == 0) { out(api, "Saved SYSTEM\\PATH.TXT"); nl(api); return; }
    if (api->file_write("fisixpath.txt", g_path, slen(g_path)) == 0) { out(api, "Saved fisixpath.txt (no SYSTEM/ folder)"); nl(api); return; }
    out(api, "Save failed (Liveboot / read-only?). In-memory only."); nl(api);
}
static void show_locations(fisix_api_t *api)
{
    out(api, "Install location: SYSTEM/APPS/  (Program Files equivalent)"); nl(api);
    out(api, "App data:         SYSTEM/DATA/  (AppData equivalent)"); nl(api);
    out(api, "PATH file:        SYSTEM/PATH.TXT"); nl(api);
    out(api, "User files:       SYSTEM/USER/"); nl(api);
}
static void clean_path(fisix_api_t *api)
{
    unsigned int i = 0, start = 0, outn = 0; int removed = 0;
    g_tmp[0] = 0;
    for (;;) {
        if (g_path[i] == ';' || g_path[i] == 0) {
            unsigned int len = i - start, j, k, dup = 0;
            if (len == 0) { removed++; }
            else {
                /* duplicate? scan already-kept entries in g_tmp */
                unsigned int s2 = 0;
                for (;;) {
                    unsigned int e2 = s2; while (g_tmp[e2] && g_tmp[e2] != ';') e2++;
                    if (e2 - s2 == len) {
                        unsigned int m = 1, t;
                        for (t = 0; t < len; t++) if (g_tmp[s2 + t] != g_path[start + t]) m = 0;
                        if (m) dup = 1;
                    }
                    if (g_tmp[e2] == 0) break; s2 = e2 + 1;
                }
                if (dup) removed++;
                else {
                    if (outn) g_tmp[outn++] = ';';
                    for (j = 0; j < len; j++) g_tmp[outn++] = g_path[start + j];
                    g_tmp[outn] = 0;
                }
            }
            start = i + 1;
            if (g_path[i] == 0) break;
        }
        i++;
    }
    scpy(g_path, g_tmp);
    out(api, "Cleaned, removed "); api->print_u64((unsigned long long)removed); out(api, " empty/duplicate entries."); nl(api);
}

int fse_main(fisix_api_t *api)
{
    int r = api->file_read("SYSTEM\\PATH.TXT", g_path, sizeof(g_path));
    if (r < 0) r = api->file_read("fisixpath.txt", g_path, sizeof(g_path));
    if (r < 0) scpy(g_path, "SYSTEM/APPS;SYSTEM/DRIVERS");
    out(api, "Pathify for FisixOS - PATH presets for Fisix, not Windows"); nl(api);
    out(api, "1 List  2 Add  3 Remove#  4 Clean  5 Backup  6 Restore  7 Preset  8 Save  9 Locations  0 Exit"); nl(api);
    for (;;) {
        out(api, "pathify> ");
        r = read_line(api, g_line, sizeof(g_line));
        if (r < 0 || (g_line[0] == '0' && g_line[1] == 0)) break;
        if (g_line[0] == '1') list_path(api);
        else if (g_line[0] == '2') {
            out(api, "Entry to add: "); r = read_line(api, g_line, sizeof(g_line));
            if (r > 0 && slen(g_path) + 1 + slen(g_line) < sizeof(g_path)) {
                unsigned int n = slen(g_path); g_path[n] = ';'; g_path[n + 1] = 0; scpy(g_path + n + 1, g_line);
                out(api, "Added (not saved until 8)."); nl(api);
            }
        } else if (g_line[0] == '3') {
            out(api, "Number to remove: "); r = read_line(api, g_line, sizeof(g_line));
            if (r > 0) {
                unsigned int want = (unsigned int)(g_line[0] - '0'), idx = 1, i = 0, start = 0, outn = 0;
                for (;;) {
                    if (g_path[i] == ';' || g_path[i] == 0) {
                        if (idx != want) {
                            unsigned int j; if (outn) g_tmp[outn++] = ';';
                            for (j = start; j < i; j++) g_tmp[outn++] = g_path[j];
                            g_tmp[outn] = 0;
                        }
                        idx++; start = i + 1; if (g_path[i] == 0) break;
                    }
                    i++;
                }
                scpy(g_path, g_tmp);
            }
        } else if (g_line[0] == '4') clean_path(api);
        else if (g_line[0] == '5') {
            if (api->file_write("SYSTEM\\DATA\\PATH.BAK", g_path, slen(g_path)) == 0) { out(api, "Backup saved to SYSTEM\\DATA\\PATH.BAK"); nl(api); }
            else if (api->file_write("fisixpath.bak", g_path, slen(g_path)) == 0) { out(api, "Backup saved to fisixpath.bak"); nl(api); }
            else { out(api, "Backup failed (Liveboot?)."); nl(api); }
        } else if (g_line[0] == '6') {
            int br = api->file_read("SYSTEM\\DATA\\PATH.BAK", g_tmp, sizeof(g_tmp));
            if (br < 0) br = api->file_read("fisixpath.bak", g_tmp, sizeof(g_tmp));
            if (br < 0) { out(api, "No PATH backup found."); nl(api); }
            else { scpy(g_path, g_tmp); out(api, "Restored from backup (not saved until 8)."); nl(api); }
        } else if (g_line[0] == '7') {
            out(api, "Preset: 1 Example  2 Tools: "); r = read_line(api, g_line, sizeof(g_line));
            if (g_line[0] == '1') scpy(g_path, "SYSTEM/APPS;SYSTEM/DRIVERS");
            else if (g_line[0] == '2') scpy(g_path, "SYSTEM/APPS;SYSTEM/DRIVERS;SYSTEM/TOOLS");
            out(api, "Preset applied (not saved until 8)."); nl(api);
        } else if (g_line[0] == '8') save_path(api);
        else if (g_line[0] == '9') show_locations(api);
        else out(api, "Unknown: 1-9 or 0.");
        nl(api);
    }
    out(api, "Pathify done."); nl(api);
    return 0;
}
