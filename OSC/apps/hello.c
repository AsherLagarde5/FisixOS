/* hello.fse - sample FiSix Executable, built with GCC.
 * Build: see tools/fisix-gcc hello.c -o hello.fse
 * No globals, no libc, only the Fisix API table. */
#include "../boot/fse.h"

int fse_main(fisix_api_t *api)
{
    api->print("Hello from FisixOS, from a .fse program!");
    api->print_newline();
    api->print("42 + 1 = ");
    api->print_u64(43);
    api->print_newline();
    return 0;
}
