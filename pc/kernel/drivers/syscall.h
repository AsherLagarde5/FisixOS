#ifndef FISIX_SYSCALL_H
#define FISIX_SYSCALL_H
#include <stdint.h>
#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_OPEN 3
#define SYS_CLOSE 4
#define SYS_SBRK 5
#define SYS_GETPID 6
#define SYS_YIELD 7
void syscall_init(void);
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int syscall_selftest(void);
#endif
