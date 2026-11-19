/* gpu2d — 2D blit acceleration. See gpu2d.h for what this is
 * and is not: SIMD (SSE2) fill/copy today, no GPU hardware yet. */
#include "gpu2d.h"
#include "cpu.h"
#ifdef __SSE2__
#include <emmintrin.h>
#endif

int gpu2d_accelerated(void) { return cpu_has_sse2(); }

const char *gpu2d_backend_name(void)
{
    return cpu_has_sse2() ? "SSE2" : "scalar";
}

void gpu2d_fill_rect(uint32_t *fb, uint32_t pitch,
                     uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t colour)
{
    uint32_t row;
    if (!fb || w == 0 || h == 0 || pitch == 0) return;
    if (x >= pitch) return;
    if (w > pitch - x) w = pitch - x; /* clamp to the row */
    if (cpu_has_sse2()) {
#ifdef __SSE2__
        __m128i v = _mm_set1_epi32((int)colour);
        for (row = 0; row < h; row++) {
            uint32_t *dst = fb + (uint64_t)(y + row) * pitch + x;
            uint32_t i = 0;
            /* 4 pixels at a time. _mm_storeu_*: the row start
             * need not be 16-byte aligned (x can be odd). */
            for (; i + 4 <= w; i += 4)
                _mm_storeu_si128((__m128i *)(dst + i), v);
            for (; i < w; i++) dst[i] = colour;
        }
        return;
#endif
    }
    for (row = 0; row < h; row++) {
        uint32_t *dst = fb + (uint64_t)(y + row) * pitch + x;
        uint32_t i;
        for (i = 0; i < w; i++) dst[i] = colour;
    }
}

void gpu2d_copy_rect(uint32_t *dst_fb, uint32_t dst_pitch,
                     uint32_t dx, uint32_t dy,
                     const uint8_t *src, uint32_t src_pitch_px,
                     uint32_t w, uint32_t h)
{
    uint32_t row;
    if (!dst_fb || !src || w == 0 || h == 0) return;
    if (dst_pitch == 0 || src_pitch_px == 0) return;
    if (dx >= dst_pitch) return;
    if (w > dst_pitch - dx) w = dst_pitch - dx;
    if (w > src_pitch_px) w = src_pitch_px;
    if (cpu_has_sse2()) {
#ifdef __SSE2__
        /* Mask the alpha byte out of each pixel: LVGL A becomes
         * GOP X = 0, exactly like the old byte loop wrote. */
        const __m128i mask = _mm_set1_epi32(0x00FFFFFF);
        for (row = 0; row < h; row++) {
            const uint8_t *s = src + (uint64_t)row * src_pitch_px * 4u;
            uint32_t *d = dst_fb + (uint64_t)(dy + row) * dst_pitch + dx;
            uint32_t i = 0;
            for (; i + 4 <= w; i += 4) {
                __m128i px = _mm_loadu_si128((const __m128i *)(s + (uint64_t)i * 4u));
                px = _mm_and_si128(px, mask);
                _mm_storeu_si128((__m128i *)(d + i), px);
            }
            for (; i < w; i++)
                d[i] = (uint32_t)s[i * 4] |
                       ((uint32_t)s[i * 4 + 1] << 8) |
                       ((uint32_t)s[i * 4 + 2] << 16);
        }
        return;
#endif
    }
    /* Scalar fallback: identical pixels to the old flush loop. */
    for (row = 0; row < h; row++) {
        const uint8_t *s = src + (uint64_t)row * src_pitch_px * 4u;
        uint8_t *d = (uint8_t *)(dst_fb + (uint64_t)(dy + row) * dst_pitch + dx);
        uint32_t x;
        for (x = 0; x < w; x++) {
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 0;
            s += 4; d += 4;
        }
    }
}
