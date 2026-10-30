# FisixOS unified top-level Makefile — PC + GBA + Dreamcast ONLY
# Usage: make iso | make gba | make dc | make all | make clean | make help
# This Makefile delegates to the existing per-device Makefiles; it does
# NOT rebuild the Dreamcast KOS toolchain (a ~4.5 h job on first build).
# It assumes the toolchain at ~/workspace/toolchain-dreamcast/ already exists.
#
# Paths are workspace-relative to the parent agent's home layout.
# Override with: make iso PC_DIR=/path/to/OSC  etc.

PC_DIR  ?= $(HOME)/workspace/fisixos/OSC
GBA_DIR ?= $(HOME)/workspace/fisixgba
DC_DIR  ?= $(HOME)/workspace/fisixdreamcast
MKDCDISC ?= $(HOME)/workspace/toolchain-dreamcast/mkdcdisc/builddir/mkdcdisc

.PHONY: all iso gba dc clean death help

all: iso gba dc

# PC ISO — delegates to the real target `iso` in fisixos/OSC/Makefile
# (iso: esp.img -> FisixOS.iso, UEFI BOOTX64.EFI + kernel.bin).
# Existing output: $(PC_DIR)/FisixOS.iso (4.6 MB, 2026-10-26).
iso:
	$(MAKE) -C $(PC_DIR) iso

# GBA ROM — delegates to `all: fisixgba.gba` in fisixgba/Makefile
# (arm-none-eabi-gcc, ARM7TDMI, linker.ld, fixup.py).
# Existing output: $(GBA_DIR)/fisixgba.gba (256 KB, v1.3).
gba:
	$(MAKE) -C $(GBA_DIR) all

# Dreamcast — sources environ.sh (sets KOS_BASE, KOS_CC_BASE, PATH via
# environ_base.sh), then delegates to `all` in fisixdreamcast/Makefile,
# producing fisixdreamcast.elf. The .cdi step is separate and optional:
# if mkdcdisc is present it is run, otherwise a note is printed and the
# existing .cdi is left untouched. Do NOT expect a full toolchain rebuild.
# Existing outputs: $(DC_DIR)/fisixdreamcast.elf (1.7 MB),
#   ~/workspace/your_files/fisixdreamcast-v0.1.cdi (102 MB).
dc:
	. $(DC_DIR)/environ.sh && $(MAKE) -C $(DC_DIR) all
	@if [ -x "$(MKDCDISC)" ]; then \
		echo "mkdcdisc found: to make a .cdi, run:"; \
		echo "  $(MKDCDISC) -e $(DC_DIR)/fisixdreamcast.elf -o fisixdreamcast.cdi"; \
	else \
		echo "NOTE: mkdcdisc not found at $(MKDCDISC); .cdi step skipped."; \
		echo "Existing .cdi (if any) is in ~/workspace/your_files/fisixdreamcast-v0.1.cdi"; \
	fi

clean:
	$(MAKE) -C $(PC_DIR) clean || true
	$(MAKE) -C $(GBA_DIR) clean || true
	$(MAKE) -C $(DC_DIR) clean || true

# make death — KEPT PER DEREK'S INSTRUCTION, copied VERBATIM from
# ~/workspace/fisixos/OSC/Makefile:167 (the PC Makefile only; no death
# target was found in the GBA or Dreamcast Makefiles, or in their src/).
# WARNING: DO NOT RUN THIS CASUALLY. It loops forever, creating
# MWAHAHA_<12 random chars>.txt files containing "EVILTEXT" in the
# current directory, until interrupted (Ctrl-C). It is a joke/prank
# target, not a build, clean, or shutdown step. Included here only
# because Derek said to keep it, exactly as written in the original.
death:
	@echo "You have been terminatered."
	@while true; do \
		randstr=$$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c 12); \
		echo "EVILTEXT" > "MWAHAHA_$${randstr}.txt"; \
	done

help:
	@echo "FisixOS — PC + GBA + Dreamcast only"
	@echo "  make iso   - Build PC UEFI ISO (fisixos/OSC/Makefile: iso)"
	@echo "  make gba   - Build GBA ROM fisixgba.gba (fisixgba/Makefile: all)"
	@echo "  make dc    - Build Dreamcast ELF via KOS environ.sh (fisixdreamcast/Makefile: all)"
	@echo "               .cdi via mkdcdisc is noted, not forced, in the dc recipe"
	@echo "  make all   - iso + gba + dc"
	@echo "  make clean - Clean all three trees"
	@echo "  make death - JOKE/PRANK ONLY, loops forever making MWAHAHA_*.txt (see Makefile)"
	@echo "  make help  - This help"
	@echo "Drivers are organised by device under drivers/PC/, drivers/GBA/, drivers/Dreamcast/"
	@echo "AMD drivers (detection only, untested on real AMD HW) are in drivers/PC/amd_*.c/.h"
	@echo "NOT included: C64/TI-89/R36S/Presario deleted 2026-10-30 (trash, exp 2026-11-29, port later); Apple IIe kept locally; M1 cancelled"
	@echo "NOT pushed to GitHub yet: destination repo still to be confirmed by Derek"
