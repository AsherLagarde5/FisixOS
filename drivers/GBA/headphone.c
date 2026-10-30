/* FisixGBA headphone driver — GBA ONLY, not in the PC tree.
   Honest limit, stated so nobody claims more than the hardware
   does: the GBA headphone jack is a mechanical switch. Plugging
   headphones in disconnects the internal speaker in hardware.
   There is NO software register that reports "headphones are
   plugged in", and no separate headphone/speaker routing
   register. So this driver does the only software job there
   is: enable the same PSG audio output that goes to the
   headphones when they are plugged in, and to the speaker
   when they are not. It does NOT claim to detect the jack. */
#include "gba.h"

void headphone_init(void) {
  audio_init();
}

void headphone_beep(u32 freq_hz, u32 ms) {
  audio_beep(freq_hz, ms);
}
