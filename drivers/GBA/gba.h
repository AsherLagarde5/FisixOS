#ifndef FISIXGBA_H
#define FISIXGBA_H
/* FisixGBA hardware registers. Original code, public GBA register map.
   No Nintendo BIOS calls, no Nintendo logo bitmap bundled.
   Target: 32-bit ARM7TDMI, standard 32 MB ROM cart maximum. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;

#define REG_DISPCNT (*(vu16*)0x04000000)
#define REG_VCOUNT  (*(vu16*)0x04000006)
#define REG_KEYINPUT (*(vu16*)0x04000130)
#define REG_SIOCNT  (*(vu16*)0x04000128)
#define REG_SIODATA8 (*(vu16*)0x0400012A)
#define REG_TM0CNT  (*(vu16*)0x04000102)
#define REG_TM0DATA (*(vu16*)0x04000100)
#define REG_TM1CNT  (*(vu16*)0x04000106)
#define REG_TM1DATA (*(vu16*)0x04000104)

#define VRAM ((vu16*)0x06000000)
#define SRAM ((vu8*)0x0E000000)
#define SCREEN_W 240
#define SCREEN_H 160

#define KEY_A      (1<<0)
#define KEY_B      (1<<1)
#define KEY_SELECT (1<<2)
#define KEY_START  (1<<3)
#define KEY_RIGHT  (1<<4)
#define KEY_LEFT   (1<<5)
#define KEY_UP     (1<<6)
#define KEY_DOWN   (1<<7)
#define KEY_R      (1<<8)
#define KEY_L      (1<<9)

#define RGB15(r,g,b) ((r)|((g)<<5)|((b)<<10))
#define SIO_START  (1<<7)
#define SIO_MASTER (1<<0)

void video_init(void);
void video_clear(u16 color);
void video_pixel(int x, int y, u16 color);
void video_text(int x, int y, const char *s, u16 color);
void video_rect(int x, int y, int w, int h, u16 color);
void video_wait_vblank(void);

u16 input_read(void);
u16 input_pressed(u16 old_keys, u16 new_keys);
int keyboard_input(char *out, int maxlen, const char *prompt);

void timer_init(void);
u32 timer_ticks(void); /* ~16384 Hz, TM0 prescale 1024 + TM1 cascade */

/* Cart: standard 32 MB ROM window, SRAM save on the cart */
const u8 *cart_rom_ptr(u32 offset);
int cart_fs_init(void);
int fs_file_exists(const char *name);
int fs_delete(const char *name);
int fs_write(const char *name, const u8 *data, u32 len);
int fs_read(const char *name, u8 *out, u32 maxlen);
int fs_list(char names[][32], int max);
void fs_flush_to_sram(void);

/* Kernel: port of PC kernel/kernel.c, see src/kernel.c */
void kernel_startup_gba(void);

/* Audio + headphone, GBA only (src/audio.c, src/headphone.c) */
void audio_init(void);
void audio_beep(u32 freq_hz, u32 ms);
void headphone_init(void);
void headphone_beep(u32 freq_hz, u32 ms);

/* GEP loader, src/gep.c + src/gep.h. Names are in gep.h. */
int gep_run_data(const unsigned char *data, unsigned int len);
int gep_run_file(const char *name);
int gep_run_embedded(const char *name);
int gep_list_embedded(char names[][32], int max);

/* Patch path: named pipes, src/pipe.c. */
int pipe_write(const char *channel, const u8 *data, u32 len);
int pipe_read(const char *channel, u8 *out, u32 maxlen);
int pipe_clear(const char *channel);

/* Patch script interpreter, src/patch.c. */
int patch_run_data(const unsigned char *data, unsigned int len);
int patch_run_file(const char *name);
int patch_list_embedded(char names[][32], int max);

/* Terminal command parser, src/apps.c. Shared by interactive
   Terminal and Patch scripts, so they are exactly the same. */
int terminal_exec(const char *cmd, int in_patch);
void terminal_patch_begin(void);

/* MS-DOS .com emulator, src/dosemu.c. Returns DOS_EMU_*
   codes: 0 OK, 1 EXIT, 2 UNSUPPORTED, 3 TRUNCATED. */
int dosemu_run(const u8 *com, unsigned int com_len, char *out, unsigned int out_size, int *exit_code);
const char *dosemu_last_output(void);
int dosemu_run_file(const char *name);
int dosemu_list_embedded(char names[][32], int max);

/* User Update Files, src/update.c + src/update.h. */
int update_check(const unsigned char *data, unsigned int len);
int update_apply(const unsigned char *data, unsigned int len, int *applied, int *skipped, int *deleted);
int update_run_file(const char *name);
int update_run_file_counts(const char *name, int *applied, int *skipped, int *deleted, unsigned int *platform_out);
int update_list_embedded(char names[][32], int max);
const char *update_platform_name(unsigned int platform);

/* Link */
void link_set_master(int is_master);
u16 link_crc16(const u8 *data, u32 len);
u16 link_crc_update(u16 crc, u8 byte);
int link_send_packet(u8 type, const u8 *payload, u16 len);
int link_recv_packet(u8 *type, u8 *payload, u16 *len);

#endif
