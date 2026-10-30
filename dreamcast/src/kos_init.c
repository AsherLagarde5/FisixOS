/* KOS-only init file. Compiled ONLY in KOS build, NOT in host build.
   KOS_INIT_FLAGS must be at file scope. Default init is used
   if this file is omitted, but explicit INIT_DEFAULT is clearer.
   This file is NOT compiled on host, so <kos.h> is not needed there. */
#include <kos.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
