/* FisixDreamcast 2D GUI — 2D ONLY, NOT 3D.
   Icons on a 640x480 PowerVR 2D screen, flat quads,
   no 3D models, no camera, no 3D menus.
   Mouse: controller analog stick moves cursor,
   D-pad moves cursor in steps, A = click, B = back,
   Dreamcast mouse device moves cursor if attached.
   Text menu stays as fallback; GUI has a TEXT MENU icon
   to return to text mode. */
#ifndef FISIXDC_GUI_H
#define FISIXDC_GUI_H
typedef enum { GUI_ICON_TERMINAL, GUI_ICON_PONG, GUI_ICON_JPEG, GUI_ICON_VIDEO, GUI_ICON_ACCESSORIES, GUI_ICON_ABOUT, GUI_ICON_TEXTMENU, GUI_ICON_SHUTDOWN, GUI_ICON_COUNT } gui_icon_t;
extern const char *gui_icon_names[GUI_ICON_COUNT];
void gui_draw(int selected, int mouse_x, int mouse_y);
int gui_hit_test(int mouse_x, int mouse_y);
#endif
