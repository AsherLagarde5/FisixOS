#ifndef GENERIC_DISPLAY_H
#define GENERIC_DISPLAY_H
#include <stdint.h>
/* Windows Basic Display Adapter equivalent for FisixOS.
 * No GPU-specific code: Intel Iris Xe, AMD Radeon, Nvidia, VirtIO-GPU,
 * QEMU std VGA all work the same way here, because the UEFI loader
 * already set up a GOP framebuffer. This driver just adopts it.
 * If GOP is absent, mode = TEXT (serial / VGA text fallback), and
 * boot must NOT fail for display. Accelerated GPU drivers are
 * optimizations on top, never required to boot. */
#define GENERIC_DISPLAY_NONE 0
#define GENERIC_DISPLAY_GOP 1
#define GENERIC_DISPLAY_TEXT 2
typedef struct { uint64_t base; uint32_t width, height, pitch; int mode; int is_generic; } generic_display_t;
int generic_display_init(uint64_t base, uint32_t width, uint32_t height, uint32_t pitch, generic_display_t *out);
int generic_display_present(const generic_display_t *d);
void generic_display_clear(generic_display_t *d, uint32_t colour);
#endif
