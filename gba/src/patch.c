/* Patch — batch files for ReTerm, v1.0.
   A .patch file is NOT a separate language anymore.
   It is exactly Terminal commands, one per line, run
   through the same terminal_exec() parser as typed
   input. A line in a patch and the same line typed in
   Terminal do exactly the same thing, because they are
   the same code path. See docs/PATCH.md.
 *
 * Commands are Terminal commands: HELP, ABOUT, EXIT,
 * RUN <gep>, LIST, PATCH <patch>, UPDATE <update>,
 * PRINT <text>, SEND <channel> <text>, RECV <channel>,
 * CLEAR <channel>, WAIT <ms>. # comments and blank
 * lines are ignored. EXIT stops the script and returns
 * to Terminal, it does not exit Terminal itself.
 * A .patch file is NOT an update file: it does not
 * replace system files, it only runs commands. An
 * update file run as a patch will be rejected by the
 * Terminal UPDATE/PATCH checks, not executed as code.
 */
#include "gba.h"

int patch_run_data(const unsigned char *data, unsigned int len) {
  unsigned int i = 0;
  char line[256];
  int line_len = 0;
  if (!data || len == 0) return -1;
  terminal_patch_begin();
  while (i <= len) {
    char c = (i < len) ? (char)data[i] : '\n';
    i++;
    if (c == '\n' || c == '\r') {
      if (line_len > 0) {
        line[line_len] = 0;
        {
          const char *s = line;
          while (*s == ' ' || *s == '\t') s++;
          if (*s && *s != '#') {
            if (terminal_exec(line, 1) == 1) break; /* EXIT: stop script */
          }
        }
        line_len = 0;
      }
    } else if (line_len < 255) {
      line[line_len++] = c;
    }
  }
  return 0;
}
static const char demo_patch[] =
  "# DEMO.PATCH — Patch demo, in Terminal commands\n"
  "PRINT PATCH DEMO\n"
  "RUN SENDER.GEP\n"
  "RUN RECEIVER.GEP\n"
  "PRINT DONE\n";
static const struct { const char *name; const char *data; } patch_embedded[] = {
  { "DEMO.PATCH", demo_patch },
};
int patch_list_embedded(char names[][32], int max) {
  int i, n = 0;
  for (i = 0; i < (int)(sizeof(patch_embedded) / sizeof(patch_embedded[0])) && n < max; i++) {
    int j;
    for (j = 0; patch_embedded[i].name[j] && j < 31; j++) names[n][j] = patch_embedded[i].name[j];
    names[n][j] = 0;
    n++;
  }
  return n;
}
static int patch_name_eq(const char *a, const char *b) {
  int i;
  for (i = 0; a[i] || b[i]; i++) {
    char ca = a[i], cb = b[i];
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return 0;
  }
  return 1;
}
int patch_run_file(const char *name) {
  static u8 buf[512];
  int n, i;
  if (!name) return -3;
  n = fs_read(name, buf, sizeof(buf));
  if (n >= 0) return patch_run_data(buf, (unsigned int)n);
  for (i = 0; i < (int)(sizeof(patch_embedded) / sizeof(patch_embedded[0])); i++)
    if (patch_name_eq(name, patch_embedded[i].name)) {
      int len = 0;
      while (patch_embedded[i].data[len]) len++;
      return patch_run_data((const unsigned char*)patch_embedded[i].data, (unsigned int)len);
    }
  return -3;
}
