#ifndef AMD_CPU_H
#define AMD_CPU_H

#include <stdint.h>

/* AMD CPU detection — real-hardware, freestanding, no Linux headers.
   STATUS (2026-10-30): DETECTION ONLY, UNTESTED ON REAL AMD HARDWARE.
   This file does not claim a working AMD system, does not enable SVM,
   does not program AMD-specific MSRs, and does not replace the Intel
   path for the Dell Latitude 5420 (i5-1145G7). It answers one question
   safely: "is this an AuthenticAMD CPU, which Zen family, and which
   features (TSC/SSE/AVX/SVM-present) does CPUID report?"
   SVM is reported, never enabled, until a tested AMD SVM path exists. */
typedef struct {
    int present;        /* 1 if CPUID vendor string is "AuthenticAMD" */
    uint32_t family;    /* decoded family, incl. extended family */
    uint32_t model;     /* decoded model, incl. extended model */
    uint32_t stepping;
    int svm_present;    /* CPUID 0x80000001 ECX bit 2 — present, NOT enabled */
    int tsc_present;    /* CPUID leaf 1 EDX bit 4 */
    int sse_present;    /* CPUID leaf 1 EDX bit 25 */
    int sse2_present;   /* CPUID leaf 1 EDX bit 26 */
    int avx_present;    /* CPUID leaf 1 ECX bit 28 — checked, never assumed */
    int avx2_present;   /* CPUID leaf 7 EBX bit 5, only if leaf 7 exists */
    char brand[49];
} amd_cpu_info_t;

int amd_cpu_detect(amd_cpu_info_t *out);
int amd_cpu_is_present(void);
/* Family/model -> generation string. Model mapping is a documented
   heuristic (see amd_cpu.c), not a datasheet guarantee for every SKU. */
const char *amd_generation_name(uint32_t family, uint32_t model);

#endif
