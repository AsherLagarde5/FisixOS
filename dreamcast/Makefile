# FisixDreamcast KOS Makefile — Dreamcast ONLY, NOT C64/GBA.
# Requires KOS_BASE, KOS_CC_BASE, etc. from environ.sh
# (see environ.sh.sample in KOS repo, adapted to workspace paths).
# Build: source environ.sh && make
# Output: fisixdreamcast.elf
# .cdi: mkdcdisc -e fisixdreamcast.elf -o fisixdreamcast.cdi (if mkdcdisc available)
# Menus: TEXT menu in main.c is fallback, 2D GUI in gui.c is
# additional, NOT 3D. JPEG via kos-ports libjpeg/stb_image
# is a next step, NOT linked here until kos-ports is built.
TARGET = fisixdreamcast.elf
OBJS = src/main.o src/vmu.o src/gui.o src/media.o src/accessories.o src/kos_init.o
all: rm-elf $(TARGET)
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f $(OBJS)
rm-elf:
	-rm -f $(TARGET)
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
