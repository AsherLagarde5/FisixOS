#include "gba.h"
u16 input_read(void) { return (u16)(~REG_KEYINPUT) & 0x03FF; }
u16 input_pressed(u16 old_keys, u16 new_keys) {
  return (u16)(new_keys & ~old_keys);
}

/* On-screen keyboard: D-pad moves, A types, B deletes,
   START finishes. Rows are 13 chars wide. */
static const char *kbd_rows[3] = {
  "ABCDEFGHIJKLM",
  "NOPQRSTUVWXYZ",
  "0123456789 .-",
};

/* v0.6 dirty keyboard: full draw once, then only the cell
   that lost/gained the highlight, and the output line when
   it changes. No full clear on a D-pad move. */
static void kbd_draw_cell(int cx, int cy, int highlight) {
  char ch[2] = {kbd_rows[cy][cx], 0};
  int px = 16 + cx * 16, py = 64 + cy * 20;
  video_rect(px - 2, py - 2, 14, 14,
             highlight ? RGB15(31, 31, 0) : RGB15(0, 0, 10));
  video_text(px, py, ch, RGB15(25, 25, 25));
}
int keyboard_input(char *out, int maxlen, const char *prompt) {
  int len = 0, cx = 0, cy = 0, i;
  u16 old = 0, keys;
  out[0] = 0;
  video_clear(RGB15(0, 0, 10));
  video_text(8, 8, prompt, RGB15(31, 31, 31));
  video_text(8, 40, "D-PAD MOVE  A TYPE  B DEL  START DONE",
             RGB15(20, 20, 20));
  for (i = 0; i < 3; i++) {
    int j;
    for (j = 0; j < 13; j++) kbd_draw_cell(j, i, j == cx && i == cy);
  }
  /* v0.7 FREEZE FIX: the old loop was the freeze.
     It waited for VBlank twice per poll, and broke out of
     the inner wait after one VBlank regardless of input,
     so short touch taps were missed. Worse, after every
     key it did while(input_read()){} — wait until ALL
     buttons are released. On a touch screen, with rapid
     taps, input never goes to zero (next tap starts
     before the previous release is seen), so the code
     sat in debounce forever, the screen never updated,
     and tapping more to unfreeze it made it worse.
     New loop: one VBlank wait, then poll, then edge
     detect. Held keys do not repeat because old is
     updated every frame, so no release-wait is needed
     at all. A tap lasting one frame is enough. */
  old = input_read(); /* snapshot at entry: held keys do not trigger */
  while (1) {
    u16 p;
    int ocx, ocy, olen;
    video_wait_vblank();
    keys = input_read();
    p = input_pressed(old, keys);
    old = keys;
    if (!p) continue;
    ocx = cx; ocy = cy; olen = len;
    if (p & KEY_UP) cy = (cy + 2) % 3;
    else if (p & KEY_DOWN) cy = (cy + 1) % 3;
    else if (p & KEY_LEFT) cx = (cx + 12) % 13;
    else if (p & KEY_RIGHT) cx = (cx + 1) % 13;
    else if (p & KEY_A) {
      char c = kbd_rows[cy][cx];
      if (len < maxlen - 1) {
        out[len++] = c; out[len] = 0;
      }
    }
    else if (p & KEY_B) {
      if (len > 0) { len--; out[len] = 0; }
    }
    else if (p & KEY_START) {
      return len;
    }
    else if (p & KEY_SELECT) {
      return -1; /* cancel, no command sent */
    }
    /* Dirty only: old cell un-highlighted, new cell
       highlighted, output line only if it changed.
       This draw is small and happens right after the
       VBlank wait above, in the same frame. */
    if (cx != ocx || cy != ocy) {
      kbd_draw_cell(ocx, ocy, 0);
      kbd_draw_cell(cx, cy, 1);
    }
    if (len != olen) {
      video_rect(8, 24, 224, 8, RGB15(0, 0, 10));
      video_text(8, 24, out, RGB15(31, 31, 0));
    }
  }
}
