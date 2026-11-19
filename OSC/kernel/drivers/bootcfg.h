/* bootcfg.h - FisixOS boot-selection config file (BOOT.CFG).
 *
 * A tiny text file the bootloader reads at menu time:
 *   default=2    menu number to boot (0 = first Installed, as
 *                if Enter was pressed; 1..N = that menu entry)
 *   timeout=5    seconds to wait for a key before booting the
 *                default (0 = wait forever, no auto-boot)
 * Lines starting with '#' are comments; unknown keys are
 * ignored; a missing or broken file means "defaults".
 *
 * The file lives at \FISIXOS\BOOT.CFG on the boot ESP and is
 * written by the loader terminal (`boot set-default N`,
 * `boot timeout S`). It is deliberately NOT a firmware
 * BootOrder edit: FisixOS never rewrites the machine's UEFI
 * boot order, it only picks among the entries its own menu
 * already shows. Pure string logic, so the host test can
 * exercise parse/format round-trips exactly.
 */
#ifndef FISIX_BOOTCFG_H
#define FISIX_BOOTCFG_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    int default_choice; /* 0 = Enter behaviour (first Installed) */
    int timeout_secs;   /* 0 = wait forever */
} bootcfg_t;

void bootcfg_defaults(bootcfg_t *cfg);
/* Parse BOOT.CFG text (NUL-terminated). Returns 0; a broken
 * line is skipped, it never fails the whole file. */
int bootcfg_parse(const char *text, bootcfg_t *cfg);
/* Format a config into buf (NUL-terminated). Returns 0 or -1
 * when buf is too small. */
int bootcfg_format(const bootcfg_t *cfg, char *buf, size_t cap);

#endif
