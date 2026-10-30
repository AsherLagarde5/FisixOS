#ifndef CPU_ARM_H
#define CPU_ARM_H
#include <stdint.h>
uint64_t cpu_arm_read_midr(void);
void cpu_arm_report(void);
#endif
