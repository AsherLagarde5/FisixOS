#ifndef TOUCHSCREEN_H
#define TOUCHSCREEN_H
void touchscreen_init(void);
int touchscreen_is_hid_over_i2c_expected(void);
void touchscreen_update_absolute(int x, int y, int down);
int touchscreen_last_x(void);
int touchscreen_last_y(void);
int touchscreen_is_down(void);
#endif
