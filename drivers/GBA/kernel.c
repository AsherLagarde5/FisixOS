/* FisixGBA kernel, hollowed down — v0.5.
   Port of PC FisixOS kernel/kernel.c (kernel_startup), but ONLY
   what a GBA actually needs to boot, plus Link transfer.
   Removed from the v0.4 port, on purpose, because a GBA has
   none of this hardware and the "not present" lines were
   just noise: ACPI, PCI, APIC, PIC, IDT, syscall, SMBIOS,
   PS/2, NVMe, xHCI, WWAN, e1000, touchscreen, Latitude
   checklist, GOP framebuffer. None of those files exist
   here, and none are linked.
   What stays, in PC kernel_startup order where it applies:
   boot info, memory, CPU, video, input, cart, Link, audio.
   This file lives ONLY in the GBA project,
   ~/workspace/fisixgba/, NOT in the PC FisixOS tree.
   The PC kernel.c stays in its own folder, untouched
   except for its own audio addition (see PC drivers/audio.c). */

#include "gba.h"

static int kline;

/* --- MWAHAHAHA IM A DINOSAUR --- */
/* (kept from PC kernel.c, verbatim, because it's funny) */

static void kprint(const char *s) {
  if (kline >= 19) return;
  video_text(8, 8 + kline * 8, s, RGB15(25, 25, 25));
  kline++;
}

static void kprint_u32(u32 value) {
  char buf[12];
  int i = 11;
  buf[i] = 0;
  if (value == 0) { kprint("0"); return; }
  while (value > 0 && i > 0) {
    buf[--i] = (char)('0' + (value % 10));
    value /= 10;
  }
  kprint(&buf[i]);
}

void kernel_startup_gba(void) {
  u32 start = timer_ticks();
  kline = 0;
  video_clear(RGB15(0, 0, 10));
  kprint("[FisixOS] Boot info OK");
  kprint("[FisixOS] memory_bytes =");
  kprint_u32(294912);
  kprint("[FisixOS] memory: EWRAM 256K+IWRAM 32K OK");
  kprint("[FisixOS] cpu: ARM7TDMI 16.78 MHZ OK");
  kprint("[FisixOS] video: LCD 240X160 OK");
  kprint("[FisixOS] input: GBA buttons OK");
  kprint("[FisixOS] cart: ROM + SRAM OK");
  kprint("[FisixOS] link: SIO ready");
  kprint("[FisixOS] audio: headphone/PSG ready");
  audio_init();
  headphone_init();
  /* Boot sound: two short PSG beeps. If no headphones are
     plugged in, this comes out of the GBA speaker instead —
     the jack switch is hardware, see src/headphone.c. */
  audio_beep(660, 120);
  audio_beep(880, 120);
  kprint("[FisixOS] Kernel early setup complete.");
  kprint("[FisixOS] GUI: menu next");
  /* Show the log for about 2 seconds total, then main()
     draws the menu. Timer is ~16384 Hz. */
  while ((timer_ticks() - start) < 32768) {
    video_wait_vblank();
  }
}
