#ifndef APIC_H
#define APIC_H
#include <stdint.h>
/* Local APIC driver, step 1: base + version read.
   Tiger Lake needs the Local APIC for timers and interrupts; the old
   8259 PIC is disabled in the kernel already. Full APIC routing (IOAPIC
   redirection, LAPIC timer calibration, EOI handling) is not claimed
   here. This reads IA32_APIC_BASE (MSR 0x1B) and the LAPIC version
   register, so interrupt setup knows where the APIC lives. */
typedef struct { uint64_t base; int enabled; uint32_t version; } apic_info_t;
int apic_detect(apic_info_t *out);
/* Real LAPIC timer, polling only: programs divide-by-16 and a large
   initial count with the LVT timer masked, so it counts down without
   delivering an interrupt. IOAPIC/LVT unmasking + interrupt delivery
   is TODO, so do not claim a working APIC timer interrupt yet. */
int apic_timer_init(void);
uint32_t apic_timer_current(void);
#endif
