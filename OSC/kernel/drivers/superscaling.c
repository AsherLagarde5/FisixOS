/* Super Scaling — see superscaling.h. No driver, no GPU:
 * nearest-neighbour scaling from a 720p logical picture to
 * whatever native resolution the GOP framebuffer already has,
 * in plain CPU code, for any resolution at all. */
#include "superscaling.h"

static int ss_enabled = 1;
static int ss_active = 0;
static uint32_t ss_native_w = 0, ss_native_h = 0;
static uint32_t ss_scale_fp = 0; /* 16.16 fixed point */
static uint32_t ss_out_w = 0, ss_out_h = 0;
static uint32_t ss_off_x = 0, ss_off_y = 0;

void superscaling_set_enabled(int on) { ss_enabled = on ? 1 : 0; }
int superscaling_enabled(void) { return ss_enabled; }
int superscaling_active(void) { return ss_active; }
uint32_t superscaling_logical_w(void) { return ss_active ? SUPERSCALING_LOGICAL_W : ss_native_w; }
uint32_t superscaling_logical_h(void) { return ss_active ? SUPERSCALING_LOGICAL_H : ss_native_h; }
uint32_t superscaling_native_w(void) { return ss_native_w; }
uint32_t superscaling_native_h(void) { return ss_native_h; }
uint32_t superscaling_output_w(void) { return ss_out_w; }
uint32_t superscaling_output_h(void) { return ss_out_h; }
uint32_t superscaling_offset_x(void) { return ss_off_x; }
uint32_t superscaling_offset_y(void) { return ss_off_y; }
uint32_t superscaling_scale_fp(void) { return ss_scale_fp; }

void superscaling_init(uint32_t native_w, uint32_t native_h)
{
    uint64_t sx, sy, scale;
    ss_native_w = native_w; ss_native_h = native_h;
    ss_active = 0; ss_scale_fp = 0; ss_out_w = native_w; ss_out_h = native_h;
    ss_off_x = 0; ss_off_y = 0;
    /* Auto-detect: native comes from the GOP mode, nothing to
     * probe and no driver to load. Anything above the 32K cap
     * (or a zero size) switches scaling off: run native. */
    if (!ss_enabled || native_w == 0 || native_h == 0) return;
    if (native_w > SUPERSCALING_MAX_W || native_h > SUPERSCALING_MAX_H) return;
    if (native_w == SUPERSCALING_LOGICAL_W && native_h == SUPERSCALING_LOGICAL_H) return;
    /* Fractional scale, so odd sizes just work: 5K, 6K, 7K,
     * 200p, anything. Fit inside, never stretch. Rounded
     * fixed-point, so odd widths land exact (6K = 6144 wide,
     * not 6143) instead of losing a pixel to truncation. */
    sx = (((uint64_t)native_w << 16) + SUPERSCALING_LOGICAL_W / 2) / SUPERSCALING_LOGICAL_W;
    sy = (((uint64_t)native_h << 16) + SUPERSCALING_LOGICAL_H / 2) / SUPERSCALING_LOGICAL_H;
    scale = sx < sy ? sx : sy;
    if (scale == 0) return;
    ss_scale_fp = (uint32_t)scale;
    ss_out_w = (uint32_t)((SUPERSCALING_LOGICAL_W * scale + 32768) >> 16);
    ss_out_h = (uint32_t)((SUPERSCALING_LOGICAL_H * scale + 32768) >> 16);
    if (ss_out_w == 0 || ss_out_h == 0) return;
    if (ss_out_w > native_w) ss_out_w = native_w;
    if (ss_out_h > native_h) ss_out_h = native_h;
    ss_off_x = (native_w - ss_out_w) / 2;
    ss_off_y = (native_h - ss_out_h) / 2;
    ss_active = 1;
}

void superscaling_scale_rect(uint32_t *dst_fb, uint32_t dst_pitch,
                             const uint8_t *src, uint32_t src_pitch_px,
                             uint32_t src_x, uint32_t src_y,
                             uint32_t src_w, uint32_t src_h)
{
    uint32_t dx0, dy0, dx1, dy1, dx, dy;
    if (!ss_active || !dst_fb || !src || src_w == 0 || src_h == 0) return;
    if (dst_pitch == 0 || src_pitch_px == 0) return;
    /* Destination rect for this logical rect, clipped to the screen.
     * Mapped proportionally against the output size (not the
     * truncated scale factor), so a full-frame flush always
     * covers exactly the whole scaled picture — at 200p the
     * old scale-based math came up 1 pixel short. */
    dx0 = ss_off_x + (uint32_t)(((uint64_t)src_x * ss_out_w) / SUPERSCALING_LOGICAL_W);
    dy0 = ss_off_y + (uint32_t)(((uint64_t)src_y * ss_out_h) / SUPERSCALING_LOGICAL_H);
    dx1 = ss_off_x + (uint32_t)(((uint64_t)(src_x + src_w) * ss_out_w) / SUPERSCALING_LOGICAL_W);
    dy1 = ss_off_y + (uint32_t)(((uint64_t)(src_y + src_h) * ss_out_h) / SUPERSCALING_LOGICAL_H);
    if (dx1 > ss_native_w) dx1 = ss_native_w;
    if (dy1 > ss_native_h) dy1 = ss_native_h;
    if (dx1 <= dx0 || dy1 <= dy0) return;
    for (dy = dy0; dy < dy1; dy++) {
        uint32_t ly;
        const uint8_t *srow;
        uint32_t *drow;
        if (dy < ss_off_y) continue;
        /* Nearest neighbour, mapped proportionally across
         * this rect: the first and last dest pixels land on
         * the first and last source pixels, so odd scales
         * never lose a strip of the picture at the far edge
         * to fixed-point rounding. */
        ly = src_y + (uint32_t)(((uint64_t)(dy - dy0) * src_h) / (dy1 - dy0));
        if (ly >= src_y + src_h) ly = src_y + src_h - 1;
        srow = src + (uint64_t)(ly - src_y) * src_pitch_px * 4u;
        drow = dst_fb + (uint64_t)dy * dst_pitch;
        for (dx = dx0; dx < dx1; dx++) {
            uint32_t lx;
            const uint8_t *s;
            if (dx < ss_off_x) continue;
            lx = src_x + (uint32_t)(((uint64_t)(dx - dx0) * src_w) / (dx1 - dx0));
            if (lx >= src_x + src_w) lx = src_x + src_w - 1;
            s = srow + (uint64_t)(lx - src_x) * 4u;
            drow[dx] = (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16);
        }
    }
}

void superscaling_native_to_logical(int nx, int ny, int *lx, int *ly)
{
    int x, y;
    if (!lx || !ly) return;
    if (!ss_active) {
        x = nx; y = ny;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (ss_native_w && (uint32_t)x >= ss_native_w) x = (int)ss_native_w - 1;
        if (ss_native_h && (uint32_t)y >= ss_native_h) y = (int)ss_native_h - 1;
        *lx = x; *ly = y;
        return;
    }
    if (nx < (int)ss_off_x) x = 0;
    else if (nx >= (int)(ss_off_x + ss_out_w)) x = (int)SUPERSCALING_LOGICAL_W - 1;
    else x = (int)(((uint64_t)(nx - (int)ss_off_x) << 16) / ss_scale_fp);
    if (ny < (int)ss_off_y) y = 0;
    else if (ny >= (int)(ss_off_y + ss_out_h)) y = (int)SUPERSCALING_LOGICAL_H - 1;
    else y = (int)(((uint64_t)(ny - (int)ss_off_y) << 16) / ss_scale_fp);
    if (x < 0) x = 0;
    if (x >= (int)SUPERSCALING_LOGICAL_W) x = (int)SUPERSCALING_LOGICAL_W - 1;
    if (y < 0) y = 0;
    if (y >= (int)SUPERSCALING_LOGICAL_H) y = (int)SUPERSCALING_LOGICAL_H - 1;
    *lx = x; *ly = y;
}
