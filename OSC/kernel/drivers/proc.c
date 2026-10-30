#include "proc.h"
#include "fse.h"
#include "generic.h"
/* GUI hooks, set by gui.c before running an .fse: print goes to
 * the GUI terminal window (plus serial), read_key polls the
 * PS/2 keyboard. If no hook is set, print is serial-only and
 * read_key returns -1, as before. */
static void (*g_gui_print_cb)(const char *s) = 0;
static int (*g_gui_read_key_cb)(void) = 0;
void proc_set_gui_print(void (*cb)(const char *s)) { g_gui_print_cb = cb; }
void proc_set_gui_read_key(int (*cb)(void)) { g_gui_read_key_cb = cb; }
static void p_serial(const char *s);

static void p_print_cb(const char *s) { if (g_gui_print_cb) g_gui_print_cb(s); p_serial(s); }
static void p_serial(const char *s)
{
    while (s && *s) {
        uint32_t to = 100000; uint8_t lsr;
        do { __asm__ volatile ("inb %1, %0" : "=a"(lsr) : "Nd"((uint16_t)(0x3F8 + 5))); if (to-- == 0) return; } while ((lsr & 0x20) == 0);
        __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)*s), "Nd"((uint16_t)0x3F8)); s++;
    }
}
static void p_u64(uint64_t v)
{
    char buf[24]; int i = 0;
    if (v == 0) { p_print_cb("0"); return; }
    while (v > 0) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) { char c[2]; c[0] = buf[--i]; c[1] = 0; p_print_cb(c); }
}
static void p_nl(void) { p_print_cb("\n"); }
static int p_read_key(void) { if (g_gui_read_key_cb) return g_gui_read_key_cb(); return -1; }
static int p_dir_list(const char *path, char *buf, unsigned int sz) { (void)path; (void)buf; (void)sz; return 0; }
static int p_file_delete(const char *path) { (void)path; return -1; }
static int p_file_rename(const char *a, const char *b) { (void)a; (void)b; return -1; }
static int p_file_mkdir(const char *path) { (void)path; return -1; }
static int p_file_exists(const char *path) { (void)path; return 0; }
static int p_file_size(const char *path) { (void)path; return -1; }
static void p_get_time(unsigned int *y, unsigned int *mo, unsigned int *d, unsigned int *h, unsigned int *mi, unsigned int *s) { rtc_time_t rt; if (generic_rtc_read(&rt)) { *y=(unsigned int)rt.year; *mo=(unsigned int)rt.month; *d=(unsigned int)rt.day; *h=(unsigned int)rt.hour; *mi=(unsigned int)rt.minute; *s=(unsigned int)rt.second; } else { *y=0; *mo=0; *d=0; *h=0; *mi=0; *s=0; } }
static void p_clear_screen(void) { if (g_gui_print_cb) g_gui_print_cb("\n"); }
static void p_sleep_ms(unsigned int ms) { volatile uint32_t i; uint32_t n = ms * 2000u; for (i=0;i<n;i++) { __asm__ volatile ("pause"); } }
static int p_file_read(const char *path, char *buf, unsigned int sz)
{ (void)path; (void)buf; (void)sz; return -1; }
static int p_file_write(const char *path, const char *buf, unsigned int len)
{ (void)path; (void)buf; (void)len; return -1; }

static uint32_t rd32le(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int proc_run_fse_at(const uint8_t *file, uint32_t file_len, uint64_t load_addr)
{
    uint32_t entry_off, code_size, i;
    fisix_api_t api;
    fse_entry_t entry;
    if (!file || file_len < 64) return -1;
    if (rd32le(file) != FISIX_FSE_MAGIC) return -1;
    if (rd32le(file + 4) != FISIX_FSE_VERSION) return -1;
    entry_off = rd32le(file + 8);
    code_size = rd32le(file + 12);
    if (code_size > FISIX_FSE_MAX_CODE_SIZE) return -1;
    if (entry_off < 64 || entry_off >= 64 + code_size) return -1;
    if (64 + code_size > file_len) return -1;
    /* Copy code+data to the load address, then call its entry. The
       .fse was linked for this address (see tools/fse.ld), so no
       relocation is needed for position-dependent .fse v1 code. */
    for (i = 0; i < code_size; i++)
        ((uint8_t *)(uintptr_t)load_addr)[i] = file[64 + i];
    api.print = p_print_cb; api.print_u64 = p_u64; api.print_newline = p_nl;
    api.read_key = p_read_key; api.file_read = p_file_read; api.file_write = p_file_write;
    api.dir_list = p_dir_list; api.file_delete = p_file_delete; api.file_rename = p_file_rename; api.file_mkdir = p_file_mkdir; api.file_exists = p_file_exists; api.file_size = p_file_size;
    api.get_time = p_get_time; api.clear_screen = p_clear_screen; api.sleep_ms = p_sleep_ms;
    entry = (fse_entry_t)(uintptr_t)(load_addr + (entry_off - 64));
    return entry(&api);
}
int proc_run_fse(const uint8_t *file, uint32_t file_len)
{
    return proc_run_fse_at(file, file_len, FISIX_FSE_LOAD_ADDRESS);
}
