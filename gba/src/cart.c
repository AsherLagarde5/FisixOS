/* Cart: standard 32 MB ROM window + 32 KB SRAM save.
   SRAM is on the cartridge, so a profile saved here travels
   with the cart to another GBA. In mGBA this becomes the
   .sav file next to the ROM. No 8 GB paging in this build. */
#include "gba.h"

const u8 *cart_rom_ptr(u32 offset) {
  return (const u8*)(0x08000000u + (offset & 0x01FFFFFFu));
}

#define MAX_FILES 16
#define SRAM_MAGIC0 'F'
#define SRAM_MAGIC1 'S'
#define SRAM_MAGIC2 'X'
#define SRAM_MAGIC3 'G'
/* SRAM layout: magic 4, count 1, pad 3, then entries:
   name 32, len 4, data 512  = 548 bytes each, 16 * 548 = 8768,
   well under 32768. */
#define ENTRY_SIZE 548

struct file_ent { char name[32]; u32 len; u8 data[512]; int used; };
static struct file_ent files[MAX_FILES];
static int fs_ready = 0;

static int name_eq(const char *a, const char *b) {
  int i;
  for (i = 0; a[i] || b[i]; i++)
    if (a[i] != b[i]) return 0;
  return 1;
}
static void name_copy(char *d, const char *s) {
  int i;
  for (i = 0; s[i] && i < 31; i++) d[i] = s[i];
  d[i] = 0;
}

void fs_flush_to_sram(void) {
  int i, j, off;
  if (!fs_ready) return;
  SRAM[0] = SRAM_MAGIC0; SRAM[1] = SRAM_MAGIC1;
  SRAM[2] = SRAM_MAGIC2; SRAM[3] = SRAM_MAGIC3;
  {
    int count = 0;
    for (i = 0; i < MAX_FILES; i++) if (files[i].used) count++;
    SRAM[4] = (u8)count;
  }
  SRAM[5] = 0; SRAM[6] = 0; SRAM[7] = 0;
  off = 8;
  for (i = 0; i < MAX_FILES; i++) {
    if (!files[i].used) continue;
    for (j = 0; j < 32; j++)
      SRAM[off + j] = (u8)(j < 32 ? files[i].name[j] : 0);
    SRAM[off + 32] = (u8)(files[i].len & 0xFF);
    SRAM[off + 33] = (u8)((files[i].len >> 8) & 0xFF);
    SRAM[off + 34] = (u8)((files[i].len >> 16) & 0xFF);
    SRAM[off + 35] = (u8)((files[i].len >> 24) & 0xFF);
    for (j = 0; j < 512; j++)
      SRAM[off + 36 + j] = (j < (int)files[i].len) ? files[i].data[j] : 0;
    off += ENTRY_SIZE;
  }
}

int cart_fs_init(void) {
  int i, j, off;
  for (i = 0; i < MAX_FILES; i++) files[i].used = 0;
  fs_ready = 1;
  if (SRAM[0] == SRAM_MAGIC0 && SRAM[1] == SRAM_MAGIC1 &&
      SRAM[2] == SRAM_MAGIC2 && SRAM[3] == SRAM_MAGIC3) {
    int count = SRAM[4];
    if (count > MAX_FILES) count = MAX_FILES;
    off = 8;
    for (i = 0; i < count; i++) {
      for (j = 0; j < 32; j++) files[i].name[j] = (char)SRAM[off + j];
      files[i].name[31] = 0;
      files[i].len = (u32)SRAM[off + 32] |
                     ((u32)SRAM[off + 33] << 8) |
                     ((u32)SRAM[off + 34] << 16) |
                     ((u32)SRAM[off + 35] << 24);
      if (files[i].len > 512) files[i].len = 512;
      for (j = 0; j < 512; j++) files[i].data[j] = SRAM[off + 36 + j];
      files[i].used = 1;
      off += ENTRY_SIZE;
    }
  }
  return 1;
}
int fs_file_exists(const char *name) {
  int i;
  if (!fs_ready) return 0;
  for (i = 0; i < MAX_FILES; i++)
    if (files[i].used && name_eq(name, files[i].name)) return 1;
  return 0;
}
int fs_list(char names[][32], int max) {
  int i, n = 0;
  if (!fs_ready) return 0;
  for (i = 0; i < MAX_FILES && n < max; i++)
    if (files[i].used) name_copy(names[n++], files[i].name);
  return n;
}
int fs_write(const char *name, const u8 *data, u32 len) {
  int i, slot = -1;
  if (!fs_ready || len > 512) return -1;
  for (i = 0; i < MAX_FILES; i++)
    if (files[i].used && name_eq(name, files[i].name)) { slot = i; break; }
  if (slot < 0)
    for (i = 0; i < MAX_FILES; i++)
      if (!files[i].used) { slot = i; break; }
  if (slot < 0) return -1;
  name_copy(files[slot].name, name);
  for (i = 0; (u32)i < len; i++) files[slot].data[i] = data[i];
  files[slot].len = len;
  files[slot].used = 1;
  fs_flush_to_sram();
  return 0;
}
int fs_delete(const char *name) {
  int i;
  if (!fs_ready) return -1;
  for (i = 0; i < MAX_FILES; i++)
    if (files[i].used && name_eq(name, files[i].name)) {
      files[i].used = 0;
      files[i].len = 0;
      fs_flush_to_sram();
      return 0;
    }
  return -1;
}
int fs_read(const char *name, u8 *out, u32 maxlen) {
  int i, j;
  if (!fs_ready) return -1;
  for (i = 0; i < MAX_FILES; i++)
    if (files[i].used && name_eq(name, files[i].name)) {
      u32 n = files[i].len < maxlen ? files[i].len : maxlen;
      for (j = 0; (u32)j < n; j++) out[j] = files[i].data[j];
      return (int)n;
    }
  return -1;
}
