#include "generic_cpu.h"
#include "../cpu.h"
#include "apic.h"
int generic_cpu_detect(generic_cpu_t *out){
    cpu_info_t c; apic_info_t a;
    if(!out) return 0;
    out->vendor=0; out->family=0; out->model=0; out->stepping=0; out->sse=0; out->sse2=0; out->apic=0; out->brand[0]=0;
    if(!cpu_get_info(&c)) return 1; /* unknown CPU: still boot, log only */
    out->vendor=(int)c.vendor; out->family=c.family; out->model=c.model; out->stepping=c.stepping;
    out->sse=cpu_has_sse(); out->sse2=cpu_has_sse2();
    { int i; for(i=0;i<48 && c.brand[i];i++) out->brand[i]=c.brand[i]; out->brand[48]=0; }
    out->apic=apic_detect(&a) ? 1 : 0;
    return 1;
}
