/* FisixDreamcast accessories — Dreamcast ONLY.
   KOS Maple bus where possible. Status is honest:
   WORKING = KOS driver exists in repo examples / headers
   SCAFFOLD_UNTESTED = KOS Maple protocol scaffold, no
     hardware here to test, NOT claimed tested
   STUB_UNRELEASED = unreleased, no hardware to test,
     stub only, NOT claimed working
   Do NOT add to C64/GBA/TI-89. */
#ifndef FISIXDC_ACCESSORIES_H
#define FISIXDC_ACCESSORIES_H
typedef enum { ACC_WORKING, ACC_SCAFFOLD_UNTESTED, ACC_STUB_UNRELEASED } acc_status_t;
typedef struct { const char *name; acc_status_t status; const char *note; } accessory_t;
extern const accessory_t fisixdc_accessories[];
extern const int fisixdc_accessory_count;
const char *acc_status_text(acc_status_t s);
#endif
