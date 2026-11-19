/* vfs.c - mounted-volume layer (see vfs.h).
 * Original FisixOS code. Freestanding: no libc. */
#include "vfs.h"
#include "fat.h"

static uint8_t *g_img = 0;
static size_t g_len = 0;

int vfs_mount(uint8_t *img, size_t len)
{
    int is32 = 0;
    if (!img || len==0) return -1;
    if (!fat_probe(img, len, &is32) || !is32) return -1;
    g_img = img; g_len = len;
    return 0;
}
void vfs_unmount(void) { g_img = 0; g_len = 0; }
int vfs_mounted(void) { return g_img != 0; }
size_t vfs_volume_len(void) { return g_len; }

int vfs_read(const char *path, uint8_t *buf, size_t cap, uint32_t *out_len)
{
    if (!g_img || !path || !buf) return -1;
    return fat_read_file(g_img, g_len, path, buf, cap, out_len);
}
int vfs_write(const char *path, const uint8_t *data, uint32_t len)
{
    if (!g_img || !path) return -1;
    return fat_write_file(g_img, g_len, path, data, len);
}
int vfs_delete(const char *path)
{
    if (!g_img || !path) return -1;
    return fat_delete_file(g_img, g_len, path);
}
int vfs_mkdir(const char *path)
{
    if (!g_img || !path) return -1;
    return fat_mkdir(g_img, g_len, path);
}
int vfs_exists(const char *path)
{
    if (!g_img || !path) return 0;
    return fat_exists(g_img, g_len, path);
}
int32_t vfs_size(const char *path)
{
    if (!g_img || !path) return -1;
    return fat_file_size(g_img, g_len, path);
}
int vfs_list(const char *path, char *out, size_t cap)
{
    if (!g_img || !path || !out) return -1;
    return fat_list_dir(g_img, g_len, path, out, cap);
}
