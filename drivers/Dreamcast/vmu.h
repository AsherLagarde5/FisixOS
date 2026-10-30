/* FisixDreamcast VMU LCD abstraction, 48x32 monochrome.
   Docked VMU: Dreamcast SH-4 talks to VMU over Maple bus.
   In KOS, this is vmu_draw_lcd() / maple VMU device.
   In host test, this prints the 48x32 screen as ASCII art,
   so VMU logic can be tested without a Dreamcast.
   VMU screen layout, v0.1:
   - Row 0-7:   FISIX + user (text, 48 pixels wide, ~8 chars)
   - Row 8-15:  current app
   - Row 16-23: status / score / file count
   - Row 24-31: icon / tick, 8x8, alternating on timer
   This is NOT a duplicate of the TV screen, it is a
   second, tiny status screen in the controller. */
#ifndef FISIXDC_VMU_H
#define FISIXDC_VMU_H
void vmu_lcd_draw(const char *line1, const char *line2, const char *line3, int tick);
#endif
