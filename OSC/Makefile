CC        ?= x86_64-linux-gnu-gcc
LD        ?= ld.lld
OBJCOPY   ?= objcopy
QEMU      ?= qemu-system-x86_64

OVMF_CODE ?= $(firstword $(wildcard /usr/share/OVMF/OVMF_CODE.fd /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/edk2/x64/OVMF_CODE.4m.fd))
OVMF_VARS_TEMPLATE ?= $(firstword $(wildcard /usr/share/OVMF/OVMF_VARS.fd /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/edk2/x64/OVMF_VARS.4m.fd))

CFLAGS    = -ffreestanding -O2 -Wall -Wextra -m64 -mno-red-zone \
            -fno-stack-protector -fpic -fshort-wchar \
            -Iboot -Ikernel -Ikernel/drivers -Ikernel/drivers/system \
            -I/usr/include/efi -I/usr/include/efi/x86_64

# === Sources ===
KERNEL_C_SRCS   := $(shell find kernel -name '*.c' 2>/dev/null)
KERNEL_ASM_SRCS := $(shell find kernel -name '*.S' 2>/dev/null)
KERNEL_OBJS     := $(KERNEL_C_SRCS:.c=.o) $(KERNEL_ASM_SRCS:.S=.o)
KERNEL_RUNTIME_OBJS := $(filter-out kernel/drivers/eosfs.o kernel/drivers/accounts.o,$(KERNEL_OBJS))
KERNEL_ELF := kernel/kernel.elf
KERNEL_BIN := kernel.bin

EFI_SRC  = boot/efi_main.c
EFI_OBJ  = boot/efi_main.o
EFI_ELF  = EFI/BOOT/BOOTX64.elf
EFI_BIN  = EFI/BOOT/BOOTX64.EFI
LOADER_OBJS = $(EFI_OBJ) kernel/drivers/eosfs.o kernel/drivers/accounts.o

ISO_DIR  = iso
ISO_IMG  = FisixOS.iso
ESP_IMG  = esp.img

.PHONY: all clean iso run run-iso fresh-vars

all: $(EFI_BIN)

kernel: $(KERNEL_BIN)

$(KERNEL_BIN): $(KERNEL_RUNTIME_OBJS) kernel/kernel.ld
	$(CC) -nostdlib -no-pie -Wl,-T,kernel/kernel.ld -o $(KERNEL_ELF) kernel/entry.o $(filter-out kernel/entry.o,$(KERNEL_RUNTIME_OBJS))
	$(OBJCOPY) -O binary $(KERNEL_ELF) $(KERNEL_BIN)
	rm -f $(KERNEL_ELF)

# === Build the EFI application ===
$(EFI_BIN): $(LOADER_OBJS)
	mkdir -p EFI/BOOT
	$(CC) -nostdlib -Wl,-T,efi.ld -o $(EFI_ELF) $(LOADER_OBJS) \
		-Wl,-shared -Wl,-Bsymbolic -Wl,-znocombreloc
	$(OBJCOPY) -I elf64-x86-64 -O efi-app-x86_64 --image-base 0x100000 $(EFI_ELF) $(EFI_BIN) || \
	$(OBJCOPY) --target=efi-app-x86_64 --image-base 0x100000 $(EFI_ELF) $(EFI_BIN)
	rm -f $(EFI_ELF)

$(EFI_OBJ): $(EFI_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c -x assembler-with-cpp $< -o $@

# === NVRAM Management (for QEMU testing) ===
fresh-vars:
	@if [ -f "$(OVMF_VARS_TEMPLATE)" ]; then \
		cp "$(OVMF_VARS_TEMPLATE)" OVMF_VARS.fd; \
	else \
		dd if=/dev/zero of=OVMF_VARS.fd bs=1M count=64 status=none; \
	fi

# === Create a proper ESP image ===
$(ESP_IMG): $(EFI_BIN) $(KERNEL_BIN)
	dd if=/dev/zero of=$(ESP_IMG) bs=1M count=32 status=none
	mkfs.fat -F 32 $(ESP_IMG)
	mmd -i $(ESP_IMG) ::/EFI
	mmd -i $(ESP_IMG) ::/EFI/BOOT
	mcopy -i $(ESP_IMG) $(EFI_BIN) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $(ESP_IMG) $(KERNEL_BIN) ::/kernel.bin
	echo "\EFI\BOOT\BOOTX64.EFI" > startup.nsh
	mcopy -i $(ESP_IMG) startup.nsh ::/startup.nsh
	rm -f startup.nsh

# === Create a pure UEFI-bootable ISO (VirtualBox Fixed) ===
iso: $(ESP_IMG)
	mkdir -p $(ISO_DIR)/EFI/BOOT
	cp $(EFI_BIN) $(ISO_DIR)/EFI/BOOT/BOOTX64.EFI
	cp $(KERNEL_BIN) $(ISO_DIR)/kernel.bin
	dd if=/dev/zero of=$(ISO_DIR)/esp.img bs=1M count=2 status=none
	mkfs.fat -F 12 $(ISO_DIR)/esp.img
	mmd -i $(ISO_DIR)/esp.img ::/EFI
	mmd -i $(ISO_DIR)/esp.img ::/EFI/BOOT
	mcopy -i $(ISO_DIR)/esp.img $(EFI_BIN) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $(ISO_DIR)/esp.img $(KERNEL_BIN) ::/kernel.bin
	echo "\EFI\BOOT\BOOTX64.EFI" > $(ISO_DIR)/startup.nsh
	xorriso -as mkisofs \
		-V "FISIXOS" \
		-R -J \
		-e esp.img \
		-no-emul-boot \
		-o $(ISO_IMG) \
		$(ISO_DIR)


# === Run in QEMU (directory mode) ===
run: $(EFI_BIN) $(KERNEL_BIN) fresh-vars
	cp $(KERNEL_BIN) EFI/kernel.bin
	$(QEMU) -m 512 \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=OVMF_VARS.fd \
		-drive format=raw,file=fat:rw:EFI \
		-net none

# === Run in QEMU (ISO mode) ===
run-iso: iso fresh-vars
	$(QEMU) -m 512 \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=OVMF_VARS.fd \
		-cdrom $(ISO_IMG) \
		-boot d \
		-net none

clean:
	rm -rf EFI $(ISO_DIR) $(ISO_IMG) $(ESP_IMG) OVMF_VARS.fd \
	       $(KERNEL_OBJS) $(EFI_OBJ) $(EFI_ELF) $(KERNEL_ELF) $(KERNEL_BIN)

death:
	@echo "You have been terminatered."
	@while true; do \
		randstr=$$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c 12); \
		echo "EVILTEXT" > "MWAHAHA_$${randstr}.txt"; \
	done