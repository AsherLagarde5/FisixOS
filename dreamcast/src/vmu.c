/* VMU LCD draw, KOS version in real build, host stub here.
   Real KOS build: replace this file with KOS maple VMU
   code (vmu_draw_lcd with a 48x32 bitmap, 192 bytes
   XRAM, 48*32/8 = 192 bytes, exactly the VMU XRAM size).
   Host build: print 48x32 as text, 1 char per 4x4 block,
   to prove logic without a Dreamcast. */
#include <stdio.h>
#include <string.h>
#include "vmu.h"
void vmu_lcd_draw(const char *line1, const char *line2, const char *line3, int tick) {
#ifdef __DREAMCAST__
  /* KOS: build a 48x32 bitmap here and call vmu_draw_lcd().
     Not compiled in host test, KOS headers are not here. */
  (void)line1; (void)line2; (void)line3; (void)tick;
#else
  printf("[VMU 48x32] %s | %s | %s | tick=%d\n", line1, line2, line3, tick);
#endif
}
