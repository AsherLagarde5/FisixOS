#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

void memory_init(void);
/* Called by the EFI loader before ExitBootServices(). */
void memory_set_total_bytes(uint64_t bytes);
uint64_t memory_total_bytes(void);

#endif
