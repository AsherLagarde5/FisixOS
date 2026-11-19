#ifndef FISIX_GPU2D_H
#define FISIX_GPU2D_H
#include <stdint.h>
/* gpu2d — FisixOS 2D blit acceleration, 2026-11-19.
 *
 * WHAT THIS IS: the kernel's 2D blit engine — solid fills and
 * rectangular copies of 32bpp pixels. It is the interface a real
 * GPU blitter backend would sit behind, and it is what the LVGL
 * flush path (lvgl_port.c) and generic_display_clear() now use
 * instead of byte-by-byte loops.
 *
 * WHAT THIS IS NOT (honest): this is NOT a GPU driver. No GPU
 * MMIO registers are touched, no command buffers are submitted,
 * no Iris Xe / Radeon firmware is loaded. The first backend is
 * CPU SIMD: SSE2 128-bit fill/copy, used only when
 * cpu_has_sse2() says the CPU has it, with a scalar fallback
 * otherwise. On the Latitude 5420 (Tiger Lake, SSE2/AVX) the
 * SIMD path runs; on a CPU without SSE2 the same pixels land
 * via the scalar path.
 *
 * WHY SIMD FIRST: the real GPU options are all large. Intel
 * Iris Xe 2D (BCS blitter) needs BAR0 MMIO, GTT setup, a ring
 * buffer and batch submission; a full amdgpu-style driver is
 * bigger still. UEFI GOP Blt is NOT an option here: it is a
 * UEFI BootServices protocol, unavailable after
 * ExitBootServices, so the kernel cannot call it. The blit
 * API in this file is the seam: a future Iris Xe BCS or AMD
 * SDMA backend implements gpu2d_fill_rect/gpu2d_copy_rect
 * behind it, and the callers do not change.
 *
 * PIXEL FORMAT: framebuffer pixels are 32bpp B,G,R,X (GOP
 * BGRX); source pixels are 32bpp B,G,R,A (LVGL). The X byte
 * is reserved/ignored by GOP hardware, but gpu2d_copy_rect
 * zeroes it anyway, so framebuffer contents match the old
 * byte-by-byte flush loop exactly.
 *
 * STRIDES: pitch arguments are in PIXELS per scanline
 * (GOP PixelsPerScanLine), not bytes — same convention as
 * lvgl_port.c and gui.c. */
void gpu2d_fill_rect(uint32_t *fb, uint32_t pitch,
                     uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t colour);
void gpu2d_copy_rect(uint32_t *dst_fb, uint32_t dst_pitch,
                     uint32_t dx, uint32_t dy,
                     const uint8_t *src, uint32_t src_pitch_px,
                     uint32_t w, uint32_t h);
int gpu2d_accelerated(void); /* 1 = SIMD backend active, 0 = scalar */
const char *gpu2d_backend_name(void); /* "SSE2" or "scalar" */
#endif
