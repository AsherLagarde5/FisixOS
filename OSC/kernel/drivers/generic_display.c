#include "generic_display.h"
#include "gpu2d.h"
int generic_display_init(uint64_t base, uint32_t w, uint32_t h, uint32_t p, generic_display_t *out){
    if(!out) return 0;
    out->base=base; out->width=w; out->height=h; out->pitch=p; out->is_generic=1;
    if(base && w && h && p){ out->mode=GENERIC_DISPLAY_GOP; return 1; }
    out->mode=GENERIC_DISPLAY_TEXT; return 1; /* text fallback still boots */
}
int generic_display_present(const generic_display_t *d){ return d && d->mode!=GENERIC_DISPLAY_NONE; }
void generic_display_clear(generic_display_t *d, uint32_t colour){
    if(!d || d->mode!=GENERIC_DISPLAY_GOP || !d->base) return;
    /* 2026-11-19: accelerated clear via gpu2d (SSE2 fill when
     * the CPU has it, scalar otherwise). This also fixes a
     * stride bug in the old loop: pitch is pixels-per-scanline
     * (GOP PixelsPerScanLine), but the old code added y*pitch
     * as a BYTE offset, so rows overlapped whenever pitch was
     * not width*4 bytes. gpu2d_fill_rect strides by pitch
     * pixels, like lvgl_port.c and gui.c do. */
    gpu2d_fill_rect((uint32_t *)(uintptr_t)d->base, d->pitch,
                    0, 0, d->width, d->height, colour);
}
