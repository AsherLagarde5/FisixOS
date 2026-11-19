#ifndef FISIX_SUPERSCALING_H
#define FISIX_SUPERSCALING_H
#include <stdint.h>
/* Super Scaling — FisixOS resolution scaling, 2026-11-19.
 *
 * The idea, in Asher's words: an option called Super Scaling
 * that automatically detects the resolution and doesn't need
 * a driver no matter what, so even if something is designed
 * for 720p it can go up to fucking 32K.
 *
 * HOW IT WORKS: the GUI is designed once, at 1280x720 (720p).
 * At boot, Super Scaling reads the native resolution the
 * firmware already set (the UEFI GOP mode — no driver, no
 * GPU, no mode-setting, just the framebuffer that is already
 * there) and scales the 720p picture to fit it, with
 * nearest-neighbour sampling, in plain CPU code. It works
 * for ANY native resolution, not just the normal ones:
 * 4K and 8K, the odd ones like 5K, 6K and 7K, all the way
 * up to 32K (32768 x 18432), and the weird low ones like
 * 200p too — scaling down as well as up, because the scale
 * factor is fractional fixed-point, never assumed to be a
 * whole number and never assumed to be a standard size.
 *
 * ASPECT: the picture is scaled to fit, letterboxed, never
 * stretched — a 16:9 design stays 16:9 on an ultrawide or
 * a square-ish display, with black bars filling the rest.
 *
 * THE OPTION: Super Scaling is an option, on by default.
 * superscaling_set_enabled(0) turns it off; the change
 * takes effect on the next lvgl_port_init (next boot).
 * With it off, the GUI runs at native resolution directly.
 *
 * HONEST LIMITS:
 * - Nearest-neighbour only: no bilinear/filtered scaling
 *   (that would need more CPU per pixel and is a later step).
 *   Downscaling to something like 200p drops pixels — the
 *   picture still shows, just coarser. Upscaling is crisp
 *   because source pixels just get bigger.
 * - Native resolution is capped at 32768 x 18432 (32K).
 *   Above that, Super Scaling switches itself off and the
 *   GUI runs native instead of writing past the framebuffer.
 * - Only the LVGL Start screen goes through Super Scaling
 *   in v1. The old gui.c GUI still draws at native
 *   resolution, per-pixel scalar.
 * - The framebuffer itself still has to exist and fit in
 *   RAM: a 32K framebuffer at 32bpp is ~2.4 GB, so a real
 *   32K display needs far more than the 128 MB minimum RAM.
 *   Super Scaling does not magic RAM into existence. */
#define SUPERSCALING_LOGICAL_W 1280u
#define SUPERSCALING_LOGICAL_H 720u
#define SUPERSCALING_MAX_W 32768u
#define SUPERSCALING_MAX_H 18432u

void superscaling_init(uint32_t native_w, uint32_t native_h);
void superscaling_set_enabled(int on);
int superscaling_enabled(void);   /* the option, 1 = on (default) */
int superscaling_active(void);    /* 1 = actually scaling right now */
/* Logical size LVGL should render at (720p when active). */
uint32_t superscaling_logical_w(void);
uint32_t superscaling_logical_h(void);
/* Geometry, for tests and for pointer mapping. */
uint32_t superscaling_native_w(void);
uint32_t superscaling_native_h(void);
uint32_t superscaling_output_w(void);  /* scaled picture width on screen */
uint32_t superscaling_output_h(void);  /* scaled picture height on screen */
uint32_t superscaling_offset_x(void);  /* left letterbox bar width */
uint32_t superscaling_offset_y(void);  /* top letterbox bar height */
uint32_t superscaling_scale_fp(void);  /* 16.16 fixed-point scale factor */
/* Scale a source rect (logical coords, 32bpp B,G,R,A bytes)
 * onto the native framebuffer (32bpp B,G,R,X). Alpha byte is
 * written 0, like gpu2d_copy_rect. No-op when inactive. */
void superscaling_scale_rect(uint32_t *dst_fb, uint32_t dst_pitch,
                             const uint8_t *src, uint32_t src_pitch_px,
                             uint32_t src_x, uint32_t src_y,
                             uint32_t src_w, uint32_t src_h);
/* Map a native framebuffer point to logical coords, clamped
 * into the logical picture (letterbox bars map to the edge). */
void superscaling_native_to_logical(int nx, int ny, int *lx, int *ly);
#endif
