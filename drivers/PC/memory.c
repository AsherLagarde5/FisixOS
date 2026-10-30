#include "memory.h"
#include <stdint.h>

static uint64_t total_memory_bytes = 0;

void memory_set_total_bytes(uint64_t bytes)
{
    total_memory_bytes = bytes;
}

void memory_init(void)
{
    /* The EFI loader has already captured the final memory map and exited
       firmware services.  Kernel code must never call gBS after that point. */
}

uint64_t memory_total_bytes(void)
{
    return total_memory_bytes;
}
