/* linux_syscall.h - Linux x86_64 syscall dispatcher for FisixOS.
 *
 * A static Linux ELF uses the `syscall` instruction with Linux
 * numbers (see kernel/drivers/deb_linux_syscalls.h), not the
 * FisixOS int 0x80 ABI (kernel/drivers/syscall.c). This file is
 * the Linux-number side: the SYSCALL handler in ring3_entry.S
 * calls linux_syscall_dispatch() with (number, a1..a6), and the
 * dispatcher implements the call against the ring-3 process
 * state (user pages via ring3.c, fds + brk + mmap here).
 *
 * See docs/RING3.md for the implemented / stub / unimplemented
 * table. Errors are negative errno values, as Linux returns
 * them (-ENOSYS = -38 for anything not implemented yet).
 */
#ifndef FISIX_LINUX_SYSCALL_H
#define FISIX_LINUX_SYSCALL_H
#include <stdint.h>

/* Linux errno values used here. */
#define LINUX_EPERM 1
#define LINUX_ENOENT 2
#define LINUX_EBADF 9
#define LINUX_ENOMEM 12
#define LINUX_EFAULT 14
#define LINUX_EINVAL 22
#define LINUX_ENOTTY 25
#define LINUX_ENOSYS 38
#define LINUX_ENODEV 19

uint64_t linux_syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5,
                                uint64_t a6);

/* Process state. Called by ring3_run_elf() before entering the
 * user process; resets fds, brk, mmap table and the exit flag. */
void linux_syscall_reset(uint64_t brk_base, const char *prog_path);

/* Set by the dispatcher on exit(60)/exit_group(231) and by the
 * fault handler. The SYSCALL handler in ring3_entry.S reads
 * g_linux_exited to choose SYSRET vs the exit path. */
extern int g_linux_exited;
extern int g_linux_exit_code;
int linux_syscall_exit_code(void);
/* Record an exit (used by the fault handler for signal deaths:
 * 139 = SIGSEGV, 134 = SIGABRT). */
void linux_syscall_note_exit(int code);

/* Output hook for write(1)/write(2). Default is the serial
 * port. The ring-3 self-test installs a capturing hook; the
 * host test installs its own. NULL restores serial output. */
void linux_syscall_set_output_hook(void (*hook)(char c));

/* Number of the last unimplemented syscall seen (0 = none),
 * for diagnostics. */
uint64_t linux_syscall_last_unimplemented(void);

#endif
