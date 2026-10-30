/* Patch path: named pipes so different programs can interact.
   Not a filesystem path, not a cart file — a volatile channel
   in OS RAM. A program writes a message to a named channel,
   another program (run later, by a .patch script or by hand)
   reads it. Reading consumes the message, like a pipe, so a
   second read gets -1 until something writes again.
   Channels do NOT survive a reboot, and do NOT go on the cart.
   That is intentional: this is for programs talking to each
   other in one session, not for saving files. */
#include "gba.h"

#define PIPE_CHANNELS 4
#define PIPE_NAME_LEN 16
#define PIPE_DATA_LEN 256

struct pipe_chan { char name[PIPE_NAME_LEN]; u8 data[PIPE_DATA_LEN]; u32 len; int used; };
static struct pipe_chan pipes[PIPE_CHANNELS];

static int pipe_name_eq(const char *a, const char *b) {
  int i;
  for (i = 0; a[i] || b[i]; i++) {
    char ca = a[i], cb = b[i];
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return 0;
  }
  return 1;
}
static void pipe_name_copy(char *d, const char *s) {
  int i;
  for (i = 0; s[i] && i < PIPE_NAME_LEN - 1; i++) d[i] = s[i];
  d[i] = 0;
}
int pipe_write(const char *channel, const u8 *data, u32 len) {
  int i, slot = -1;
  if (!channel || !data || len == 0) return -1;
  if (len > PIPE_DATA_LEN) len = PIPE_DATA_LEN;
  for (i = 0; i < PIPE_CHANNELS; i++)
    if (pipes[i].used && pipe_name_eq(channel, pipes[i].name)) { slot = i; break; }
  if (slot < 0)
    for (i = 0; i < PIPE_CHANNELS; i++)
      if (!pipes[i].used) { slot = i; break; }
  if (slot < 0) return -1;
  pipe_name_copy(pipes[slot].name, channel);
  for (i = 0; (u32)i < len; i++) pipes[slot].data[i] = data[i];
  pipes[slot].len = len;
  pipes[slot].used = 1;
  return 0;
}
int pipe_read(const char *channel, u8 *out, u32 maxlen) {
  int i, j;
  u32 n;
  if (!channel || !out || maxlen == 0) return -1;
  for (i = 0; i < PIPE_CHANNELS; i++) {
    if (!pipes[i].used || !pipe_name_eq(channel, pipes[i].name)) continue;
    n = pipes[i].len < maxlen ? pipes[i].len : maxlen;
    for (j = 0; (u32)j < n; j++) out[j] = pipes[i].data[j];
    /* Consume: a pipe message is read once. */
    pipes[i].len = 0;
    pipes[i].used = 0;
    pipes[i].name[0] = 0;
    return (int)n;
  }
  return -1;
}
int pipe_clear(const char *channel) {
  int i;
  if (!channel) return -1;
  for (i = 0; i < PIPE_CHANNELS; i++)
    if (pipes[i].used && pipe_name_eq(channel, pipes[i].name)) {
      pipes[i].len = 0; pipes[i].used = 0; pipes[i].name[0] = 0;
      return 0;
    }
  return -1;
}
