# FisixGBA build — 32-bit ARM7TDMI, standard 32 MB cart max
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS = -mthumb-interwork -mcpu=arm7tdmi -O2 -Wall -Wextra -ffreestanding -nostdlib
LDFLAGS = -T linker.ld -nostartfiles -Wl,-Map=fisixgba.map
# libgcc must come AFTER the objects: it provides __aeabi_uidiv for
# the PSG frequency division in audio.c, and link order matters.
SRCS = src/crt0.s src/main.c src/video.c src/input.c src/timer.c src/cart.c src/link.c src/apps.c src/kernel.c src/audio.c src/headphone.c src/gep.c src/pipe.c src/patch.c src/update.c src/dosemu.c
OBJS = $(SRCS:.c=.o)
OBJS := $(OBJS:.s=.o)

all: fisixgba.gba

# GEP examples, embedded in the ROM so Terminal can RUN them
# without a cart file transfer first. <name>.gep -> src/<name>_gep.h.
examples/hello.elf: examples/hello.c src/gep.h tools/gep.ld
	$(CC) -mcpu=arm7tdmi -O2 -Wall -Wextra -ffreestanding -nostdlib -I src -Wl,-T,tools/gep.ld -nostartfiles -o $@ examples/hello.c -lgcc
examples/sender.elf: examples/sender.c src/gep.h tools/gep.ld
	$(CC) -mcpu=arm7tdmi -O2 -Wall -Wextra -ffreestanding -nostdlib -I src -Wl,-T,tools/gep.ld -nostartfiles -o $@ examples/sender.c -lgcc
examples/receiver.elf: examples/receiver.c src/gep.h tools/gep.ld
	$(CC) -mcpu=arm7tdmi -O2 -Wall -Wextra -ffreestanding -nostdlib -I src -Wl,-T,tools/gep.ld -nostartfiles -o $@ examples/receiver.c -lgcc
examples/%.gep: examples/%.elf tools/make_gep.py
	python3 tools/make_gep.py $< $@
src/hello_gep.h: examples/hello.gep
	python3 -c "import sys; d=open('examples/hello.gep','rb').read(); print('/* Generated from examples/hello.gep, do not edit by hand. */'); print('static const unsigned char hello_gep_data[] = {' + ','.join(str(b) for b in d) + '};'); print(f'static const unsigned int hello_gep_len = {len(d)};')" > $@
src/sender_gep.h: examples/sender.gep
	python3 -c "import sys; d=open('examples/sender.gep','rb').read(); print('/* Generated from examples/sender.gep, do not edit by hand. */'); print('static const unsigned char sender_gep_data[] = {' + ','.join(str(b) for b in d) + '};'); print(f'static const unsigned int sender_gep_len = {len(d)};')" > $@
src/receiver_gep.h: examples/receiver.gep
	python3 -c "import sys; d=open('examples/receiver.gep','rb').read(); print('/* Generated from examples/receiver.gep, do not edit by hand. */'); print('static const unsigned char receiver_gep_data[] = {' + ','.join(str(b) for b in d) + '};'); print(f'static const unsigned int receiver_gep_len = {len(d)};')" > $@
src/gep.o: src/hello_gep.h src/sender_gep.h src/receiver_gep.h

# User Update Files, embedded so Terminal can UPDATE them without
# a cart transfer first. demo.update is GBA, wrong.update is UEFI.
examples/update/demo.update: examples/update/demo2.patch examples/update/note.txt tools/make_update.py
	python3 tools/make_update.py GBA $@ ADD:DEMO2.PATCH:examples/update/demo2.patch ADD:NOTE.TXT:examples/update/note.txt
examples/update/wrong.update: examples/update/note.txt tools/make_update.py
	python3 tools/make_update.py UEFI $@ ADD:NOTE.TXT:examples/update/note.txt
src/demo_update.h: examples/update/demo.update
	python3 -c "import sys; d=open('examples/update/demo.update','rb').read(); print('/* Generated from examples/update/demo.update, do not edit by hand. */'); print('static const unsigned char demo_update_data[] = {' + ','.join(str(b) for b in d) + '};'); print(f'static const unsigned int demo_update_len = {len(d)};')" > $@
src/wrong_update.h: examples/update/wrong.update
	python3 -c "import sys; d=open('examples/update/wrong.update','rb').read(); print('/* Generated from examples/update/wrong.update, do not edit by hand. */'); print('static const unsigned char wrong_update_data[] = {' + ','.join(str(b) for b in d) + '};'); print(f'static const unsigned int wrong_update_len = {len(d)};')" > $@
src/update.o: src/demo_update.h src/wrong_update.h
src/hello_com.h: examples/hello.com
	python3 -c "import sys; d=open('examples/hello.com','rb').read(); print('/* Generated from examples/hello.com, do not edit by hand. */'); print('static const unsigned char hello_com_data[] = {' + ','.join(str(b) for b in d) + '};'); print(f'static const unsigned int hello_com_len = {len(d)};')" > $@
src/dosemu.o: src/hello_com.h

# Explicit ARM assembly rule: without this, make's builtin .s rule
# uses the host 'as' (x86) and fails on ARM instructions.
src/%.o: src/%.s
	$(CC) -mcpu=arm7tdmi -c $< -o $@

fisixgba.gba: fisixgba.elf
	$(OBJCOPY) -O binary $< fisixgba.bin
	python3 fixup.py fisixgba.bin $@

fisixgba.elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

clean:
	rm -f src/*.o fisixgba.elf fisixgba.bin fisixgba.gba fisixgba.map examples/hello.elf examples/hello.gep src/hello_gep.h examples/sender.elf examples/sender.gep src/sender_gep.h examples/receiver.elf examples/receiver.gep src/receiver_gep.h examples/update/demo.update examples/update/wrong.update src/demo_update.h src/wrong_update.h src/hello_com.h
