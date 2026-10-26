# FisixOS ARM64 port skeleton -- SEPARATE from the x86_64 build.
# This Makefile does not touch ../Makefile, ../kernel.c, or any
# x86 kernel file. It builds a bare -kernel binary for
# qemu-system-aarch64 -machine virt only. No EFI loader port,
# no GIC interrupts, no device drivers beyond PL011 UART.
CC := aarch64-linux-gnu-gcc
OBJCOPY := aarch64-linux-gnu-objcopy
CFLAGS = -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -mgeneral-regs-only
SRCS = entry_arm.S arm_kernel.c uart_pl011.c cpu_arm.c gic.c
OBJS = $(SRCS:.c=.o)
OBJS := $(OBJS:.S=.o)

all: kernel_arm.bin

kernel_arm.elf: $(OBJS) kernel_arm.ld
	$(CC) -nostdlib -no-pie -Wl,-T,kernel_arm.ld -o $@ $(OBJS)

kernel_arm.bin: kernel_arm.elf
	$(OBJCOPY) -O binary $< $@
	ls -l kernel_arm.elf kernel_arm.bin

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) kernel_arm.elf kernel_arm.bin

.PHONY: all clean
