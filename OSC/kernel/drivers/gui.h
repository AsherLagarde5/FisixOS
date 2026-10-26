#ifndef FISIX_GUI_H
#define FISIX_GUI_H
#include <stdint.h>

/* Real pixel GUI, not ASCII, not a text console.
 * Draws directly to the UEFI GOP framebuffer the loader found
 * (see boot_info.h). 32 bits per pixel, BGRX layout in memory,
 * so a colour is (R<<16)|(G<<8)|B as a uint32_t.
 * The GUI lives in the kernel, after ExitBootServices, in
 * kernel_startup(), replacing the old idle halt loop.
 * Input is PS/2 mouse + keyboard polling (no interrupts yet,
 * kernel runs with cli), see gui_poll_input().
 */
void gui_init(uint64_t fb_base, uint32_t width, uint32_t height, uint32_t pitch);
int gui_is_ready(void);
void gui_draw(void);
void gui_mouse_move(int dx, int dy);
void gui_mouse_click(int left_down);
void gui_usb_move(int dx, int dy, int buttons);
void gui_touch_set(int x, int y, int down);
uint32_t gui_width(void);
uint32_t gui_height(void);
int gui_load_jpeg(const uint8_t *data, uint32_t len);
/* Poll one PS/2 byte if available. Returns 1 if a mouse packet
   was completed and the screen was redrawn, or a key was handled.
   Call this in a tight loop from kernel_startup(). */
int gui_poll_input(void);
void gui_tick(void);

#endif
