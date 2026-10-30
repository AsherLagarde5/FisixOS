/* User Update Files: checker + applier, see update.h.
   Checker runs first, always, and writes nothing on failure.
   Applier only touches files listed in the update, in order. */
#include "gba.h"
#include "update.h"
#include "demo_update.h"
#include "wrong_update.h"

static u32 update_u32(const unsigned char *d, int off) {
  return (u32)d[off] | ((u32)d[off + 1] << 8) | ((u32)d[off + 2] << 16) | ((u32)d[off + 3] << 24);
}
const char *update_platform_name(unsigned int platform) {
  if (platform == UPDATE_PLATFORM_GBA) return "GBA";
  if (platform == UPDATE_PLATFORM_UEFI) return "UEFI";
  if (platform == UPDATE_PLATFORM_C64) return "C64";
  if (platform == UPDATE_PLATFORM_ARM64) return "ARM64";
  return "UNKNOWN";
}
int update_check(const unsigned char *data, unsigned int len) {
  unsigned int i, off;
  u32 magic, version, platform, count, crc_stored, crc_calc;
  if (!data || len < UPDATE_HEADER_SIZE) return -4;
  magic = update_u32(data, 0);
  version = update_u32(data, 4);
  platform = update_u32(data, 8);
  count = update_u32(data, 12);
  crc_stored = update_u32(data, 16);
  if (magic != UPDATE_MAGIC) return -1;
  if (version != UPDATE_VERSION) return -2;
  if (platform != UPDATE_PLATFORM_CURRENT) return -3;
  if (count > 64) return -6;
  /* CRC over all entry bytes, after the header. */
  crc_calc = 0xFFFF;
  for (i = UPDATE_HEADER_SIZE; i < len; i++) {
    crc_calc ^= (u32)data[i] << 8;
    {
      int b;
      for (b = 0; b < 8; b++) {
        if (crc_calc & 0x8000) crc_calc = (crc_calc << 1) ^ 0x1021;
        else crc_calc <<= 1;
      }
    }
    crc_calc &= 0xFFFF;
  }
  if (crc_calc != (crc_stored & 0xFFFF)) return -5;
  off = UPDATE_HEADER_SIZE;
  for (i = 0; i < count; i++) {
    u32 action, data_len;
    if (off + 40 > len) return -4;
    action = update_u32(data, (int)off + 32);
    data_len = update_u32(data, (int)off + 36);
    if (action > UPDATE_ACTION_DELETE) return -6;
    if (action == UPDATE_ACTION_DELETE && data_len != 0) return -6;
    if (data_len > 512) return -6; /* GBA cart file limit */
    off += 40;
    if (off + data_len > len) return -4;
    off += data_len;
  }
  if (off != len) return -4;
  return 0;
}
int update_apply(const unsigned char *data, unsigned int len,
                 int *applied, int *skipped, int *deleted) {
  int rc, app = 0, skp = 0, del = 0;
  unsigned int i, off, count;
  rc = update_check(data, len);
  if (rc != 0) return rc;
  count = update_u32(data, 12);
  off = UPDATE_HEADER_SIZE;
  for (i = 0; i < count; i++) {
    char name[32];
    int j;
    u32 action, data_len;
    for (j = 0; j < 32; j++) name[j] = (char)data[off + j];
    name[31] = 0;
    action = update_u32(data, (int)off + 32);
    data_len = update_u32(data, (int)off + 36);
    off += 40;
    if (action == UPDATE_ACTION_DELETE) {
      if (fs_delete(name) == 0) del++;
      else skp++;
    } else if (action == UPDATE_ACTION_ADD) {
      if (fs_file_exists(name)) {
        skp++; /* ADD only if not present, otherwise skip */
      } else {
        if (fs_write(name, data + off, data_len) == 0) app++;
        else skp++;
      }
    } else { /* REPLACE: overwrite if present, add if not */
      if (fs_write(name, data + off, data_len) == 0) app++;
      else skp++;
    }
    off += data_len;
  }
  if (applied) *applied = app;
  if (skipped) *skipped = skp;
  if (deleted) *deleted = del;
  return app;
}
static int update_name_eq(const char *a, const char *b) {
  int i;
  for (i = 0; a[i] || b[i]; i++) {
    char ca = a[i], cb = b[i];
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return 0;
  }
  return 1;
}
static const struct { const char *name; const unsigned char *data; unsigned int len; } update_embedded[] = {
  { "DEMO.UPDATE", demo_update_data, demo_update_len },
  { "WRONG.UPDATE", wrong_update_data, wrong_update_len },
};
int update_list_embedded(char names[][32], int max) {
  int i, n = 0;
  for (i = 0; i < (int)(sizeof(update_embedded) / sizeof(update_embedded[0])) && n < max; i++) {
    int j;
    for (j = 0; update_embedded[i].name[j] && j < 31; j++) names[n][j] = update_embedded[i].name[j];
    names[n][j] = 0;
    n++;
  }
  return n;
}
int update_run_file_counts(const char *name, int *applied, int *skipped, int *deleted, unsigned int *platform_out) {
  static u8 buf[512];
  int n, i;
  if (!name) return -4;
  if (platform_out) *platform_out = 0;
  n = fs_read(name, buf, sizeof(buf));
  if (n >= 0) {
    if (platform_out && n >= 12) *platform_out = update_u32(buf, 8);
    return update_apply(buf, (unsigned int)n, applied, skipped, deleted);
  }
  for (i = 0; i < (int)(sizeof(update_embedded) / sizeof(update_embedded[0])); i++)
    if (update_name_eq(name, update_embedded[i].name)) {
      if (platform_out) *platform_out = update_u32(update_embedded[i].data, 8);
      return update_apply(update_embedded[i].data, update_embedded[i].len, applied, skipped, deleted);
    }
  return -7; /* not found, distinct from checker errors */
}
int update_run_file(const char *name) {
  return update_run_file_counts(name, 0, 0, 0, 0);
}
