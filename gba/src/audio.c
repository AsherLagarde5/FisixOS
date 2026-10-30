/* FisixGBA audio: GBA PSG square-wave channel 1.
   Real hardware, not a stub: SOUND1CNT registers, 131072 Hz
   base, 50% duty, fixed volume. No WAV/DMA music here — that
   would need sample data and a mixer, and v0.5 only needs
   beeps for boot and for Pong hits.
   Registers (public GBA register map):
     SOUNDCNT_L 0x04000080, SOUNDCNT_X 0x04000084,
     SOUND1CNT_L 0x04000060, SOUND1CNT_H 0x04000062,
     SOUND1CNT_X 0x04000064. */
#include "gba.h"

#define REG_SOUNDCNT_L (*(vu16*)0x04000080)
#define REG_SOUNDCNT_X (*(vu16*)0x04000084)
#define REG_SOUND1CNT_L (*(vu16*)0x04000060)
#define REG_SOUND1CNT_H (*(vu16*)0x04000062)
#define REG_SOUND1CNT_X (*(vu16*)0x04000064)

void audio_init(void) {
  REG_SOUNDCNT_X = 0x80; /* sound on */
  /* Full volume both sides, channels 1-4 enabled both sides. */
  REG_SOUNDCNT_L = 0xFF77;
  REG_SOUND1CNT_L = 0; /* no sweep */
  REG_SOUND1CNT_H = 0;
  REG_SOUND1CNT_X = 0;
}

void audio_beep(u32 freq_hz, u32 ms) {
  u32 rate, start, ticks;
  if (freq_hz == 0 || freq_hz > 8000) return;
  /* rate = 2048 - 131072/freq, clamped to 0..2047. */
  rate = 2048 - (131072u / freq_hz);
  if (rate > 2047) rate = 2047;
  /* Duty 50% (2<<6), initial volume 12 (12<<8), no envelope step. */
  REG_SOUND1CNT_H = (u16)((2 << 6) | (12 << 8));
  /* Trigger, length disabled so it plays until we stop it. */
  REG_SOUND1CNT_X = (u16)(rate | (1 << 15));
  ticks = ms * 16; /* timer is ~16384 Hz, so ~16 ticks per ms */
  start = timer_ticks();
  while ((timer_ticks() - start) < ticks) {
    /* busy wait on the timer, not vblank, so short beeps stay short */
  }
  REG_SOUND1CNT_X = 0;
  REG_SOUND1CNT_H = 0;
}
