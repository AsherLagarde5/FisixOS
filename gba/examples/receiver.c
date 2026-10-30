/* RECEIVER.GEP — reads a message from the patch path.
   If SENDER.GEP ran first in this session, it prints
   what SENDER sent. If not, it says no message. Reading
   consumes the message, so running RECEIVER twice in a
   row prints it once, then NO MESSAGE. */
#include "gep.h"

int gep_main(gep_api_t *api) {
  char buf[64];
  int n;
  api->print("RECEIVER: READING MSG");
  api->print_newline();
  n = api->pipe_read("MSG", buf, sizeof(buf) - 1);
  if (n < 0) {
    api->print("RECEIVER: NO MESSAGE");
  } else {
    buf[n] = 0;
    api->print("RECEIVER GOT: ");
    api->print(buf);
  }
  api->print_newline();
  return 0;
}
