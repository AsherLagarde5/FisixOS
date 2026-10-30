#include "gic.h"
#include "uart_pl011.h"
void gic_init_stub(void) {
    /* Honest stub: this bare -kernel test has no ACPI/DT parsing,
     * so GIC distributor/redistributor addresses are not discovered
     * and no interrupt is wired or claimed. */
    uart_pl011_puts("GIC: stub, interrupts not yet wired\n");
}
