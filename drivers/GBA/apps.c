/* FisixGBA apps v0.4: Terminal (called Return only in About),
   Pong, FisixLink, GUI mouse mode. Files/Pathify removed,
   Z never existed on GBA. Original code, except the kernel
   port in src/kernel.c which follows PC kernel.c. */
#include "gba.h"

/* v0.7: NO while(input_read()){} release wait here or anywhere.
   That was the freeze: on touch, rapid taps never leave input
   at zero, so a release wait never exits. Edge detect only:
   snapshot at entry, poll once per VBlank, break on a NEW B. */
static void wait_b(void) {
  u16 old = input_read(), k;
  while (1) {
    video_wait_vblank();
    k = input_read();
    if (input_pressed(old, k) & KEY_B) break;
    old = k;
  }
}

static int term_eq(const char *a, const char *b) {
  int i;
  for (i = 0; a[i] || b[i]; i++) {
    char ca = a[i], cb = b[i];
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return 0;
  }
  return 1;
}
static int term_starts(const char *a, const char *b) {
  int i;
  for (i = 0; b[i]; i++) {
    char ca = a[i], cb = b[i];
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return 0;
  }
  return 1;
}
static void term_num(int v, char *out) {
  char tmp[16];
  int i = 0, j = 0, neg = 0;
  if (v < 0) { neg = 1; v = -v; }
  if (v == 0) tmp[i++] = '0';
  while (v > 0) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
  if (neg) out[j++] = '-';
  while (i > 0) out[j++] = tmp[--i];
  out[j] = 0;
}


void app_gui(void);
void app_bash(void);
void app_powershell(void);

/* EFI PC terminal commands, copied to GBA, v1.2.
   Source: PC FisixOS EFI loader terminal in
   boot/efi_main.c terminal_execute_command(), plus
   ReTerm (apps/reterm.c) aliases that EFI does not have
   (erase, rename, md, chdir, quit, size, hostname).
   Every command name from EFI is present here. Where
   GBA cannot do what EFI does, the command exists and
   says so honestly, instead of UNKNOWN COMMAND:
   - No directories on a flat cart FS: md/rd are stubs
     saying so, cd is in-memory only (like ReTerm).
   - No RTC: date/time show timer ticks, not a real date.
   - One profile only: users/useradd/passwd/login are
     stubs around the single USER.PROF profile.
   - No power off / reboot from software: shutdown/exit
     returns to the menu, reboot says so.
   - install: GBA is on a cart, no installer.
   - gui/bash: gui opens the GBA GUI app, bash opens
     the bash-style app, both return to Terminal.
   - nano: a real tiny editor, saves to NANO.TXT (or
     the file named), unlike EFI's stub which only
     said "saved".
   - copy/del/ren/type/cat/touch/rm/write/append/source
     are real on cart files, 512 bytes max each.
   - .fse direct run in EFI becomes .gep/.com/.patch/
     .update direct run here (typing the filename runs it).
 */
static char term_env_names[8][32];
static char term_env_vals[8][64];
static int term_nenv = 0;
static char term_cwd[64] = "/";
static char term_path[64] = "SYSTEM/APPS;SYSTEM/DRIVERS";
static int term_mode_bash = 0;
static int term_echo_enabled = 1;
static void term_env_set(const char *name, const char *val) {
  int i, slot = -1;
  for (i = 0; i < term_nenv; i++) if (term_eq(term_env_names[i], name)) { slot = i; break; }
  if (slot < 0 && term_nenv < 8) slot = term_nenv++;
  if (slot < 0) return;
  {
    int j = 0;
    while (name[j] && j < 31) { term_env_names[slot][j] = name[j]; j++; }
    term_env_names[slot][j] = 0;
    j = 0;
    while (val[j] && j < 63) { term_env_vals[slot][j] = val[j]; j++; }
    term_env_vals[slot][j] = 0;
  }
}
static int term_env_find(const char *name) {
  int i;
  for (i = 0; i < term_nenv; i++) if (term_eq(term_env_names[i], name)) return i;
  return -1;
}
static const char *term_current_user(void) {
  static char user[16];
  u8 buf[64];
  int n = fs_read("USER.PROF", buf, sizeof(buf) - 1), i;
  user[0] = 0;
  if (n > 0) {
    buf[n] = 0;
    if (buf[0] == 'u' && buf[1] == 's' && buf[2] == 'e' && buf[3] == 'r' && buf[4] == '=') {
      for (i = 0; i < 15 && buf[5 + i] && buf[5 + i] != '\n'; i++) user[i] = (char)buf[5 + i];
      user[i] = 0;
    }
  }
  if (!user[0]) { user[0] = 'P'; user[1] = 'L'; user[2] = 'A'; user[3] = 'Y'; user[4] = 'E'; user[5] = 'R'; user[6] = 0; }
  return user;
}
static int term_contains(const char *text, const char *needle) {
  int i, j;
  if (!*needle) return 1;
  for (i = 0; text[i]; i++) {
    for (j = 0; text[i + j] && needle[j] && text[i + j] == needle[j]; j++) {}
    if (!needle[j]) return 1;
  }
  return 0;
}
static void term_split2(const char *rest, char *a, int amax, char *b, int bmax) {
  int i = 0, j = 0;
  while (*rest == ' ') rest++;
  while (rest[i] && rest[i] != ' ' && j < amax - 1) { a[j++] = rest[i++]; }
  a[j] = 0;
  while (rest[i] == ' ') i++;
  j = 0;
  while (rest[i] && j < bmax - 1) { b[j++] = rest[i++]; }
  b[j] = 0;
}
static void app_nano(const char *filename) {
  char buf[512];
  int len = 0;
  char line[64];
  char fname[32];
  int j = 0;
  while (filename[j] && j < 31) { fname[j] = filename[j]; j++; }
  fname[j] = 0;
  if (!fname[0]) { fname[0] = 'N'; fname[1] = 'A'; fname[2] = 'N'; fname[3] = 'O'; fname[4] = '.'; fname[5] = 'T'; fname[6] = 'X'; fname[7] = 'T'; fname[8] = 0; }
  buf[0] = 0;
  while (1) {
    video_clear(RGB15(0, 0, 10));
    video_text(8, 8, "NANO - TYPE LINES", RGB15(31, 31, 31));
    video_text(8, 24, "SAVE OR EXIT TO LEAVE.", RGB15(25, 25, 25));
    video_text(8, 40, fname, RGB15(31, 31, 0));
    if (keyboard_input(line, sizeof(line), "nano>") < 0) continue;
    if (term_eq(line, "save")) {
      fs_write(fname, (u8*)buf, (u32)len);
      video_clear(RGB15(0, 0, 10));
      video_text(8, 8, "SAVED NANO BUFFER.", RGB15(31, 31, 31));
      video_text(8, 24, fname, RGB15(31, 31, 0));
      return;
    }
    if (term_eq(line, "exit") || term_eq(line, "quit")) {
      video_clear(RGB15(0, 0, 10));
      video_text(8, 8, "EXITING NANO.", RGB15(31, 31, 31));
      return;
    }
    if (len + 64 < 512) {
      int k = 0;
      while (line[k] && len < 510) { buf[len++] = line[k++]; }
      buf[len++] = '\n';
      buf[len] = 0;
    }
  }
}

/* Patch = exactly Terminal commands, v1.0.
   terminal_exec() is THE command parser. Interactive
   Terminal calls it after keyboard_input, Patch scripts
   call it for each line. Same function, same code, so a
   patch line and a typed line do exactly the same thing.
   Returns 1 for EXIT, 0 otherwise. In a patch, EXIT stops
   the script and returns to Terminal; interactively, EXIT
   exits Terminal to the menu. That is the only difference,
   and it is in the caller, not in the parser. */
static int patch_print_cy = 8;
static int patch_depth = 0;
void terminal_patch_begin(void) {
  patch_print_cy = 8;
  video_clear(RGB15(0, 0, 10));
}
static int term_ext_is(const char *name, const char *ext) {
  int l = 0;
  while (name[l]) l++;
  if (l < (int)0) return 0;
  {
    int el = 0;
    while (ext[el]) el++;
    if (l < el) return 0;
    return term_eq(name + l - el, ext);
  }
}
int terminal_exec(const char *cmd, int in_patch) {
  /* in_patch: 1 when called from a .patch script, 0 interactive.
     PRINT in a patch uses patch_print_cy so lines accumulate;
     interactively it prints at y=8, then the caller shows
     PRESS B. Other commands print at fixed y, then patch
     cursor is reset to 48 so the next PRINT follows them. */
  if (!cmd) return 0;
  while (*cmd == ' ' || *cmd == '\t') cmd++;
  if (!*cmd) return 0;
  if (term_starts(cmd, "set ") || term_eq(cmd, "set")) {
    if (term_eq(cmd, "set")) {
      int i;
      if (!term_nenv) video_text(8, 8, "(NO ENV VARS)", RGB15(31, 31, 31));
      for (i = 0; i < term_nenv && i < 8; i++) {
        char line[64];
        int j = 0, k = 0;
        while (term_env_names[i][j] && j < 30) { line[j] = term_env_names[i][j]; j++; }
        line[j++] = '=';
        while (term_env_vals[i][k] && j < 60) { line[j++] = term_env_vals[i][k++]; }
        line[j] = 0;
        video_text(8, 8 + i * 8, line, RGB15(25, 25, 25));
      }
    } else {
      const char *rest = cmd + 4;
      char nm[32], vl[64];
      term_split2(rest, nm, sizeof(nm), vl, sizeof(vl));
      if (nm[0]) { term_env_set(nm, vl); video_text(8, 8, "SET OK", RGB15(31, 31, 31)); }
      else video_text(8, 8, "USAGE: SET <NAME> <VALUE>", RGB15(31, 31, 0));
    }
    return 0;
  }
  if (term_starts(cmd, "unset ")) {
    const char *name = cmd + 6;
    int idx;
    while (*name == ' ') name++;
    idx = term_env_find(name);
    if (idx >= 0) {
      int i;
      for (i = idx; i + 1 < term_nenv; i++) {
        int j = 0;
        while (term_env_names[i + 1][j]) { term_env_names[i][j] = term_env_names[i + 1][j]; j++; }
        term_env_names[i][j] = 0;
        j = 0;
        while (term_env_vals[i + 1][j]) { term_env_vals[i][j] = term_env_vals[i + 1][j]; j++; }
        term_env_vals[i][j] = 0;
      }
      term_nenv--;
      video_text(8, 8, "UNSET OK", RGB15(31, 31, 31));
    } else video_text(8, 8, "UNSET: NOT SET", RGB15(31, 31, 0));
    return 0;
  }
  if (term_eq(cmd, "env")) {
    int i;
    if (!term_nenv) video_text(8, 8, "(NO ENV VARS)", RGB15(31, 31, 31));
    for (i = 0; i < term_nenv && i < 8; i++) {
      char line[64];
      int j = 0, k = 0;
      while (term_env_names[i][j] && j < 30) { line[j] = term_env_names[i][j]; j++; }
      line[j++] = '=';
      while (term_env_vals[i][k] && j < 60) { line[j++] = term_env_vals[i][k++]; }
      line[j] = 0;
      video_text(8, 8 + i * 8, line, RGB15(25, 25, 25));
    }
    return 0;
  }
  if (term_eq(cmd, "path")) { video_text(8, 8, term_path, RGB15(31, 31, 31)); return 0; }
  if (term_starts(cmd, "path ")) {
    const char *a = cmd + 5;
    int j = 0;
    while (*a == ' ') a++;
    while (a[j] && j < 63) { term_path[j] = a[j]; j++; }
    term_path[j] = 0;
    video_text(8, 8, "PATH SET (IN MEMORY)", RGB15(31, 31, 31));
    return 0;
  }
  if (term_eq(cmd, "time")) {
    char num[16];
    video_text(8, 8, "TICKS SINCE BOOT:", RGB15(31, 31, 31));
    term_num((int)timer_ticks(), num);
    video_text(8, 24, num, RGB15(31, 31, 0));
    video_text(8, 40, "NO RTC TIME ON GBA.", RGB15(25, 25, 25));
    return 0;
  }
  if (term_eq(cmd, "date")) {
    video_text(8, 8, "NO RTC DATE ON GBA.", RGB15(31, 31, 31));
    video_text(8, 24, "USE TIME FOR TICKS.", RGB15(25, 25, 25));
    return 0;
  }
  if (term_starts(cmd, "cd ") || term_starts(cmd, "chdir ") || term_eq(cmd, "cd") || term_eq(cmd, "chdir")) {
    const char *d = term_starts(cmd, "chdir ") ? cmd + 6 : (term_starts(cmd, "cd ") ? cmd + 3 : cmd);
    while (*d == ' ') d++;
    if (d[0]) {
      int j = 0;
      while (d[j] && j < 63) { term_cwd[j] = d[j]; j++; }
      term_cwd[j] = 0;
    }
    video_text(8, 8, term_cwd, RGB15(31, 31, 31));
    video_text(8, 24, "IN MEMORY ONLY, DIR IS FLAT.", RGB15(25, 25, 25));
    return 0;
  }
  if (term_eq(cmd, "pwd")) { video_text(8, 8, "/", RGB15(31, 31, 31)); return 0; }
  if (term_starts(cmd, "copy ")) {
    const char *rest = cmd + 5;
    char src[32], dst[32];
    u8 buf[512];
    int n;
    term_split2(rest, src, sizeof(src), dst, sizeof(dst));
    n = fs_read(src, buf, sizeof(buf));
    if (n < 0) video_text(8, 8, "COPY: COULD NOT READ SRC.", RGB15(31, 0, 0));
    else if (fs_write(dst, buf, (u32)n) == 0) video_text(8, 8, "COPY: 1 FILE COPIED", RGB15(31, 31, 31));
    else video_text(8, 8, "COPY: WRITE FAILED.", RGB15(31, 0, 0));
    return 0;
  }
  if (term_starts(cmd, "del ") || term_starts(cmd, "erase ")) {
    const char *name = term_starts(cmd, "erase ") ? cmd + 6 : cmd + 4;
    while (*name == ' ') name++;
    if (fs_delete(name) == 0) video_text(8, 8, "DEL: DELETED.", RGB15(31, 31, 31));
    else video_text(8, 8, "DEL: FAILED.", RGB15(31, 0, 0));
    return 0;
  }
  if (term_starts(cmd, "ren ") || term_starts(cmd, "rename ") || term_starts(cmd, "move ")) {
    const char *rest = term_starts(cmd, "rename ") ? cmd + 7 : (term_starts(cmd, "move ") ? cmd + 5 : cmd + 4);
    char src[32], dst[32];
    u8 buf[512];
    int n;
    term_split2(rest, src, sizeof(src), dst, sizeof(dst));
    n = fs_read(src, buf, sizeof(buf));
    if (n < 0) video_text(8, 8, "RENAME: NOT FOUND.", RGB15(31, 0, 0));
    else if (fs_write(dst, buf, (u32)n) == 0) {
      fs_delete(src);
      video_text(8, 8, "RENAMED.", RGB15(31, 31, 31));
    } else video_text(8, 8, "RENAME FAILED.", RGB15(31, 0, 0));
    return 0;
  }
  if (term_starts(cmd, "mkdir ") || term_starts(cmd, "md ")) {
    video_text(8, 8, "NO DIRECTORIES ON FLAT CART FS.", RGB15(31, 31, 0));
    return 0;
  }
  if (term_starts(cmd, "rd ") || term_eq(cmd, "rd")) {
    video_text(8, 8, "NO DIRECTORIES ON FLAT CART FS.", RGB15(31, 31, 0));
    return 0;
  }
  if (term_eq(cmd, "whoami")) { video_text(8, 8, term_current_user(), RGB15(31, 31, 31)); return 0; }
  if (term_eq(cmd, "hostname")) { video_text(8, 8, "FISIXGBA", RGB15(31, 31, 31)); return 0; }
  if (term_eq(cmd, "users")) {
    video_text(8, 8, term_current_user(), RGB15(31, 31, 31));
    video_text(8, 24, "ONLY ONE PROFILE ON GBA.", RGB15(25, 25, 25));
    return 0;
  }
  if (term_starts(cmd, "useradd ")) {
    video_text(8, 8, "GBA HAS ONE PROFILE.", RGB15(31, 31, 0));
    video_text(8, 24, "USE FIRST-TIME SETUP.", RGB15(25, 25, 25));
    return 0;
  }
  if (term_eq(cmd, "passwd")) { video_text(8, 8, "NO PASSWORDS ON GBA PROFILE.", RGB15(31, 31, 0)); return 0; }
  if (term_eq(cmd, "login")) {
    video_text(8, 8, "ALREADY LOGGED IN AS:", RGB15(31, 31, 31));
    video_text(8, 24, term_current_user(), RGB15(31, 31, 0));
    return 0;
  }
  if (term_starts(cmd, "grep ")) {
    const char *rest = cmd + 5;
    char pat[32], text[128];
    term_split2(rest, pat, sizeof(pat), text, sizeof(text));
    if (!pat[0]) { video_text(8, 8, "USAGE: GREP <PATTERN> <TEXT>", RGB15(31, 31, 0)); return 0; }
    if (term_contains(text, pat)) video_text(8, 8, text, RGB15(31, 31, 31));
    return 0;
  }
  if (term_starts(cmd, "size ")) {
    const char *name = cmd + 5;
    u8 buf[512];
    int n;
    char num[16];
    while (*name == ' ') name++;
    n = fs_read(name, buf, sizeof(buf));
    if (n < 0) video_text(8, 8, "SIZE: NOT FOUND.", RGB15(31, 0, 0));
    else {
      video_text(8, 8, name, RGB15(31, 31, 31));
      term_num(n, num);
      video_text(8, 24, num, RGB15(31, 31, 0));
      video_text(8, 40, "BYTES", RGB15(25, 25, 25));
    }
    return 0;
  }
  if (term_eq(cmd, "dir") || term_eq(cmd, "ls") || term_starts(cmd, "dir ") || term_starts(cmd, "ls ")) {
    char names[16][32];
    int n, i;
    video_text(8, 8, "CART FILES:", RGB15(31, 31, 31));
    n = fs_list(names, 16);
    for (i = 0; i < n && i < 8; i++) video_text(8, 24 + i * 8, names[i], RGB15(25, 25, 25));
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  if (term_starts(cmd, "echo ")) {
    const char *text = cmd + 5;
    while (*text == ' ') text++;
    if (in_patch) {
      if (patch_print_cy > 152) { video_clear(RGB15(0, 0, 10)); patch_print_cy = 8; }
      video_text(8, patch_print_cy, text, RGB15(31, 31, 31));
      patch_print_cy += 8;
    } else {
      video_text(8, 8, text, RGB15(31, 31, 31));
    }
    return 0;
  }
  if (term_eq(cmd, "echo")) { return 0; }
  if (term_ext_is(cmd, ".GEP") || term_ext_is(cmd, ".COM") || term_ext_is(cmd, ".PATCH") || term_ext_is(cmd, ".UPDATE")) {
    /* Direct filename run, like EFI .fse direct run. */
    char runline[64];
    int j = 0;
    const char *prefix = term_ext_is(cmd, ".GEP") ? "RUN " : (term_ext_is(cmd, ".COM") ? "DOS " : (term_ext_is(cmd, ".PATCH") ? "PATCH " : "UPDATE "));
    while (prefix[j]) { runline[j] = prefix[j]; j++; }
    {
      int k = 0;
      while (cmd[k] && j < 63) { runline[j++] = cmd[k++]; }
      runline[j] = 0;
    }
    return terminal_exec(runline, in_patch);
  }
  /* EFI parity, v1.2: & separator and echo|grep pipeline,
     checked before any single command, like EFI. */
  {
    int i;
    for (i = 0; cmd[i]; i++) if (cmd[i] == '&') {
      char left[64], right[64];
      int j = 0, k = 0;
      while (cmd[j] && cmd[j] != '&' && j < 63) { left[j] = cmd[j]; j++; }
      left[j] = 0;
      j++;
      while (cmd[j] && k < 63) { right[k++] = cmd[j++]; }
      right[k] = 0;
      terminal_exec(left, in_patch);
      return terminal_exec(right, in_patch);
    }
    for (i = 0; cmd[i]; i++) if (cmd[i] == '|') {
      if (term_starts(cmd, "echo ") || term_starts(cmd, "ECHO ")) {
        char left[64], pat[32];
        int j = 0, k = 0, m = 0;
        const char *r = cmd;
        while (r[j] && r[j] != '|' && j < 63) { left[j] = r[j]; j++; }
        left[j] = 0;
        j++;
        while (r[j] == ' ') j++;
        if (!term_starts(r + j, "grep ") && !term_starts(r + j, "GREP ")) {
          video_text(8, 8, "USAGE: ECHO <TEXT> | GREP <PATTERN>", RGB15(31, 31, 0));
          return 0;
        }
        r = r + j + 5;
        while (*r == ' ') r++;
        while (*r && m < 31) { pat[m++] = *r++; }
        pat[m] = 0;
        k = 0;
        while (left[k] && left[k] != ' ') k++;
        while (left[k] == ' ') k++;
        if (term_contains(left + k, pat)) video_text(8, 8, left + k, RGB15(31, 31, 31));
        return 0;
      } else {
        video_text(8, 8, "PIPES SUPPORT ECHO | GREP.", RGB15(31, 31, 0));
        return 0;
      }
    }
  }
  if (term_starts(cmd, "@echo ") || term_eq(cmd, "@echo")) {
    const char *a = cmd + 5;
    while (*a == ' ') a++;
    if (term_eq(a, "off")) term_echo_enabled = 0;
    else if (term_eq(a, "on")) term_echo_enabled = 1;
    return 0;
  }
  if (term_eq(cmd, "rem") || term_starts(cmd, "rem ") || term_starts(cmd, "::")) {
    return 0;
  }
  if (term_starts(cmd, "switchlang ") || term_eq(cmd, "switchlang")) {
    const char *a = cmd + 10;
    while (*a == ' ') a++;
    if (term_eq(a, "bash")) { term_mode_bash = 1; video_text(8, 8, "SWITCHED TO BASH.", RGB15(31, 31, 31)); }
    else if (term_eq(a, "batch")) { term_mode_bash = 0; video_text(8, 8, "SWITCHED TO BATCH.", RGB15(31, 31, 31)); }
    else video_text(8, 8, "USAGE: SWITCHLANG BASH | BATCH", RGB15(31, 31, 0));
    return 0;
  }
  if (term_eq(cmd, "bash")) { term_mode_bash = 1; video_text(8, 8, "BASH MODE ENABLED.", RGB15(31, 31, 31)); return 0; }
  if (term_eq(cmd, "batch")) { term_mode_bash = 0; video_text(8, 8, "BATCH MODE ENABLED.", RGB15(31, 31, 31)); return 0; }
  if (term_eq(cmd, "install")) { video_text(8, 8, "GBA IS ON A CART.", RGB15(31, 31, 31)); video_text(8, 24, "NO INSTALLER NEEDED.", RGB15(25, 25, 25)); return 0; }
  if (term_eq(cmd, "gui")) { app_gui(); video_clear(RGB15(0, 10, 0)); video_text(8, 8, "BACK FROM GUI.", RGB15(31, 31, 31)); if (in_patch) patch_print_cy = 48; return 0; }
  if (term_starts(cmd, "nano ") || term_eq(cmd, "nano")) {
    const char *a = cmd + 4;
    while (*a == ' ') a++;
    app_nano(a);
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  if (term_eq(cmd, "apps")) {
    video_text(8, 8, "APPS: NANO, CALC, CAT, MEMO, TODO", RGB15(31, 31, 31));
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  if (term_starts(cmd, "calc ") || term_eq(cmd, "calc")) {
    const char *s = cmd + 4;
    long a, b, r; char op; int neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    a = 0; while (*s >= '0' && *s <= '9') { a = a * 10 + (*s - '0'); s++; }
    if (neg) a = -a;
    while (*s == ' ') s++;
    op = *s++;
    neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    b = 0; while (*s >= '0' && *s <= '9') { b = b * 10 + (*s - '0'); s++; }
    if (neg) b = -b;
    if (op == '+') r = a + b;
    else if (op == '-') r = a - b;
    else if (op == '*') r = a * b;
    else if (op == '/' && b != 0) r = a / b;
    else if (op == '/' && b == 0) { video_text(8, 8, "CALC: DIVIDE BY ZERO", RGB15(31, 0, 0)); return 0; }
    else { video_text(8, 8, "CALC: USE A+B, A-B, A*B, A/B", RGB15(31, 31, 0)); return 0; }
    {
      char num[16];
      video_text(8, 8, "CALC:", RGB15(31, 31, 31));
      term_num((int)r, num);
      video_text(8, 24, num, RGB15(31, 31, 0));
    }
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  if (term_starts(cmd, "cat ") || term_starts(cmd, "type ")) {
    const char *name = term_starts(cmd, "cat ") ? cmd + 4 : cmd + 5;
    u8 buf[256];
    int n;
    while (*name == ' ') name++;
    if (!*name) { video_text(8, 8, "USAGE: CAT <FILE>", RGB15(31, 31, 0)); return 0; }
    n = fs_read(name, buf, sizeof(buf) - 1);
    if (n < 0) video_text(8, 8, "FILE NOT FOUND.", RGB15(31, 0, 0));
    else { buf[n] = 0; video_text(8, 8, (char*)buf, RGB15(31, 31, 31)); }
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  if (term_eq(cmd, "type")) { video_text(8, 8, "BAT SCRIPT EXECUTION SUPPORT ACTIVE.", RGB15(31, 31, 31)); return 0; }
  if (term_starts(cmd, "touch ")) {
    const char *name = cmd + 6;
    while (*name == ' ') name++;
    if (!*name) video_text(8, 8, "USAGE: TOUCH <FILE>", RGB15(31, 31, 0));
    else if (fs_write(name, (u8*)"", 0) == 0) video_text(8, 8, "TOUCHED.", RGB15(31, 31, 31));
    else video_text(8, 8, "TOUCH FAILED.", RGB15(31, 0, 0));
    return 0;
  }
  if (term_starts(cmd, "rm ")) {
    const char *name = cmd + 3;
    while (*name == ' ') name++;
    if (fs_delete(name) == 0) video_text(8, 8, "DELETED.", RGB15(31, 31, 31));
    else video_text(8, 8, "RM FAILED.", RGB15(31, 0, 0));
    return 0;
  }
  if (term_starts(cmd, "write ") || term_starts(cmd, "append ")) {
    int is_append = term_starts(cmd, "append ");
    const char *rest = is_append ? cmd + 7 : cmd + 6;
    char fname[32], text[128];
    term_split2(rest, fname, sizeof(fname), text, sizeof(text));
    if (!fname[0] || !text[0]) { video_text(8, 8, "USAGE: WRITE <FILE> <TEXT>", RGB15(31, 31, 0)); return 0; }
    if (is_append) {
      u8 old[512];
      int n = fs_read(fname, old, sizeof(old));
      int tl = 0, i;
      char out[512];
      if (n < 0) n = 0;
      for (i = 0; i < n && i < 500; i++) out[i] = (char)old[i];
      while (text[tl] && n + tl < 510) { out[n + tl] = text[tl]; tl++; }
      out[n + tl] = 0;
      if (fs_write(fname, (u8*)out, (u32)(n + tl)) == 0) video_text(8, 8, "APPENDED.", RGB15(31, 31, 31));
      else video_text(8, 8, "APPEND FAILED.", RGB15(31, 0, 0));
    } else {
      int tl = 0;
      while (text[tl]) tl++;
      if (fs_write(fname, (u8*)text, (u32)tl) == 0) video_text(8, 8, "WRITTEN.", RGB15(31, 31, 31));
      else video_text(8, 8, "WRITE FAILED.", RGB15(31, 0, 0));
    }
    return 0;
  }
  if (term_starts(cmd, "source ")) {
    const char *name = cmd + 7;
    int rc;
    while (*name == ' ') name++;
    rc = patch_run_file(name);
    video_clear(RGB15(0, 10, 0));
    if (rc == -3) video_text(8, 8, "SCRIPT NOT FOUND.", RGB15(31, 0, 0));
    else video_text(8, 8, "SOURCE DONE.", RGB15(31, 31, 31));
    return 0;
  }
  if (term_starts(cmd, "memo ") || term_eq(cmd, "memo")) {
    const char *text = cmd + 4;
    u8 buf[256];
    int n;
    while (*text == ' ') text++;
    if (*text) {
      int tl = 0, i;
      char out[512];
      n = fs_read("MEMO.TXT", buf, sizeof(buf));
      if (n < 0) n = 0;
      for (i = 0; i < n && i < 500; i++) out[i] = (char)buf[i];
      while (text[tl] && n + tl < 510) { out[n + tl] = text[tl]; tl++; }
      out[n + tl] = '\n'; out[n + tl + 1] = 0;
      fs_write("MEMO.TXT", (u8*)out, (u32)(n + tl + 1));
      video_text(8, 8, "MEMO SAVED.", RGB15(31, 31, 31));
    } else {
      n = fs_read("MEMO.TXT", buf, sizeof(buf) - 1);
      if (n < 0) video_text(8, 8, "(NO MEMO)", RGB15(31, 31, 31));
      else { buf[n] = 0; video_text(8, 8, (char*)buf, RGB15(31, 31, 31)); }
    }
    return 0;
  }
  if (term_starts(cmd, "todo ") || term_eq(cmd, "todo")) {
    const char *text = cmd + 4;
    u8 buf[256];
    int n;
    while (*text == ' ') text++;
    if (*text) {
      int tl = 0, i;
      char out[512];
      n = fs_read("TODO.TXT", buf, sizeof(buf));
      if (n < 0) n = 0;
      for (i = 0; i < n && i < 500; i++) out[i] = (char)buf[i];
      while (text[tl] && n + tl < 510) { out[n + tl] = text[tl]; tl++; }
      out[n + tl] = '\n'; out[n + tl + 1] = 0;
      fs_write("TODO.TXT", (u8*)out, (u32)(n + tl + 1));
      video_text(8, 8, "TODO SAVED.", RGB15(31, 31, 31));
    } else {
      n = fs_read("TODO.TXT", buf, sizeof(buf) - 1);
      if (n < 0) video_text(8, 8, "(NO TODO)", RGB15(31, 31, 31));
      else { buf[n] = 0; video_text(8, 8, (char*)buf, RGB15(31, 31, 31)); }
    }
    return 0;
  }
  if (term_eq(cmd, "status")) { video_text(8, 8, "OK", RGB15(31, 31, 31)); return 0; }
  if (term_starts(cmd, "system ") || term_eq(cmd, "system")) {
    const char *a = cmd + 6;
    while (*a == ' ') a++;
    video_text(8, 8, a, RGB15(31, 31, 31));
    return 0;
  }
  if (term_eq(cmd, "ver") || term_eq(cmd, "version")) {
    video_text(8, 8, "FISIXGBA V1.3", RGB15(31, 31, 31));
    video_text(8, 24, "FISIX OS BATCH TERMINAL 0.1", RGB15(25, 25, 25));
    return 0;
  }
  if (term_eq(cmd, "mem")) {
    video_text(8, 8, "MEMORY: 294912 BYTES", RGB15(31, 31, 31));
    video_text(8, 24, "EWRAM 256KB + IWRAM 32KB", RGB15(25, 25, 25));
    return 0;
  }
  if (term_eq(cmd, "clear") || term_eq(cmd, "cls")) {
    video_clear(RGB15(0, 10, 0));
    return 0;
  }
  if (term_eq(cmd, "pause")) {
    video_text(8, 8, "PRESS ANY KEY TO CONTINUE.", RGB15(31, 31, 0));
    {
      u16 old = input_read(), k;
      while (1) {
        video_wait_vblank();
        k = input_read();
        if (input_pressed(old, k)) break;
        old = k;
      }
    }
    return 0;
  }
  if (term_eq(cmd, "shutdown") || term_eq(cmd, "quit")) {
    video_text(8, 8, "SHUTDOWN COMMAND RECEIVED.", RGB15(31, 31, 31));
    return 1;
  }
  if (term_eq(cmd, "reboot")) {
    video_text(8, 8, "REBOOT COMMAND RECEIVED.", RGB15(31, 31, 31));
    video_text(8, 24, "USE EMULATOR RESET.", RGB15(25, 25, 25));
    return 0;
  }
  if (term_starts(cmd, "PATCH ") || term_eq(cmd, "PATCH")) {
    const char *name = cmd + 5;
    int rc;
    while (*name == ' ') name++;
    if (!*name) {
      video_text(8, 8, "USAGE: PATCH DEMO.PATCH", RGB15(31, 31, 31));
    } else if (term_ext_is(name, ".UPDATE")) {
      video_text(8, 8, "THAT IS AN UPDATE.", RGB15(31, 31, 0));
      video_text(8, 24, "USE UPDATE, NOT PATCH.", RGB15(31, 31, 0));
    } else if (patch_depth >= 3) {
      video_text(8, 8, "PATCH TOO DEEP.", RGB15(31, 0, 0));
    } else {
      patch_depth++;
      rc = patch_run_file(name);
      patch_depth--;
      if (in_patch) patch_print_cy = 48;
      else video_clear(RGB15(0, 10, 0));
      if (rc == -3) {
        video_text(8, 8, "PATCH NOT FOUND.", RGB15(31, 0, 0));
        video_text(8, 24, "TRY: LIST", RGB15(31, 31, 0));
      } else {
        video_text(8, 8, "PATCH DONE.", RGB15(31, 31, 31));
      }
    }
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (term_starts(cmd, "UPDATE ") || term_eq(cmd, "UPDATE")) {
    const char *name = cmd + 6;
    int app = 0, skp = 0, del = 0;
    unsigned int plat = 0;
    int rc;
    char num[16];
    while (*name == ' ') name++;
    if (!*name) {
      video_text(8, 8, "USAGE: UPDATE DEMO.UPDATE", RGB15(31, 31, 31));
    } else if (term_ext_is(name, ".PATCH")) {
      video_text(8, 8, "THAT IS A PATCH.", RGB15(31, 31, 0));
      video_text(8, 24, "USE PATCH, NOT UPDATE.", RGB15(31, 31, 0));
    } else if (term_ext_is(name, ".GEP")) {
      video_text(8, 8, "THAT IS A GEP.", RGB15(31, 31, 0));
      video_text(8, 24, "USE RUN, NOT UPDATE.", RGB15(31, 31, 0));
    } else {
      rc = update_run_file_counts(name, &app, &skp, &del, &plat);
      if (rc == -7) {
        video_text(8, 8, "UPDATE NOT FOUND.", RGB15(31, 0, 0));
        video_text(8, 24, "TRY: LIST", RGB15(31, 31, 0));
      } else if (rc == -3) {
        video_text(8, 8, "WRONG PLATFORM.", RGB15(31, 0, 0));
        video_text(8, 24, "UPDATE IS FOR:", RGB15(31, 31, 0));
        video_text(8, 32, update_platform_name(plat), RGB15(31, 31, 0));
        video_text(8, 40, "NOT GBA. NOTHING CHANGED.", RGB15(31, 31, 0));
      } else if (rc == -1) {
        video_text(8, 8, "NOT AN UPDATE FILE.", RGB15(31, 0, 0));
        video_text(8, 24, "BAD MAGIC.", RGB15(31, 31, 0));
      } else if (rc == -5) {
        video_text(8, 8, "UPDATE BAD CRC.", RGB15(31, 0, 0));
        video_text(8, 24, "NOTHING CHANGED.", RGB15(31, 31, 0));
      } else if (rc < 0) {
        video_text(8, 8, "UPDATE FAILED.", RGB15(31, 0, 0));
        term_num(rc, num);
        video_text(8, 24, num, RGB15(31, 31, 0));
        video_text(8, 32, "NOTHING CHANGED.", RGB15(31, 31, 0));
      } else {
        video_text(8, 8, "UPDATE APPLIED:", RGB15(31, 31, 31));
        term_num(app, num);
        video_text(8, 24, num, RGB15(31, 31, 0));
        video_text(8, 32, "SKIPPED/DELETED:", RGB15(25, 25, 25));
        /* skipped and deleted are shown as one line each
           below the applied count in patch mode; interactive
           PRESS B at y=40 will overwrite the deleted line,
           which is acceptable — applied is the main number. */
        term_num(skp, num);
        video_text(8, 40, num, RGB15(31, 31, 0));
        (void)del;
      }
    }
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (term_starts(cmd, "DOS ") || term_eq(cmd, "DOS")) {
    const char *name = cmd + 3;
    int rc;
    while (*name == ' ') name++;
    if (!*name) {
      video_text(8, 8, "USAGE: DOS HELLO.COM", RGB15(31, 31, 31));
    } else {
      rc = dosemu_run_file(name);
      video_clear(RGB15(0, 10, 0));
      if (rc == -1) {
        video_text(8, 8, "COM NOT FOUND.", RGB15(31, 0, 0));
        video_text(8, 24, "TRY: LIST", RGB15(31, 31, 0));
      } else if (rc == 2) {
        video_text(8, 8, "COM UNSUPPORTED ON GBA.", RGB15(31, 0, 0));
        video_text(8, 24, "GBA DOS IS SIMPLE ONLY.", RGB15(31, 31, 0));
        video_text(8, 32, "FULL DOS IS PC ONLY.", RGB15(31, 31, 0));
      } else if (rc == 3) {
        video_text(8, 8, "COM TOO BIG.", RGB15(31, 0, 0));
      } else {
        video_text(8, 8, "DOS OUTPUT:", RGB15(31, 31, 31));
        video_text(8, 24, dosemu_last_output(), RGB15(31, 31, 0));
      }
    }
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (term_starts(cmd, "RUN ") || term_eq(cmd, "RUN")) {
    const char *name = cmd + 3;
    int rc;
    char num[16];
    while (*name == ' ') name++;
    if (!*name) {
      video_text(8, 8, "USAGE: RUN HELLO.GEP", RGB15(31, 31, 31));
    } else if (term_ext_is(name, ".PATCH")) {
      /* A .patch name through RUN is still a patch, exactly
         as if you had typed PATCH — same parser, same result. */
      if (patch_depth >= 3) {
        video_text(8, 8, "PATCH TOO DEEP.", RGB15(31, 0, 0));
      } else {
        patch_depth++;
        rc = patch_run_file(name);
        patch_depth--;
        video_clear(RGB15(0, 10, 0));
        if (rc == -3) video_text(8, 8, "PATCH NOT FOUND.", RGB15(31, 0, 0));
        else video_text(8, 8, "PATCH DONE.", RGB15(31, 31, 31));
      }
    } else if (term_ext_is(name, ".UPDATE")) {
      video_text(8, 8, "THAT IS AN UPDATE.", RGB15(31, 31, 0));
      video_text(8, 24, "USE UPDATE, NOT RUN.", RGB15(31, 31, 0));
    } else if (term_ext_is(name, ".COM")) {
      rc = dosemu_run_file(name);
      video_clear(RGB15(0, 10, 0));
      if (rc == -1) video_text(8, 8, "COM NOT FOUND.", RGB15(31, 0, 0));
      else if (rc == 2) { video_text(8, 8, "COM UNSUPPORTED ON GBA.", RGB15(31, 0, 0)); video_text(8, 24, "GBA DOS IS SIMPLE ONLY.", RGB15(31, 31, 0)); }
      else {
        video_text(8, 8, "DOS OUTPUT:", RGB15(31, 31, 31));
        video_text(8, 24, dosemu_last_output(), RGB15(31, 31, 0));
      }
    } else {
      rc = gep_run_file(name);
      video_clear(RGB15(0, 10, 0));
      if (rc >= 0) {
        video_text(8, 8, "GEP EXITED, CODE:", RGB15(31, 31, 31));
        term_num(rc, num);
        video_text(8, 24, num, RGB15(31, 31, 0));
      } else if (rc == -1) {
        video_text(8, 8, "GEP BAD HEADER.", RGB15(31, 0, 0));
        video_text(8, 24, "PC FSE FILES ARE NOT GEP.", RGB15(31, 31, 0));
      } else if (rc == -3) {
        video_text(8, 8, "GEP NOT FOUND.", RGB15(31, 0, 0));
        video_text(8, 24, "TRY: LIST", RGB15(31, 31, 0));
      } else {
        video_text(8, 8, "GEP LOAD FAILED.", RGB15(31, 0, 0));
        term_num(rc, num);
        video_text(8, 24, num, RGB15(31, 31, 0));
      }
    }
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (term_starts(cmd, "PRINT ") || term_eq(cmd, "PRINT")) {
    const char *text = cmd + 5;
    while (*text == ' ') text++;
    if (in_patch) {
      if (patch_print_cy > 152) {
        video_clear(RGB15(0, 0, 10));
        patch_print_cy = 8;
      }
      video_text(8, patch_print_cy, text, RGB15(31, 31, 31));
      patch_print_cy += 8;
    } else {
      video_text(8, 8, text, RGB15(31, 31, 31));
    }
    return 0;
  }
  else if (term_starts(cmd, "SEND ") || term_eq(cmd, "SEND")) {
    const char *rest = cmd + 4;
    char chan[16];
    int j = 0;
    const char *text;
    int len;
    while (*rest == ' ') rest++;
    while (rest[j] && rest[j] != ' ' && rest[j] != '\t' && j < 15) { chan[j] = rest[j]; j++; }
    chan[j] = 0;
    text = rest + j;
    while (*text == ' ') text++;
    len = 0;
    while (text[len]) len++;
    if (pipe_write(chan, (const u8*)text, (u32)len) == 0)
      video_text(8, 8, "SENT.", RGB15(31, 31, 31));
    else
      video_text(8, 8, "SEND FAILED.", RGB15(31, 0, 0));
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (term_starts(cmd, "RECV ") || term_eq(cmd, "RECV")) {
    const char *chan = cmd + 4;
    u8 buf[256];
    int n;
    while (*chan == ' ') chan++;
    n = pipe_read(chan, buf, sizeof(buf) - 1);
    if (n < 0) {
      video_text(8, 8, "NO MESSAGE.", RGB15(31, 31, 31));
    } else {
      char out[256];
      int k;
      for (k = 0; k < n && k < 255; k++) out[k] = (char)buf[k];
      out[k] = 0;
      video_text(8, 8, out, RGB15(31, 31, 31));
    }
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (term_starts(cmd, "CLEAR ") || term_eq(cmd, "CLEAR")) {
    const char *chan = cmd + 5;
    while (*chan == ' ') chan++;
    if (pipe_clear(chan) == 0) video_text(8, 8, "CLEARED.", RGB15(31, 31, 31));
    else video_text(8, 8, "NO SUCH CHANNEL.", RGB15(31, 31, 31));
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (term_starts(cmd, "WAIT ") || term_eq(cmd, "WAIT")) {
    const char *numstr = cmd + 4;
    int ms = 0;
    u32 start, wait;
    while (*numstr == ' ') numstr++;
    while (*numstr >= '0' && *numstr <= '9') { ms = ms * 10 + (*numstr - '0'); numstr++; }
    start = timer_ticks();
    wait = (u32)ms * 16384u / 1000u;
    while (timer_ticks() - start < wait) { }
    return 0;
  }
  else if (term_eq(cmd, "LIST") || term_eq(cmd, "L")) {
    char names[16][32];
    int n, i;
    video_text(8, 8, "CART FILES:", RGB15(31, 31, 31));
    n = fs_list(names, 16);
    for (i = 0; i < n && i < 8; i++) video_text(8, 24 + i * 8, names[i], RGB15(25, 25, 25));
    video_text(8, 104, "BUILT-IN GEP:", RGB15(31, 31, 31));
    n = gep_list_embedded(names, 16);
    for (i = 0; i < n && i < 4; i++) video_text(8, 120 + i * 8, names[i], RGB15(31, 31, 0));
    video_text(8, 144, "BUILT-IN PATCH:", RGB15(31, 31, 31));
    n = patch_list_embedded(names, 16);
    for (i = 0; i < n && i < 1; i++) video_text(8, 152, names[i], RGB15(31, 31, 0));
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (term_eq(cmd, "HELP") || cmd[0] == 'H' || cmd[0] == 'h') {
    video_text(8, 8, "HELP: HELP ECHO STATUS SYSTEM VER", RGB15(31, 31, 31));
    video_text(8, 16, "DIR LS MEM CLEAR CLS PAUSE EXIT", RGB15(25, 25, 25));
    video_text(8, 24, "SHUTDOWN REBOOT TYPE SET TIME DATE", RGB15(25, 25, 25));
    video_text(8, 32, "CD COPY DEL REN MD RD PATH WHOAMI", RGB15(25, 25, 25));
    video_text(8, 40, "PWD SWITCHLANG INSTALL GUI RUN", RGB15(25, 25, 25));
    video_text(8, 48, "NANO APPS CALC CAT TOUCH RM WRITE", RGB15(25, 25, 25));
    video_text(8, 56, "APPEND SOURCE MEMO TODO GREP USERS", RGB15(25, 25, 25));
    video_text(8, 64, "USERADD PASSWD LOGIN BASH BATCH", RGB15(25, 25, 25));
    video_text(8, 72, "LIST PATCH UPDATE DOS PRINT SEND", RGB15(25, 25, 25));
    video_text(8, 80, "RECV CLEAR WAIT SIZE ENV UNSET", RGB15(25, 25, 25));
    video_text(8, 88, "ERASE RENAME MOVE CHDIR QUIT", RGB15(25, 25, 25));
    video_text(8, 96, "ECHO TEXT | GREP PATTERN, CMD & CMD", RGB15(25, 25, 25));
    video_text(8, 104, "GBA LIMITS: FLAT FS, NO RTC, 1 USER", RGB15(31, 31, 0));
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (cmd[0] == 'F' || cmd[0] == 'f') {
    video_text(8, 8, "FILES REMOVED.", RGB15(31, 31, 31));
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (term_eq(cmd, "ABOUT") || cmd[0] == 'A' || cmd[0] == 'a') {
    video_text(8, 8, "FISIXGBA: ARM PORT OF FISIXOS.", RGB15(31, 31, 31));
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
  else if (term_eq(cmd, "EXIT") || cmd[0] == 'E' || cmd[0] == 'e') {
    return 1;
  }
  else {
    video_text(8, 8, "UNKNOWN COMMAND.", RGB15(31, 31, 31));
    video_text(8, 24, cmd, RGB15(31, 31, 0));
    if (in_patch) patch_print_cy = 48;
    return 0;
  }
}

void app_terminal(void) {
  char cmd[64];
  int done = 0;
  /* v0.3 input scheme (see docs/INPUT.md):
     SELECT opens keyboard (GBA has no Y button),
     keyboard: D-pad + A type, B delete, START sends. */
  while (!done) {
    video_clear(RGB15(0, 10, 0));
    video_text(8, 8, "TERMINAL", RGB15(31, 31, 31));
    video_text(8, 24, "HELP ABOUT EXIT RUN LIST", RGB15(25, 25, 25));
    video_text(8, 40, "SELECT: KEYBOARD  B: EXIT", RGB15(31, 31, 0));
    {
      u16 old = input_read(), k;
      while (1) {
        video_wait_vblank();
        k = input_read();
        if (input_pressed(old, k) & KEY_SELECT) break;
        if (input_pressed(old, k) & KEY_B) { done = 1; break; }
        old = k;
      }
      if (done) break;
    }
    if (keyboard_input(cmd, sizeof(cmd), term_mode_bash ? "bash$" : "FISIXGBA>") < 0) {
      continue; /* SELECT in keyboard = cancel, no command sent */
    }
    video_clear(RGB15(0, 10, 0));
    if (terminal_exec(cmd, 0) == 1) done = 1;
    if (!done) {
      video_text(8, 152, "PRESS B TO CONTINUE.", RGB15(31, 31, 0));
      wait_b();
    }
  }
}

/* Pong, v0.6 dirty-rect version: NO video_clear in the loop.
   Full clear + draw happens once at start. After that, only
   the rectangles that actually moved are erased (black) and
   redrawn, and only after video_wait_vblank(), so the small
   dirty draw happens in VBlank instead of mid-scan. If a
   paddle did not move, it is not redrawn at all. The ball
   moves every frame, but it is only 8x8 pixels, not 240x160. */
void app_pong(void) {
  int bx = 120, by = 80, dx = 2, dy = 1;
  int py = 60, ay = 60, ps = 0, as = 0;
  int obx = 120, oby = 80, opy = 60, oay = 60;
  u16 old = 0;
  video_clear(0);
  video_rect(8, py, 4, 32, RGB15(31, 31, 31));
  video_rect(228, ay, 4, 32, RGB15(31, 31, 31));
  video_rect(bx, by, 8, 8, RGB15(31, 31, 0));
  while (1) {
    video_wait_vblank();
    {
      u16 keys = input_read();
      u16 p = input_pressed(old, keys);
      if (p & KEY_B) break;
      if ((keys & KEY_UP) && py > 0) py -= 3;
      if ((keys & KEY_DOWN) && py < 128) py += 3;
      old = keys;
      bx += dx; by += dy;
      if (by <= 0 || by >= 152) dy = -dy;
      if (bx <= 12 && by >= py && by <= py + 32) { dx = 2; audio_beep(440, 30); }
      if (bx >= 220 && by >= ay && by <= ay + 32) { dx = -2; audio_beep(440, 30); }
      if (bx < 0) { as++; bx = 120; by = 80; }
      if (bx > 232) { ps++; bx = 120; by = 80; }
      if (by > ay + 16) ay += 2; else if (by < ay + 16) ay -= 2;
      if (ay < 0) ay = 0;
      if (ay > 128) ay = 128;
      /* Dirty only: erase old, draw new, per moving object. */
      if (bx != obx || by != oby) {
        video_rect(obx, oby, 8, 8, 0);
        video_rect(bx, by, 8, 8, RGB15(31, 31, 0));
        obx = bx; oby = by;
      }
      if (py != opy) {
        video_rect(8, opy, 4, 32, 0);
        video_rect(8, py, 4, 32, RGB15(31, 31, 31));
        opy = py;
      }
      if (ay != oay) {
        video_rect(228, oay, 4, 32, 0);
        video_rect(228, ay, 4, 32, RGB15(31, 31, 31));
        oay = ay;
      }
    }
  }
  (void)ps; (void)as;
}

void app_link(void) {
  /* Link menu: UP/DOWN picks role, A starts.
     Master: HELLO -> wait ACK -> send USER.PROF as PUT/DATA/DONE.
     Slave: wait HELLO -> ACK -> receive PUT/DATA -> save to cart. */
  int role = 0; /* 0 master, 1 slave */
  u16 old = input_read(); /* snapshot: held menu A does not start Link */
  /* v0.6 dirty: full draw once, then only the two role lines
     when the role actually changes. Idle frames draw nothing. */
  video_clear(RGB15(10, 0, 10));
  video_text(8, 8, "FISIXLINK", RGB15(31, 31, 31));
  video_text(8, 24, "> SEND (MASTER)", RGB15(25, 25, 25));
  video_text(8, 40, "  RECEIVE (SLAVE)", RGB15(25, 25, 25));
  video_text(8, 64, "CONNECT LINK CABLE FIRST.", RGB15(25, 25, 25));
  video_text(8, 80, "A START  B BACK", RGB15(31, 31, 0));
  while (1) {
    u16 k, pr;
    int orole;
    video_wait_vblank();
    k = input_read(); pr = input_pressed(old, k);
    if (pr & KEY_B) break;
    if (pr & KEY_A) goto start_link;
    orole = role;
    if (pr & KEY_UP) role = 0;
    if (pr & KEY_DOWN) role = 1;
    old = k;
    if (role != orole) {
      video_rect(8, 24, 160, 8, RGB15(10, 0, 10));
      video_rect(8, 40, 176, 8, RGB15(10, 0, 10));
      video_text(8, 24, role == 0 ? "> SEND (MASTER)" : "  SEND (MASTER)", RGB15(25, 25, 25));
      video_text(8, 40, role == 1 ? "> RECEIVE (SLAVE)" : "  RECEIVE (SLAVE)", RGB15(25, 25, 25));
    }
  }
  return;
start_link:
  video_clear(RGB15(10, 0, 10));
  if (role == 0) {
    u8 type, payload[512];
    u16 len;
    u8 prof[512];
    int plen;
    link_set_master(1);
    video_text(8, 8, "SENDING HELLO...", RGB15(31, 31, 31));
    if (link_send_packet(0x01, (const u8*)"FISIXGBA", 8) != 0) {
      video_text(8, 24, "HELLO FAILED: NO SLAVE.", RGB15(31, 0, 0));
      video_text(8, 40, "B: BACK", RGB15(31, 31, 0));
      wait_b(); return;
    }
    if (link_recv_packet(&type, payload, &len) != 0 || type != 0x07) {
      video_text(8, 24, "NO ACK FROM SLAVE.", RGB15(31, 0, 0));
      video_text(8, 40, "B: BACK", RGB15(31, 31, 0));
      wait_b(); return;
    }
    plen = fs_read("USER.PROF", prof, sizeof(prof));
    if (plen < 0) plen = 0;
    video_text(8, 24, "SENDING USER.PROF...", RGB15(31, 31, 31));
    if (link_send_packet(0x05, (const u8*)"USER.PROF", 9) != 0 ||
        link_send_packet(0x06, prof, (u16)plen) != 0 ||
        link_send_packet(0x09, 0, 0) != 0) {
      video_text(8, 40, "SEND FAILED.", RGB15(31, 0, 0));
    } else {
      video_text(8, 40, "SENT. CHECK OTHER GBA.", RGB15(20, 31, 20));
    }
    video_text(8, 56, "B: BACK", RGB15(31, 31, 0));
    wait_b(); return;
  } else {
    u8 type, payload[512];
    u16 len;
    char fname[32];
    int have_name = 0;
    link_set_master(0);
    video_text(8, 8, "WAITING FOR HELLO...", RGB15(31, 31, 31));
    video_text(8, 24, "B CANCELS BETWEEN PACKETS.", RGB15(25, 25, 25));
    while (1) {
      if (link_recv_packet(&type, payload, &len) == 0) {
        if (type == 0x01) {
          link_send_packet(0x07, 0, 0);
          video_text(8, 40, "HELLO OK. WAITING FILE...", RGB15(20, 31, 20));
        } else if (type == 0x05) {
          int i;
          for (i = 0; i < 31 && i < len; i++) fname[i] = (char)payload[i];
          fname[i] = 0;
          have_name = 1;
        } else if (type == 0x06 && have_name) {
          fs_write(fname, payload, len);
          video_text(8, 56, "FILE SAVED TO THIS CART.", RGB15(20, 31, 20));
        } else if (type == 0x09) {
          video_text(8, 72, "DONE. B: BACK", RGB15(31, 31, 0));
          wait_b(); return;
        }
      } else {
        if (input_read() & KEY_B) break;
      }
    }
    return;
  }
}

/* GUI mouse mode, v0.4 (see docs/INPUT.md):
   D-pad moves cursor, A = left click (launch),
   B = right click (info), SELECT/START = exit to menu.
   GBA has no Y button, so B is the right-click button.
   Only 2 icons now, not a desktop full of them: Terminal
   and Pong. Link stays in the menu, not as an icon. */
/* GUI mouse mode, v0.6 dirty version: NO video_clear in the loop.
   Full draw happens once at start (and once after returning
   from a launched app, because that app overwrote the screen).
   After that:
   - if the cursor did not move and info did not change, NOTHING
     is drawn at all, the loop just waits for VBlank and polls;
   - if the cursor moved, only its 9x9 box is restored from the
     pixels saved underneath it, then the new cursor is drawn;
   - if info changed, only the info line (y=80) is erased and
     redrawn.
   Cursor save/restore: before drawing a cursor, the 81 pixels
   under its 9x9 box are copied to saved[]. Restoring them puts
   back exactly whatever icon/text was underneath, no full
   background redraw needed. */
static void gui_save_box(int mx, int my, u16 *saved) {
  int dx, dy, n = 0;
  for (dy = -4; dy <= 4; dy++)
    for (dx = -4; dx <= 4; dx++) {
      int x = mx + dx, y = my + dy;
      saved[n++] = (x >= 0 && x < 240 && y >= 0 && y < 160)
        ? VRAM[y * 240 + x] : RGB15(5, 5, 15);
    }
}
static void gui_restore_box(int mx, int my, const u16 *saved) {
  int dx, dy, n = 0;
  for (dy = -4; dy <= 4; dy++)
    for (dx = -4; dx <= 4; dx++) {
      int x = mx + dx, y = my + dy;
      if (x >= 0 && x < 240 && y >= 0 && y < 160)
        VRAM[y * 240 + x] = saved[n];
      n++;
    }
}
static void gui_draw_cursor(int mx, int my) {
  video_rect(mx - 4, my, 9, 1, RGB15(31, 31, 31));
  video_rect(mx, my - 4, 1, 9, RGB15(31, 31, 31));
}
void app_gui(void) {
  static const char *names[2] = {"TERMINAL", "PONG"};
  static const int ix[2] = {16, 96};
  int mx = 120, my = 80, i, need_full = 1;
  int omx = 120, omy = 80, info_dirty = 0;
  char info[40];
  u16 saved[81];
  u16 old = input_read();
  info[0] = 0;
  while (1) {
    u16 keys, pr;
    int moved = 0;
    if (need_full) {
      video_clear(RGB15(5, 5, 15));
      video_text(8, 8, "FISIXGBA GUI - MOUSE MODE", RGB15(31, 31, 31));
      video_text(8, 20, "D-PAD MOVE  A CLICK  B RIGHT  SELECT EXIT", RGB15(20, 20, 20));
      for (i = 0; i < 2; i++) {
        video_rect(ix[i], 32, 48, 32, RGB15(10, 10, 25));
        video_text(ix[i] + 4, 44, names[i], RGB15(25, 25, 25));
      }
      if (info[0]) video_text(8, 80, info, RGB15(31, 31, 0));
      gui_save_box(mx, my, saved);
      gui_draw_cursor(mx, my);
      omx = mx; omy = my;
      need_full = 0;
      info_dirty = 0;
    }
    keys = input_read();
    pr = input_pressed(old, keys);
    if (pr & KEY_SELECT) break;
    if (pr & KEY_START) break;
    if (keys & KEY_UP) my -= 3;
    if (keys & KEY_DOWN) my += 3;
    if (keys & KEY_LEFT) mx -= 3;
    if (keys & KEY_RIGHT) mx += 3;
    if (mx < 0) mx = 0;
    if (mx > 239) mx = 239;
    if (my < 0) my = 0;
    if (my > 159) my = 159;
    if (mx != omx || my != omy) moved = 1;
    if (pr & KEY_A) {
      int hit = -1;
      for (i = 0; i < 2; i++)
        if (mx >= ix[i] && mx < ix[i] + 48 && my >= 32 && my < 64) hit = i;
      if (hit >= 0) {
        if (hit == 0) app_terminal();
        else app_pong();
        old = input_read();
        need_full = 1;
        continue;
      }
    }
    if (pr & KEY_B) {
      int hit = -1;
      for (i = 0; i < 2; i++)
        if (mx >= ix[i] && mx < ix[i] + 48 && my >= 32 && my < 64) hit = i;
      if (hit >= 0) {
        int j = 0;
        const char *p = "RIGHT CLICK: ";
        while (*p) info[j++] = *p++;
        p = names[hit];
        while (*p && j < 38) info[j++] = *p++;
        info[j] = 0;
      } else {
        info[0] = 'R'; info[1] = 'I'; info[2] = 'G'; info[3] = 'H'; info[4] = 'T';
        info[5] = ' '; info[6] = 'C'; info[7] = 'L'; info[8] = 'I'; info[9] = 'C';
        info[10] = 'K'; info[11] = ' '; info[12] = 'O'; info[13] = 'N'; info[14] = ' ';
        info[15] = 'E'; info[16] = 'M'; info[17] = 'P'; info[18] = 'T'; info[19] = 'Y';
        info[20] = ' '; info[21] = 'S'; info[22] = 'P'; info[23] = 'A'; info[24] = 'C';
        info[25] = 'E'; info[26] = 0;
      }
      info_dirty = 1;
    }
    old = keys;
    if (!moved && !info_dirty) {
      video_wait_vblank();
      continue; /* idle: draw nothing at all */
    }
    video_wait_vblank();
    /* Restore old cursor first, so an info-line erase that
       overlaps the cursor does not leave a ghost, then draw
       the new info, then save and draw the cursor again.
       Still only the 9x9 cursor box + the info line. */
    gui_restore_box(omx, omy, saved);
    if (info_dirty) {
      video_rect(8, 80, 232, 8, RGB15(5, 5, 15));
      if (info[0]) video_text(8, 80, info, RGB15(31, 31, 0));
      info_dirty = 0;
    }
    gui_save_box(mx, my, saved);
    gui_draw_cursor(mx, my);
    omx = mx; omy = my;
  }
}

/* PowerShell-style shell, v1.1 — NOT Microsoft PowerShell.
   Microsoft PowerShell is open source, but it needs .NET,
   a garbage collector, and a Windows/.NET runtime — none
   of which exist on a GBA, C64, or Apple IIe, and none of
   which FisixGBA provides. This is a small shell with
   PowerShell-style syntax (Verb-Noun cmdlets, $variables,
   Write-Output, Get-ChildItem, etc.), as an alternative
   to Terminal/ReTerm. It runs on the same cart filesystem
   and GEP loader as Terminal, so its commands are real
   here, just a subset.
   Cmdlets: Get-ChildItem (dir, ls), Get-Content,
   Write-Output, Clear-Host (cls), Get-Help, Get-Date
   (no RTC, so it prints timer ticks, honestly),
   Start-Process <gep>, Exit. $var = text stores a
   variable (up to 4), Write-Output $var expands it. */
static char ps_vars[4][32];
static char ps_vals[4][64];
static void ps_set_var(const char *name, const char *val) {
  int i, slot = -1;
  for (i = 0; i < 4; i++) if (ps_vars[i][0] && term_eq(ps_vars[i], name)) { slot = i; break; }
  if (slot < 0) for (i = 0; i < 4; i++) if (!ps_vars[i][0]) { slot = i; break; }
  if (slot < 0) return;
  {
    int j = 0;
    while (name[j] && j < 31) { ps_vars[slot][j] = name[j]; j++; }
    ps_vars[slot][j] = 0;
    j = 0;
    while (val[j] && j < 63) { ps_vals[slot][j] = val[j]; j++; }
    ps_vals[slot][j] = 0;
  }
}
static const char *ps_get_var(const char *name) {
  int i;
  for (i = 0; i < 4; i++) if (ps_vars[i][0] && term_eq(ps_vars[i], name)) return ps_vals[i];
  return 0;
}
static void ps_expand(const char *in, char *out, int maxlen) {
  int i = 0, o = 0;
  while (in[i] && o < maxlen - 1) {
    if (in[i] == '$') {
      char name[32];
      int j = 0;
      const char *v;
      i++;
      while (in[i] && in[i] != ' ' && j < 31) { name[j++] = in[i++]; }
      name[j] = 0;
      v = ps_get_var(name);
      if (v) while (*v && o < maxlen - 1) out[o++] = *v++;
    } else {
      out[o++] = in[i++];
    }
  }
  out[o] = 0;
}
void app_powershell(void) {
  char cmd[64];
  int done = 0;
  while (!done) {
    video_clear(RGB15(10, 0, 10));
    video_text(8, 8, "POWERSHELL-STYLE SHELL", RGB15(31, 31, 31));
    video_text(8, 24, "NOT MICROSOFT POWERSHELL.", RGB15(25, 25, 25));
    video_text(8, 40, "GET-HELP FOR CMDELETS.", RGB15(31, 31, 0));
    video_text(8, 56, "SELECT: KEYBOARD  B: EXIT", RGB15(31, 31, 0));
    {
      u16 old = input_read(), k;
      while (1) {
        video_wait_vblank();
        k = input_read();
        if (input_pressed(old, k) & KEY_SELECT) break;
        if (input_pressed(old, k) & KEY_B) { done = 1; break; }
        old = k;
      }
      if (done) break;
    }
    if (keyboard_input(cmd, sizeof(cmd), "PS>") < 0) continue;
    video_clear(RGB15(10, 0, 10));
    if (cmd[0] == '$') {
      /* $var = value */
      char name[32];
      int i = 1, j = 0;
      const char *val;
      while (cmd[i] && cmd[i] != ' ' && cmd[i] != '=' && j < 31) { name[j++] = cmd[i++]; }
      name[j] = 0;
      while (cmd[i] == ' ' || cmd[i] == '=') i++;
      val = cmd + i;
      ps_set_var(name, val);
      video_text(8, 8, "VARIABLE SET.", RGB15(31, 31, 31));
    } else if (term_starts(cmd, "Write-Output ") || term_eq(cmd, "Write-Output")) {
      char out[64];
      const char *text = cmd + 12;
      while (*text == ' ') text++;
      ps_expand(text, out, sizeof(out));
      video_text(8, 8, out, RGB15(31, 31, 31));
    } else if (term_starts(cmd, "Get-ChildItem") || term_eq(cmd, "dir") || term_eq(cmd, "ls")) {
      char names[16][32];
      int n, i;
      video_text(8, 8, "CART FILES:", RGB15(31, 31, 31));
      n = fs_list(names, 16);
      for (i = 0; i < n && i < 8; i++) video_text(8, 24 + i * 8, names[i], RGB15(25, 25, 25));
    } else if (term_starts(cmd, "Get-Content ")) {
      const char *name = cmd + 12;
      u8 buf[256];
      int n;
      while (*name == ' ') name++;
      n = fs_read(name, buf, sizeof(buf) - 1);
      if (n < 0) video_text(8, 8, "FILE NOT FOUND.", RGB15(31, 0, 0));
      else { buf[n] = 0; video_text(8, 8, (char*)buf, RGB15(31, 31, 31)); }
    } else if (term_starts(cmd, "Start-Process ")) {
      const char *name = cmd + 14;
      int rc;
      while (*name == ' ') name++;
      rc = gep_run_file(name);
      video_clear(RGB15(10, 0, 10));
      if (rc < 0) video_text(8, 8, "PROCESS NOT FOUND.", RGB15(31, 0, 0));
      else video_text(8, 8, "PROCESS EXITED.", RGB15(31, 31, 31));
    } else if (term_eq(cmd, "Clear-Host") || term_eq(cmd, "cls")) {
      video_clear(RGB15(10, 0, 10));
      video_text(8, 8, "CLEARED.", RGB15(31, 31, 31));
    } else if (term_eq(cmd, "Get-Help")) {
      video_text(8, 8, "GET-CHILDITEM GET-CONTENT", RGB15(31, 31, 31));
      video_text(8, 24, "WRITE-OUTPUT CLEAR-HOST", RGB15(25, 25, 25));
      video_text(8, 40, "START-PROCESS GET-DATE EXIT", RGB15(25, 25, 25));
    } else if (term_eq(cmd, "Get-Date")) {
      char num[16];
      term_num((int)timer_ticks(), num);
      video_text(8, 8, "TICKS SINCE BOOT:", RGB15(31, 31, 31));
      video_text(8, 24, num, RGB15(31, 31, 0));
      video_text(8, 40, "NO RTC DATE ON GBA.", RGB15(25, 25, 25));
    } else if (term_eq(cmd, "Exit")) {
      done = 1;
    } else {
      video_text(8, 8, "UNKNOWN CMDLET.", RGB15(31, 31, 31));
      video_text(8, 24, cmd, RGB15(31, 31, 0));
    }
    if (!done) {
      video_text(8, 120, "PRESS B TO CONTINUE.", RGB15(31, 31, 0));
      wait_b();
    }
  }
}

/* Bash-style shell, v1.1 — NOT GNU bash.
   GNU bash needs POSIX, fork/exec, a filesystem with
   directories, and a libc — none of which exist on a
   GBA, C64, or Apple IIe in this form. This is a small
   shell with bash-style syntax (echo, ls, cat, $var,
   ./program, clear, help, exit), as a second alternative
   to Terminal/ReTerm, for users who still want bash
   after PowerShell-style. Same cart/GEP backend as
   Terminal, so its commands are real here, just a subset. */
void app_bash(void) {
  char cmd[64];
  int done = 0;
  while (!done) {
    video_clear(RGB15(0, 10, 10));
    video_text(8, 8, "BASH-STYLE SHELL", RGB15(31, 31, 31));
    video_text(8, 24, "NOT GNU BASH.", RGB15(25, 25, 25));
    video_text(8, 40, "HELP FOR COMMANDS.", RGB15(31, 31, 0));
    video_text(8, 56, "SELECT: KEYBOARD  B: EXIT", RGB15(31, 31, 0));
    {
      u16 old = input_read(), k;
      while (1) {
        video_wait_vblank();
        k = input_read();
        if (input_pressed(old, k) & KEY_SELECT) break;
        if (input_pressed(old, k) & KEY_B) { done = 1; break; }
        old = k;
      }
      if (done) break;
    }
    if (keyboard_input(cmd, sizeof(cmd), "bash$") < 0) continue;
    video_clear(RGB15(0, 10, 10));
    if (term_starts(cmd, "echo ")) {
      char out[64];
      ps_expand(cmd + 5, out, sizeof(out));
      video_text(8, 8, out, RGB15(31, 31, 31));
    } else if (term_eq(cmd, "ls") || term_eq(cmd, "ls -l")) {
      char names[16][32];
      int n, i;
      video_text(8, 8, "CART FILES:", RGB15(31, 31, 31));
      n = fs_list(names, 16);
      for (i = 0; i < n && i < 8; i++) video_text(8, 24 + i * 8, names[i], RGB15(25, 25, 25));
    } else if (term_starts(cmd, "cat ")) {
      const char *name = cmd + 4;
      u8 buf[256];
      int n;
      while (*name == ' ') name++;
      n = fs_read(name, buf, sizeof(buf) - 1);
      if (n < 0) video_text(8, 8, "FILE NOT FOUND.", RGB15(31, 0, 0));
      else { buf[n] = 0; video_text(8, 8, (char*)buf, RGB15(31, 31, 31)); }
    } else if (term_starts(cmd, "./")) {
      const char *name = cmd + 2;
      int rc = gep_run_file(name);
      video_clear(RGB15(0, 10, 10));
      if (rc < 0) video_text(8, 8, "PROGRAM NOT FOUND.", RGB15(31, 0, 0));
      else video_text(8, 8, "PROGRAM EXITED.", RGB15(31, 31, 31));
    } else if (term_eq(cmd, "clear")) {
      video_clear(RGB15(0, 10, 10));
      video_text(8, 8, "CLEARED.", RGB15(31, 31, 31));
    } else if (term_eq(cmd, "help")) {
      video_text(8, 8, "ECHO LS CAT ./PROG CLEAR", RGB15(31, 31, 31));
      video_text(8, 24, "PWD EXIT $VAR=VALUE", RGB15(25, 25, 25));
    } else if (term_eq(cmd, "pwd")) {
      video_text(8, 8, "/", RGB15(31, 31, 31));
      video_text(8, 24, "FLAT CART FS, NO DIRS.", RGB15(25, 25, 25));
    } else if (term_eq(cmd, "exit")) {
      done = 1;
    } else if (cmd[0] && term_starts(cmd, "$") == 0 && cmd[0] != '$') {
      /* var=value, using ps_vars for simplicity, same store */
      char name[32];
      int i = 0, j = 0;
      while (cmd[i] && cmd[i] != '=' && j < 31) { name[j++] = cmd[i++]; }
      name[j] = 0;
      if (cmd[i] == '=') {
        ps_set_var(name, cmd + i + 1);
        video_text(8, 8, "VARIABLE SET.", RGB15(31, 31, 31));
      } else {
        video_text(8, 8, "UNKNOWN COMMAND.", RGB15(31, 31, 31));
        video_text(8, 24, cmd, RGB15(31, 31, 0));
      }
    } else {
      video_text(8, 8, "UNKNOWN COMMAND.", RGB15(31, 31, 31));
      video_text(8, 24, cmd, RGB15(31, 31, 0));
    }
    if (!done) {
      video_text(8, 120, "PRESS B TO CONTINUE.", RGB15(31, 31, 0));
      wait_b();
    }
  }
}
