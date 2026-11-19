/* FisixOS LVGL port layer — see lvgl_port.h. */
#include "lvgl_port.h"
#include "gui.h"
#include "gpu2d.h"
#include "superscaling.h"

#include "lvgl.h"

/* Partial render buffer: 64K pixels x 4 bytes = 256 KiB, static. */
#define LVGL_PORT_BUF_PX 65536u
static uint32_t lvgl_port_buf[LVGL_PORT_BUF_PX];
static uint32_t *lvgl_port_fb = 0;
static uint32_t lvgl_port_w = 0, lvgl_port_h = 0, lvgl_port_pitch = 0;
static lv_display_t *lvgl_port_disp = 0;
static lv_indev_t *lvgl_port_indev = 0;
static int lvgl_port_active = 0;
static int lvgl_port_suspended = 0;
/* Pointer state fed by gui.c input paths; -1 = never fed. */
static int lvgl_port_px = -1, lvgl_port_py = -1, lvgl_port_pressed = 0;

static void lvgl_port_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w, h;
    (void)disp;
    if (!lvgl_port_fb || !px_map) { lv_display_flush_ready(disp); return; }
    if (area->x2 < area->x1 || area->y2 < area->y1) { lv_display_flush_ready(disp); return; }
    /* The flush copy is the hot path of the Start screen: every
     * redrawn area used to be copied byte-by-byte (4 stores per
     * pixel). gpu2d_copy_rect does it 4 pixels at a time with
     * SSE2 when the CPU has it, scalar otherwise, and zeroes
     * the GOP X byte exactly like the old loop did.
     * With Super Scaling on, LVGL renders at 720p logical and
     * this flush scales the area up (or down) to the native
     * framebuffer instead — no driver, any resolution. */
    w = (uint32_t)(area->x2 - area->x1 + 1);
    h = (uint32_t)(area->y2 - area->y1 + 1);
    if (superscaling_active())
        superscaling_scale_rect(lvgl_port_fb, lvgl_port_pitch,
                                px_map, w,
                                (uint32_t)area->x1, (uint32_t)area->y1,
                                w, h);
    else
        gpu2d_copy_rect(lvgl_port_fb, lvgl_port_pitch,
                        (uint32_t)area->x1, (uint32_t)area->y1,
                        px_map, w, w, h);
    lv_display_flush_ready(disp);
}

static void lvgl_port_indev_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    int x, y, pressed;
    (void)indev;
    if (lvgl_port_px >= 0) {
        x = lvgl_port_px; y = lvgl_port_py; pressed = lvgl_port_pressed;
    } else {
        /* gui cursor is in native framebuffer coords; the LVGL
         * display is logical (720p when Super Scaling is on),
         * so map it down. lvgl_port_px/py are already logical
         * (converted in lvgl_port_feed_pointer). */
        superscaling_native_to_logical(gui_cursor_x(), gui_cursor_y(), &x, &y);
        pressed = gui_pointer_down();
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint32_t)x >= lvgl_port_w) x = (int)lvgl_port_w - 1;
    if ((uint32_t)y >= lvgl_port_h) y = (int)lvgl_port_h - 1;
    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void lvgl_port_init(uint64_t fb_base, uint32_t width, uint32_t height, uint32_t pitch)
{
    uint32_t lw, lh;
    if (!fb_base || width == 0 || height == 0) return;
    lv_init();
    lvgl_port_fb = (uint32_t *)(uintptr_t)fb_base;
    /* Super Scaling auto-detects the native resolution from
     * the GOP framebuffer size passed in here — no driver.
     * The LVGL display is created at the logical size
     * (1280x720 while scaling is active), and the flush
     * scales it to native. */
    superscaling_init(width, height);
    lw = superscaling_logical_w();
    lh = superscaling_logical_h();
    lvgl_port_w = lw;
    lvgl_port_h = lh;
    lvgl_port_pitch = pitch ? pitch : width;
    /* When Super Scaling letterboxes, the bars are outside
     * every flush rect, so nothing would ever paint them:
     * clear the whole native framebuffer black once here,
     * so the bars are black instead of stale GOP contents. */
    if (superscaling_active())
        gpu2d_fill_rect(lvgl_port_fb, lvgl_port_pitch, 0, 0, width, height, 0);
    lvgl_port_disp = lv_display_create((int32_t)lw, (int32_t)lh);
    if (!lvgl_port_disp) return;
    lv_display_set_buffers(lvgl_port_disp, lvgl_port_buf, 0,
                            sizeof(lvgl_port_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(lvgl_port_disp, lvgl_port_flush);
    lvgl_port_indev = lv_indev_create();
    if (lvgl_port_indev) {
        lv_indev_set_type(lvgl_port_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(lvgl_port_indev, lvgl_port_indev_read);
    }
    lvgl_port_active = 1;
    lvgl_port_suspended = 0;
    lvgl_port_px = (int)lw / 2;
    lvgl_port_py = (int)lh / 2;
    lvgl_port_pressed = 0;
}

int lvgl_is_active(void) { return lvgl_port_active && !lvgl_port_suspended; }

void lvgl_port_suspend(void) { lvgl_port_suspended = 1; }

int lvgl_port_is_suspended(void) { return lvgl_port_active && lvgl_port_suspended; }

void lvgl_port_resume(void)
{
    if (!lvgl_port_active) return;
    lvgl_port_suspended = 0;
    /* Force a full redraw of the Start screen over the old app. */
    if (lvgl_port_disp) lv_obj_invalidate(lv_screen_active());
    lv_timer_handler();
}

void lvgl_port_tick(void)
{
    static unsigned int n = 0;
    if (!lvgl_is_active()) return;
    /* No ms timer in the GUI poll loop: advance LVGL time in
     * fixed 5 ms steps, and run timers every 40th call. This
     * makes animations run; their wall-clock speed depends on
     * loop speed, which is a known limitation, not a bug. */
    n++;
    if ((n % 40u) == 0) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
}

void lvgl_port_feed_pointer(int x, int y, int pressed)
{
    /* gui.c feeds native framebuffer coords; store logical
     * coords, so the indev read needs no conversion. */
    superscaling_native_to_logical(x, y, &lvgl_port_px, &lvgl_port_py);
    lvgl_port_pressed = pressed ? 1 : 0;
}
