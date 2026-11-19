/* FisixOS LVGL port layer.
 *
 * Wires LVGL v9.3.0 (MIT) to the UEFI GOP framebuffer:
 *  - display: partial render buffer (static, 256 KiB), flush callback
 *    copies LVGL pixels to the GOP framebuffer. LVGL 32bpp colour
 *    bytes in memory are B,G,R,A; GOP BGRX bytes are B,G,R,X, so the
 *    R/G/B bytes copy straight across and the 4th byte is written 0.
 *    A pure-red LVGL pixel therefore lands as 00 00 FF 00 in the fb.
 *  - pointer: LVGL indev reads the existing GUI cursor state
 *    (gui_cursor_x/y + gui_pointer_down from gui.c); the kernel
 *    input paths also feed press/release directly via
 *    lvgl_port_feed_pointer(), so a click is not lost between polls.
 *  - tick: lv_tick_inc() + lv_timer_handler() driven from
 *    lvgl_port_tick(), called from the kernel main loop. There is
 *    no millisecond timer in the GUI poll loop, so the tick is
 *    advanced in fixed steps (see lvgl_port_tick).
 *  - memory: LVGL's own TLSF allocator on a static 1 MiB pool
 *    (LV_MEM_SIZE in lv_conf.h). No kernel malloc is used.
 */
#ifndef FISIX_LVGL_PORT_H
#define FISIX_LVGL_PORT_H
#include <stdint.h>

void lvgl_port_init(uint64_t fb_base, uint32_t width, uint32_t height, uint32_t pitch);
int  lvgl_is_active(void);          /* 1 while the LVGL screen owns the fb */
void lvgl_port_suspend(void);       /* old-GUI app opened: stop LVGL drawing */
void lvgl_port_resume(void);        /* old-GUI app closed: LVGL owns the fb again */
void lvgl_port_tick(void);          /* lv_tick_inc + lv_timer_handler, call in main loop */
void lvgl_port_feed_pointer(int x, int y, int pressed);
void lvgl_port_resume(void);
int lvgl_port_is_suspended(void);

#endif
