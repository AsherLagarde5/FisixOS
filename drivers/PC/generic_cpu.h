#ifndef GENERIC_CPU_H
#define GENERIC_CPU_H
#include <stdint.h>
/* Generic CPU: any x86-64 vendor. Intel, AMD, or other (e.g. VIA/Zhaoxin
 * reporting a vendor string) all boot if CPUID gives family/model and
 * SSE/SSE2. No Tiger Lake / Zen-specific code is required to boot;
 * those are optimizations. APIC presence is reported, not required
 * at this stage (PIC fallback is still in the kernel). */
typedef struct { int vendor; uint32_t family, model, stepping; int sse, sse2, apic; char brand[49]; } generic_cpu_t;
int generic_cpu_detect(generic_cpu_t *out);
#endif
