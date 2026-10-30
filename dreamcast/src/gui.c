#include <stdio.h>
#include "gui.h"
const char *gui_icon_names[GUI_ICON_COUNT] = {
  "TERMINAL", "PONG", "JPEG", "VIDEO", "ACCESSORIES", "ABOUT", "TEXT MENU", "SHUTDOWN"
};
/* Icon grid, 640x480, 4 columns x 2 rows, 160x120 cells.
   2D only: flat rectangles, no 3D. */
void gui_draw(int selected, int mouse_x, int mouse_y) {
#ifdef __DREAMCAST__
  /* Real KOS: pvr_init, pvr_poly_cxt_col, flat quads for
     each icon cell, text via KOS font, cursor as a quad.
     NOT compiled in host test. */
  (void)selected; (void)mouse_x; (void)mouse_y;
#else
  int i;
  printf("[GUI 2D 640x480] mouse %d,%d selected %s\n", mouse_x, mouse_y, gui_icon_names[selected]);
  for (i = 0; i < GUI_ICON_COUNT; i++)
    printf("  %s %s at %d,%d\n", i == selected ? ">" : " ", gui_icon_names[i], (i % 4) * 160, (i / 4) * 120);
#endif
}
int gui_hit_test(int mouse_x, int mouse_y) {
  int col = mouse_x / 160; int row = mouse_y / 120;
  int idx = row * 4 + col;
  if (col < 0 || col > 3 || row < 0 || row > 1) return -1;
  if (idx >= GUI_ICON_COUNT) return -1;
  return idx;
}
