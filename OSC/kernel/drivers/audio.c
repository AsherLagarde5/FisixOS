#include "audio.h"
#include "generic.h"

/* --- MWAHAHAHA IM A DINOSAUR --- */
/* (kept from kernel.c, because it's funny — same joke, both versions) */

void audio_init(void) {
    /* PC speaker needs no init beyond the PIT setup that
       generic_speaker_beep() does on every beep. */
}

void audio_beep(uint32_t freq_hz, uint32_t approx_ms) {
    generic_speaker_beep(freq_hz, approx_ms);
}
