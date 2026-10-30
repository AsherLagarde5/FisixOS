#ifndef PS2KBD_H
#define PS2KBD_H
/* PS/2 keyboard driver, step 1: controller test + port enable.
   Latitude 5420's internal keyboard is on the embedded controller /
   PS/2 path, and external PS/2 keyboards use the same ports (0x60 data,
   0x64 command/status). All waits are bounded: if the controller does
   not answer, this reports not-present instead of hanging the boot. */
int ps2kbd_init(void);
int ps2kbd_present(void);
#endif
