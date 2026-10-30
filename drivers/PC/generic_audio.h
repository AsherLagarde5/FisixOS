#ifndef GENERIC_AUDIO_H
#define GENERIC_AUDIO_H
#include <stdint.h>
#include "pci.h"
/* HD Audio class (04/03) detection + PC speaker beep. Full HDA codec
 * / Realtek / Intel SST driver is NOT claimed here: detection + beep
 * is enough to boot, audio is optional. Never fail boot for audio. */
typedef struct { int hda_found; pci_device_t pci; int speaker; } generic_audio_t;
int generic_audio_detect(generic_audio_t *out);
void generic_audio_beep(uint32_t freq_hz, uint32_t ms);
#endif
