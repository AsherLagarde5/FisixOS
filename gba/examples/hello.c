/* HELLO.GEP — first Game Boy Executable Program.
   Built with tools/gep.ld + tools/make_gep.py, run in
   FisixGBA Terminal with: RUN HELLO.GEP
   Uses only the GEP API, no hardware registers directly. */
#include "gep.h"

int gep_main(gep_api_t *api) {
  api->print("HELLO FROM GEP!");
  api->print_newline();
  api->print("GAME BOY EXECUTABLE PROGRAM");
  api->print_newline();
  api->print("THIS IS ARM CODE, NOT X86.");
  api->print_newline();
  api->print("NUMBER: ");
  api->print_u64(12345);
  api->print_newline();
  api->print("PRESS ANY BUTTON TO EXIT.");
  api->print_newline();
  api->read_key();
  return 0;
}
