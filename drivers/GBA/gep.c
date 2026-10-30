/* GEP loader: Game Boy Executable Program, v1.
   Loads ARM code from a .gep file into EWRAM at 0x02008000,
   zeroes its .bss, calls gep_main(api), returns its code.
   Cart files first, then apps embedded in the ROM (see
   src/hello_gep.h, generated at build time). A PC .fse file
   is rejected here: its magic is FSE1, not GEP1, and its
   code is x86-64, not ARM. */
#include "gba.h"
#include "gep.h"
#include "hello_gep.h"
#include "sender_gep.h"
#include "receiver_gep.h"

/* ---- LCD console for GEP apps ---- */
static int gep_cx = 8, gep_cy = 8;
static void gep_console_clear(void) {
  video_clear(RGB15(0, 0, 10));
  gep_cx = 8; gep_cy = 8;
}
static void gep_console_newline(void) {
  gep_cx = 8; gep_cy += 8;
  if (gep_cy > 152) gep_console_clear();
}
static void gep_api_print(const char *s) {
  char ch[2];
  int i;
  if (!s) return;
  for (i = 0; s[i]; i++) {
    char c = s[i];
    if (c == '\n' || c == '\r') { gep_console_newline(); continue; }
    ch[0] = c; ch[1] = 0;
    video_text(gep_cx, gep_cy, ch, RGB15(31, 31, 31));
    gep_cx += 8;
    if (gep_cx > 232) gep_console_newline();
  }
}
static void gep_print_u64_impl(unsigned long long v) {
  char tmp[24], out[24];
  int i = 0, j = 0;
  if (v == 0) { gep_api_print("0"); return; }
  while (v > 0 && i < 20) { tmp[i++] = (char)('0' + (int)(v % 10)); v /= 10; }
  while (i > 0) out[j++] = tmp[--i];
  out[j] = 0;
  gep_api_print(out);
}
static void gep_api_print_newline(void) { gep_console_newline(); }
static int gep_api_read_key(void) {
  u16 old = input_read(), k;
  while (1) {
    video_wait_vblank();
    k = input_read();
    if (input_pressed(old, k)) return (int)input_pressed(old, k);
    old = k;
  }
}
static int gep_api_file_read(const char *path, char *buf, unsigned int buf_size) {
  int n;
  if (!path || !buf || buf_size == 0) return -1;
  n = fs_read(path, (u8*)buf, buf_size - 1);
  if (n < 0) return -1;
  buf[n] = 0;
  return n;
}
static int gep_api_file_write(const char *path, const char *buf, unsigned int len) {
  if (!path || !buf) return -1;
  return fs_write(path, (const u8*)buf, len);
}
static int gep_api_dir_list(const char *path, char *buf, unsigned int buf_size) {
  char names[16][32];
  int n, i, pos = 0;
  (void)path; /* flat cart fs: one directory, path is ignored */
  if (!buf || buf_size == 0) return -1;
  n = fs_list(names, 16);
  for (i = 0; i < n; i++) {
    int j;
    for (j = 0; names[i][j]; j++) {
      if (pos + 1 >= (int)buf_size) break;
      buf[pos++] = names[i][j];
    }
    if (pos + 1 < (int)buf_size) buf[pos++] = '\n';
  }
  if (pos < (int)buf_size) buf[pos] = 0;
  return n;
}
static int gep_api_file_delete(const char *path) { (void)path; return -1; }
static int gep_api_file_rename(const char *o, const char *n) { (void)o; (void)n; return -1; }
static int gep_api_file_mkdir(const char *path) { (void)path; return -1; }
static int gep_api_file_exists(const char *path) {
  if (!path) return 0;
  return fs_file_exists(path) ? 1 : 0;
}
static int gep_api_file_size(const char *path) {
  u8 tmp[512];
  int n;
  if (!path) return -1;
  n = fs_read(path, tmp, sizeof(tmp));
  return n;
}
static void gep_api_get_time(unsigned int *y, unsigned int *mo, unsigned int *d,
                             unsigned int *h, unsigned int *mi, unsigned int *s) {
  /* No RTC in this build: all zero, honestly. */
  if (y) *y = 0;
  if (mo) *mo = 0;
  if (d) *d = 0;
  if (h) *h = 0;
  if (mi) *mi = 0;
  if (s) *s = 0;
}
static void gep_api_clear_screen(void) { gep_console_clear(); }
static void gep_api_sleep_ms(unsigned int ms) {
  u32 start = timer_ticks();
  u32 wait = ms * 16384u / 1000u;
  while (timer_ticks() - start < wait) { }
}
static int gep_api_pipe_write(const char *channel, const char *data, unsigned int len) {
  return pipe_write(channel, (const u8*)data, len);
}
static int gep_api_pipe_read(const char *channel, char *buf, unsigned int buf_size) {
  return pipe_read(channel, (u8*)buf, buf_size);
}
static int gep_api_pipe_clear(const char *channel) {
  return pipe_clear(channel);
}

static gep_api_t gep_api = {
  gep_api_print,
  gep_print_u64_impl,
  gep_api_print_newline,
  gep_api_read_key,
  gep_api_file_read,
  gep_api_file_write,
  gep_api_dir_list,
  gep_api_file_delete,
  gep_api_file_rename,
  gep_api_file_mkdir,
  gep_api_file_exists,
  gep_api_file_size,
  gep_api_get_time,
  gep_api_clear_screen,
  gep_api_sleep_ms,
  gep_api_pipe_write,
  gep_api_pipe_read,
  gep_api_pipe_clear
};

/* Embedded apps: generated header, one entry for now. */
static const struct { const char *name; const unsigned char *data; unsigned int len; } gep_embedded[] = {
  { "HELLO.GEP", hello_gep_data, hello_gep_len },
  { "SENDER.GEP", sender_gep_data, sender_gep_len },
  { "RECEIVER.GEP", receiver_gep_data, receiver_gep_len },
};
#define GEP_EMBEDDED_COUNT (sizeof(gep_embedded) / sizeof(gep_embedded[0]))

int gep_list_embedded(char names[][32], int max) {
  int i, n = 0;
  for (i = 0; i < (int)GEP_EMBEDDED_COUNT && n < max; i++) {
    int j;
    for (j = 0; gep_embedded[i].name[j] && j < 31; j++) names[n][j] = gep_embedded[i].name[j];
    names[n][j] = 0;
    n++;
  }
  return n;
}
static int name_eq_up(const char *a, const char *b) {
  int i;
  for (i = 0; a[i] || b[i]; i++) {
    char ca = a[i], cb = b[i];
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return 0;
  }
  return 1;
}
int gep_run_embedded(const char *name) {
  int i;
  if (!name) return -3;
  for (i = 0; i < (int)GEP_EMBEDDED_COUNT; i++)
    if (name_eq_up(name, gep_embedded[i].name))
      return gep_run_data(gep_embedded[i].data, gep_embedded[i].len);
  return -3;
}
int gep_run_file(const char *name) {
  /* Cart files are 512 bytes max, so a small GEP fits; a
     larger one must be embedded in ROM or sent via Link. */
  static u8 buf[512];
  int n;
  if (!name) return -3;
  n = fs_read(name, buf, sizeof(buf));
  if (n < 0) return gep_run_embedded(name);
  return gep_run_data(buf, (unsigned int)n);
}
int gep_run_data(const unsigned char *data, unsigned int len) {
  gep_header_t hdr;
  unsigned int i, bss_size;
  u8 *load;
  gep_entry_t entry;
  int rc;
  if (!data || len < GEP_HEADER_SIZE) return -1;
  /* Header is packed, copy field by field to avoid alignment. */
  hdr.magic = (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
  hdr.version = (u32)data[4] | ((u32)data[5] << 8) | ((u32)data[6] << 16) | ((u32)data[7] << 24);
  hdr.entry_offset = (u32)data[8] | ((u32)data[9] << 8) | ((u32)data[10] << 16) | ((u32)data[11] << 24);
  hdr.code_size = (u32)data[12] | ((u32)data[13] << 8) | ((u32)data[14] << 16) | ((u32)data[15] << 24);
  hdr.api_version = (u32)data[16] | ((u32)data[17] << 8) | ((u32)data[18] << 16) | ((u32)data[19] << 24);
  bss_size = (u32)data[20] | ((u32)data[21] << 8) | ((u32)data[22] << 16) | ((u32)data[23] << 24);
  if (hdr.magic != GEP_MAGIC) return -1; /* PC FSE1 lands here, rejected */
  if (hdr.version != GEP_VERSION || hdr.api_version > GEP_API_VERSION) return -1;
  if (hdr.code_size == 0 || hdr.code_size > GEP_MAX_CODE_SIZE) return -2;
  if (bss_size > GEP_MAX_BSS_SIZE) return -4;
  if (hdr.entry_offset < GEP_HEADER_SIZE ||
      hdr.entry_offset >= GEP_HEADER_SIZE + hdr.code_size) return -2;
  if (len < GEP_HEADER_SIZE + hdr.code_size) return -2;
  load = (u8*)GEP_LOAD_ADDRESS;
  for (i = 0; i < hdr.code_size; i++) load[i] = data[GEP_HEADER_SIZE + i];
  for (i = 0; i < bss_size; i++) load[hdr.code_size + i] = 0;
  entry = (gep_entry_t)(GEP_LOAD_ADDRESS + (hdr.entry_offset - GEP_HEADER_SIZE));
  gep_console_clear();
  rc = entry(&gep_api);
  return rc;
}
