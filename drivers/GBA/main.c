/* FisixGBA main: 32-bit ARM7TDMI port.
   Boot -> first-time setup (if no USER.PROF on cart SRAM)
   -> menu. No EFI, no x86 code, no BIOS calls. */
#include "gba.h"

/* v0.4 menu, de-bloated at Derek's request: no Files/Pathify,
   no Z (GBA never had Z), no 34-icon desktop. Just Terminal,
   Pong, Link, the mouse GUI, and About. */
static const char *menu_items[] = {
  "TERMINAL",
  "PONG",
  "LINK TRANSFER",
  "GUI - MOUSE MODE",
  "POWERSHELL-STYLE",
  "BASH-STYLE",
  "ABOUT",
};
#define MENU_COUNT 7

extern void app_terminal(void);
extern void app_pong(void);
extern void app_link(void);
extern void app_gui(void);
extern void app_powershell(void);
extern void app_bash(void);

static void get_username(char *out, int maxlen) {
  u8 buf[64];
  int n = fs_read("USER.PROF", buf, sizeof(buf) - 1), i;
  out[0] = 0;
  if (n <= 0) return;
  buf[n] = 0;
  /* USER.PROF format: user=NAME\nsetup=1\n */
  if (buf[0] == 'u' && buf[1] == 's' && buf[2] == 'e' && buf[3] == 'r' && buf[4] == '=') {
    for (i = 0; i < maxlen - 1 && buf[5 + i] && buf[5 + i] != '\n'; i++)
      out[i] = (char)buf[5 + i];
    out[i] = 0;
  }
}

/* v0.6 dirty menu: draw_menu_full() once, then draw_menu_item()
   only for the two lines that change on Up/Down. No full clear
   on a selection change, and no draw at all on an idle frame. */
static void draw_menu_item(int i, int selected) {
  char item[32];
  int j = 0, len = 0;
  const char *p;
  if (i == selected) { item[j++] = '>'; item[j++] = ' '; }
  else { item[j++] = ' '; item[j++] = ' '; }
  p = menu_items[i];
  while (*p) { item[j++] = *p++; len++; }
  item[j] = 0;
  video_rect(16, 56 + i * 16, (2 + len) * 8, 8, RGB15(0, 0, 10));
  video_text(16, 56 + i * 16, item,
             selected ? RGB15(31, 31, 0) : RGB15(25, 25, 25));
}
static void draw_menu(int sel, const char *user) {
  int i;
  char line[40];
  video_clear(RGB15(0, 0, 10));
  video_text(8, 8, "FISIXGBA", RGB15(31, 31, 31));
  video_text(8, 20, "FISIXOS FOR GBA - 32-BIT ARM", RGB15(20, 20, 31));
  line[0] = 0;
  {
    int j = 0;
    const char *p = "USER: ";
    while (*p) line[j++] = *p++;
    p = user;
    while (*p && j < 38) line[j++] = *p++;
    line[j] = 0;
  }
  video_text(8, 32, line, RGB15(20, 31, 20));
  for (i = 0; i < MENU_COUNT; i++) draw_menu_item(i, i == sel);
  video_text(8, 144, "SAVE IS ON THE CART. SWAP GBAS.", RGB15(10, 20, 10));
}

int main(void) {
  u16 old_keys = 0, keys;
  int sel = 0;
  char user[16];

  video_init();
  timer_init();
  kernel_startup_gba();
  cart_fs_init();

  if (!fs_file_exists("USER.PROF")) {
    char name[16];
    video_clear(RGB15(0, 0, 10));
    video_text(8, 8, "FIRST-TIME SETUP", RGB15(31, 31, 31));
    video_text(8, 24, "NO PROFILE ON THIS CART.", RGB15(25, 25, 25));
    video_text(8, 40, "PRESS A TO PICK A NAME.", RGB15(31, 31, 0));
    /* v0.7: vblank edge poll for A, no release wait. The old
       tight loop + while(input_read()){} was the setup freeze
       path: on touch, a tap that is still held, or a rapid
       next tap, kept input non-zero and the release wait
       never exited. */
    old_keys = input_read();
    while (1) {
      video_wait_vblank();
      keys = input_read();
      if (input_pressed(old_keys, keys) & KEY_A) break;
      old_keys = keys;
    }
    keyboard_input(name, sizeof(name), "PICK A NAME:");
    if (name[0] == 0) { name[0] = 'P'; name[1] = 'L'; name[2] = 'A'; name[3] = 'Y'; name[4] = 'E'; name[5] = 'R'; name[6] = 0; }
    {
      u8 prof[48];
      int j = 0, k = 0;
      const char *p = "user=";
      while (*p) prof[j++] = (u8)*p++;
      while (name[k]) prof[j++] = (u8)name[k++];
      prof[j++] = '\n';
      p = "setup=1\n";
      while (*p) prof[j++] = (u8)*p++;
      fs_write("USER.PROF", prof, (u32)j);
    }
  }

  get_username(user, sizeof(user));
  if (!user[0]) { user[0] = 'P'; user[1] = 'L'; user[2] = 'A'; user[3] = 'Y'; user[4] = 'E'; user[5] = 'R'; user[6] = 0; }

  while (1) {
    draw_menu(sel, user);
    while (1) {
      int osel;
      video_wait_vblank();
      keys = input_read();
      {
        u16 pressed = input_pressed(old_keys, keys);
        if (pressed & KEY_A) goto open_app;
        if (pressed & KEY_START) goto open_app;
        osel = sel;
        if (pressed & KEY_UP) sel = (sel + MENU_COUNT - 1) % MENU_COUNT;
        if (pressed & KEY_DOWN) sel = (sel + 1) % MENU_COUNT;
        if (sel != osel) {
          /* Dirty only: old line back to white, new line yellow. */
          draw_menu_item(osel, 0);
          draw_menu_item(sel, 1);
        }
      }
      old_keys = keys;
    }
  open_app:
    old_keys = input_read(); /* snapshot: held menu key does not retrigger in app */
    if (sel == 0) app_terminal();
    else if (sel == 1) app_pong();
    else if (sel == 2) app_link();
    else if (sel == 3) app_gui();
    else if (sel == 4) app_powershell();
    else if (sel == 5) app_bash();
    else {
      video_clear(RGB15(0, 0, 10));
      video_text(8, 8, "FISIXGBA V1.3", RGB15(31, 31, 31));
      video_text(8, 24, "32 MB CART, 32-BIT ARM7TDMI.", RGB15(25, 25, 25));
      video_text(8, 40, "OS AND SAVE LIVE ON THE CART.", RGB15(25, 25, 25));
      video_text(8, 56, "KERNEL: PORTED FROM KERNEL.C", RGB15(25, 25, 25));
      video_text(8, 72, "TERMINAL IS CALLED RETURN.", RGB15(25, 25, 25));
      video_text(8, 88, "ONLY HERE, IN ABOUT, NOT MENU.", RGB15(25, 25, 25));
      video_text(8, 112, "PRESS B TO GO BACK.", RGB15(31, 31, 0));
      old_keys = input_read();
      while (1) {
        video_wait_vblank();
        keys = input_read();
        if (input_pressed(old_keys, keys) & KEY_B) break;
        old_keys = keys;
      }
    }
    old_keys = input_read();
  }
  return 0;
}
