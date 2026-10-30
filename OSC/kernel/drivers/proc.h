#ifndef FISIX_PROC_H
#define FISIX_PROC_H
#include <stdint.h>
/* Load and run a .fse program in the kernel (ring 0, same as the
   loader terminal runs .fse today, but after ExitBootServices).
   Returns the program's return value, or -1 on a bad header/size.
   This is not yet a ring-3 process: no separate address space,
   no user stack, and no preemption. That is the next stage. */
int proc_run_fse(const uint8_t *file, uint32_t file_len);
int proc_run_fse_at(const uint8_t *file, uint32_t file_len, uint64_t load_addr);
void proc_set_gui_print(void (*cb)(const char *s));
void proc_set_gui_read_key(int (*cb)(void));
#endif
