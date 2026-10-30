#include "generic_audio.h"
#include "generic.h"
int generic_audio_detect(generic_audio_t *out){
    if(!out) return 0;
    out->hda_found=pci_find_device(0x04,0x03,0x00,&out->pci) ? 1 : 0;
    out->speaker=1; /* PIT + port 0x61, present on almost every PC */
    return 1;
}
void generic_audio_beep(uint32_t f, uint32_t ms){ generic_speaker_beep(f,ms); }
