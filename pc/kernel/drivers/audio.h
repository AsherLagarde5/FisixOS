#ifndef FISIX_AUDIO_H
#define FISIX_AUDIO_H
#include <stdint.h>
/* FisixOS audio, PC version — VM-only addition 2026-10-29, NOT pushed
 * and NOT synced to Derek's PC yet.
 * Honest scope: this is the PC speaker (PIT + port 0x61), already
 * implemented as generic_speaker_beep() in generic.c. It is NOT
 * Realtek / Intel HDA / SST — that driver does not exist in this
 * tree, and this file does not claim it does. The GBA version's
 * audio is a different driver in a different project folder
 * (fisixgba/src/audio.c, PSG square wave), and the GBA headphone
 * driver (fisixgba/src/headphone.c) is GBA-only and is NOT here. */
void audio_init(void);
void audio_beep(uint32_t freq_hz, uint32_t approx_ms);
#endif
