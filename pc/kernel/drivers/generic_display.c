#include "generic_display.h"
int generic_display_init(uint64_t base, uint32_t w, uint32_t h, uint32_t p, generic_display_t *out){
    if(!out) return 0;
    out->base=base; out->width=w; out->height=h; out->pitch=p; out->is_generic=1;
    if(base && w && h && p){ out->mode=GENERIC_DISPLAY_GOP; return 1; }
    out->mode=GENERIC_DISPLAY_TEXT; return 1; /* text fallback still boots */
}
int generic_display_present(const generic_display_t *d){ return d && d->mode!=GENERIC_DISPLAY_NONE; }
void generic_display_clear(generic_display_t *d, uint32_t colour){
    uint32_t y, x;
    if(!d || d->mode!=GENERIC_DISPLAY_GOP || !d->base) return;
    for(y=0;y<d->height;y++){
        uint32_t *row=(uint32_t*)(uintptr_t)(d->base + (uint64_t)y*d->pitch);
        for(x=0;x<d->width;x++) row[x]=colour;
    }
}
