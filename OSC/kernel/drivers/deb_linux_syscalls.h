/* deb_linux_syscalls.h - Linux x86_64 syscall numbers vs FisixOS.
 *
 * Adapted from Asher's Rust OS project kernel/src/syscall/mod.rs (Rust,
 * Asher) — the syscall number table only, rewritten here as C
 * constants with all profanity stripped. Asher's Rust OS project
 * implements these numbers with a real SYSCALL/SYSRET dispatcher
 * (LSTAR/STAR/FMASK MSRs, ring-3 jump); FisixOS instead has an
 * int 0x80 gate (kernel/drivers/syscall.c) with its OWN numbers
 * (SYS_EXIT 0, SYS_WRITE 1, SYS_READ 2, ... — see syscall.h).
 * The two ABIs are NOT the same, so this table is a mapping
 * reference for the .deb/ELF work, not a claim that a Linux
 * binary can call FisixOS syscalls directly. Where a Linux
 * number has a FisixOS equivalent, it is listed; where it does
 * not, the entry says so.
 *
 * Asher's Rust OS project dispatcher status (for reference only):
 *   implemented: read(0), write(1), open(2, dummy fd), close(3),
 *     mmap(9), munmap(11), brk(12), ioctl(16, stub), sched_yield(24),
 *     nanosleep(35), getpid(39), exit(60), uname(63), getcwd(79),
 *     chdir(80, stub), gettimeofday(96), sysinfo(99), getuid(102),
 *     getgid(104), geteuid(107), getegid(108), getppid(110),
 *     arch_prctl(158, SET_FS only), reboot(169), time(201),
 *     clock_gettime(228), exit_group(231)
 *   everything else: -ENOSYS (38)
 * FisixOS syscall.c today: exit, write, read (memfiles only),
 * open/close (memfiles only), sbrk (64 KiB heap), getpid, yield.
 * A static Linux ELF needs at least write/exit/brk/mmap/uname/
 * arch_prctl(SET_FS) before it can do anything useful — that is
 * the gap the ring-3 + SYSCALL work in docs/DEB.md has to close.
 */
#ifndef FISIX_DEB_LINUX_SYSCALLS_H
#define FISIX_DEB_LINUX_SYSCALLS_H

/* Linux x86_64 numbers (from Asher's Rust OS project syscall/mod.rs). */
#define LINUX_SYS_READ 0
#define LINUX_SYS_WRITE 1
#define LINUX_SYS_OPEN 2
#define LINUX_SYS_CLOSE 3
#define LINUX_SYS_FSTAT 5
#define LINUX_SYS_LSEEK 8
#define LINUX_SYS_MMAP 9
#define LINUX_SYS_MPROTECT 10
#define LINUX_SYS_MUNMAP 11
#define LINUX_SYS_BRK 12
#define LINUX_SYS_RT_SIGACTION 13
#define LINUX_SYS_RT_SIGPROCMASK 14
#define LINUX_SYS_IOCTL 16
#define LINUX_SYS_PREAD64 17
#define LINUX_SYS_WRITEV 20
#define LINUX_SYS_ACCESS 21
#define LINUX_SYS_SCHED_YIELD 24
#define LINUX_SYS_MADVISE 28
#define LINUX_SYS_NANOSLEEP 35
#define LINUX_SYS_GETPID 39
#define LINUX_SYS_EXIT 60
#define LINUX_SYS_UNAME 63
#define LINUX_SYS_GETCWD 79
#define LINUX_SYS_CHDIR 80
#define LINUX_SYS_GETTIMEOFDAY 96
#define LINUX_SYS_SYSINFO 99
#define LINUX_SYS_GETUID 102
#define LINUX_SYS_GETGID 104
#define LINUX_SYS_GETEUID 107
#define LINUX_SYS_GETEGID 108
#define LINUX_SYS_GETPPID 110
#define LINUX_SYS_ARCH_PRCTL 158
#define LINUX_SYS_REBOOT 169
#define LINUX_SYS_GETTID 186
#define LINUX_SYS_TIME 201
#define LINUX_SYS_FUTEX 202
#define LINUX_SYS_SET_TID_ADDRESS 218
#define LINUX_SYS_CLOCK_GETTIME 228
#define LINUX_SYS_EXIT_GROUP 231
#define LINUX_SYS_OPENAT 257
#define LINUX_SYS_NEWFSTATAT 262
#define LINUX_SYS_READLINKAT 267
#define LINUX_SYS_SET_ROBUST_LIST 273
#define LINUX_SYS_RSEQ 334
#define LINUX_SYS_PRLIMIT64 302
#define LINUX_SYS_GETRANDOM 318
#define LINUX_SYS_STATX 332

/* FisixOS equivalents in syscall.h (int 0x80 ABI), or -1 if none. */
#define FISIX_SYS_FOR_LINUX_WRITE 1   /* SYS_WRITE */
#define FISIX_SYS_FOR_LINUX_EXIT 0    /* SYS_EXIT */
#define FISIX_SYS_FOR_LINUX_READ 2    /* SYS_READ (memfiles only) */
#define FISIX_SYS_FOR_LINUX_OPEN 3    /* SYS_OPEN (memfiles only) */
#define FISIX_SYS_FOR_LINUX_CLOSE 4   /* SYS_CLOSE */
#define FISIX_SYS_FOR_LINUX_BRK 5     /* SYS_SBRK (64 KiB heap) */
#define FISIX_SYS_FOR_LINUX_GETPID 6  /* SYS_GETPID */
#define FISIX_SYS_FOR_LINUX_YIELD 7   /* SYS_YIELD */
#define FISIX_SYS_FOR_LINUX_MMAP (-1) /* no mmap yet */
#define FISIX_SYS_FOR_LINUX_UNAME (-1)
#define FISIX_SYS_FOR_LINUX_ARCH_PRCTL (-1)
#define FISIX_SYS_FOR_LINUX_CLOCK_GETTIME (-1)

#endif
