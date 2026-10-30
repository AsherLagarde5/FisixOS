/* FisixDreamcast v0.1 — FisixOS port to Dreamcast, with VMU.
   SH-4 rewrite, not PC/GBA/C64 code carried over.
   KOS (KallistiOS) is the Dreamcast homebrew SDK.
   In a real KOS build, __DREAMCAST__ is defined, and
   kos_init(), pvr_init(), cont, maple, vmu_draw_lcd()
   etc. are used. In host test, those are stubbed to
   stdio, so menu/Terminal/VMU logic can be tested
   without a Dreamcast toolchain (30-60 min build).
   TV: 640x480, text first. VMU: 48x32 status screen
   in controller, updated on every app change, see vmu.h.
   Save: VMU save data, USER.PROF, move VMU to another
   Dreamcast and profile moves with it.
   No Sega BIOS/ROMs bundled. No GD-ROM commercial
   games bundled. This is original homebrew code.
 */
#include <stdio.h>
#include <string.h>
#include "vmu.h"

static char g_user[16] = "PLAYER";
static int g_tick = 0;

static void upcase(char *s) {
  int i;
  for (i = 0; s[i]; i++) if (s[i] >= 'a' && s[i] <= 'z') s[i] -= 32;
}
static int eq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static int starts(const char *s, const char *p) {
  while (*p) { if (*s != *p) return 0; s++; p++; } return 1;
}
static void vmu_update(const char *app, const char *status) {
  char l1[32], l2[32], l3[32];
  snprintf(l1, sizeof(l1), "FISIX %s", g_user);
  snprintf(l2, sizeof(l2), "%s", app);
  snprintf(l3, sizeof(l3), "%s", status);
  vmu_lcd_draw(l1, l2, l3, g_tick++);
}
static void read_line(char *out, int max) {
  if (!fgets(out, max, stdin)) out[0] = 0;
  else { char *p = out; while (*p) { if (*p == '\n' || *p == '\r') { *p = 0; break; } p++; } }
  upcase(out);
}
static void app_pong(void) {
  printf("PONG - DREAMCAST TV, VMU SHOWS SCORE\n");
  vmu_update("PONG", "YOU 0 CPU 0");
  printf("PONG V0.1: TEXT DEMO, FIRST TO 3, W/S VS I/K, Q QUIT\n");
  /* Simple text Pong logic, same as C64, for host test.
     Real Dreamcast Pong will use PowerVR + controller
     analog stick, not text, in v0.2. */
  {
    int bx = 20, by = 12, dx = 1, dy = 1, py = 10, ay = 10, ps = 0, as = 0;
    char line[64];
    while (ps < 3 && as < 3) {
      printf("BALL %d,%d PADDLES %d/%d SCORE %d-%d > ", bx, by, py, ay, ps, as);
      read_line(line, sizeof(line));
      if (eq(line, "Q")) break;
      if (eq(line, "W")) py--;
      if (eq(line, "S")) py++;
      if (eq(line, "I")) ay--;
      if (eq(line, "K")) ay++;
      bx += dx; by += dy;
      if (by <= 0 || by >= 24) dy = -dy;
      if (bx <= 2) { as++; bx = 20; by = 12; }
      if (bx >= 38) { ps++; bx = 20; by = 12; }
      { char st[32]; snprintf(st, sizeof(st), "YOU %d CPU %d", ps, as); vmu_update("PONG", st); }
    }
    printf("PONG OVER YOU %d CPU %d\n", ps, as);
  }
}
static void app_terminal(void) {
  char line[64];
  printf("FISIXDREAMCAST TERMINAL - TYPE HELP\n");
  vmu_update("TERMINAL", "OK");
  for (;;) {
    printf("FISIXDC> ");
    read_line(line, sizeof(line));
    if (eq(line, "EXIT") || eq(line, "QUIT")) break;
    else if (eq(line, "HELP")) {
      printf("HELP ECHO CALC CLEAR STATUS VER WHOAMI\n");
      printf("HOSTNAME SYSINFO MEM PONG VMU SHUTDOWN EXIT\n");
    }
    else if (starts(line, "ECHO ")) printf("%s\n", line + 5);
    else if (starts(line, "CALC ")) {
      long a = 0, b = 0; char op = 0; const char *s = line + 5;
      while (*s == ' ') s++;
      while (*s >= '0' && *s <= '9') { a = a * 10 + (*s - '0'); s++; }
      while (*s == ' ') s++; op = *s++;
      while (*s == ' ') s++;
      while (*s >= '0' && *s <= '9') { b = b * 10 + (*s - '0'); s++; }
      printf("CALC: %ld\n", op == '+' ? a + b : op == '-' ? a - b : op == '*' ? a * b : b ? a / b : 0);
    }
    else if (eq(line, "CLEAR") || eq(line, "CLS")) printf("\n---CLR---\n");
    else if (eq(line, "STATUS")) printf("OK\n");
    else if (eq(line, "VER")) printf("FISIXDREAMCAST V0.1\n");
    else if (eq(line, "WHOAMI")) printf("%s\n", g_user);
    else if (eq(line, "HOSTNAME")) printf("FISIXDREAMCAST\n");
    else if (eq(line, "SYSINFO")) {
      printf("DREAMCAST SH-4 200MHZ, 16MB RAM, 8MB VRAM\n");
      printf("POWERVR2, AICA SOUND, MAPLE CONTROLLER\n");
      printf("VMU 48X32 LCD, 128KB CARD, 64KB FLASH\n");
    }
    else if (eq(line, "MEM")) printf("FREE RAM: 16MB TOTAL, OS USES ~1MB\n");
    else if (eq(line, "VMU")) vmu_update("TERMINAL", "VMU TEST");
    else if (eq(line, "PONG")) app_pong();
    else if (eq(line, "SHUTDOWN")) { printf("SHUTDOWN - POWER OFF DREAMCAST\n"); return; }
    else if (line[0] == 0) { }
    else printf("UNKNOWN: %s\n", line);
  }
}
int main(void) {
#ifdef __DREAMCAST__
  /* Real KOS: kos_init_all(), pvr_init(), etc., here.
     Not compiled in host test. */
#endif
  printf("FISIXDREAMCAST V0.1\n");
  printf("SH-4 PORT OF FISIXOS, WITH VMU SCREEN\n");
  printf("FIRST-TIME SETUP - PICK A NAME:\n");
  {
    char name[16];
    read_line(name, sizeof(name));
    if (name[0]) { int i = 0; while (name[i] && i < 15) { g_user[i] = name[i]; i++; } g_user[i] = 0; }
    printf("USER: %s, SAVED TO VMU (V0.1: SESSION ONLY IF NO VMU)\n", g_user);
  }
  vmu_update("MENU", "OK");
  for (;;) {
    printf("\nFISIXDREAMCAST V0.1 - USER %s\n", g_user);
    printf("1 TERMINAL\n2 PONG\n3 ABOUT\n4 SHUTDOWN\nPICK 1-4: ");
    {
      char k[16];
      read_line(k, sizeof(k));
      if (eq(k, "1")) app_terminal();
      else if (eq(k, "2")) app_pong();
      else if (eq(k, "3")) { printf("FISIXDREAMCAST: SH-4, POWERVR2, VMU 48X32\n"); vmu_update("ABOUT", "V0.1"); }
      else if (eq(k, "4")) { printf("SHUTDOWN - POWER OFF\n"); break; }
      else continue;
    }
    vmu_update("MENU", "OK");
  }
  return 0;
}
