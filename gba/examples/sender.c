/* SENDER.GEP — writes a message to the patch path, for
   another program to read. Run it, then run RECEIVER.GEP,
   or run DEMO.PATCH which runs both in order. */
#include "gep.h"

int gep_main(gep_api_t *api) {
  api->print("SENDER: SENDING TO MSG");
  api->print_newline();
  if (api->pipe_write("MSG", "HELLO FROM SENDER", 17) == 0)
    api->print("SENDER: SENT");
  else
    api->print("SENDER: SEND FAILED");
  api->print_newline();
  return 0;
}
