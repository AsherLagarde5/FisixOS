/* See fisix_legal.h. Short notice only; full texts are in LICENSES/. */
#include "fisix_legal.h"

static const char legal_text[] =
    "FisixOS - Legal Notice\n"
    "FisixOS itself: licensed under GPLv2 (\n"
    "see LICENSE at the source tree root, marked DRAFT).\n"
    "Third-party permissive code used in FisixOS:\n"
    " - FisixOS-original CRC32 / SHA-256 / tiny snprintf in kernel/lib/\n"
    "   (GPLv2; spec-based, not copied verbatim\n"
    "   from Brad Conte public-domain code or mpaland/printf MIT code,\n"
    "   which were consulted as references - see LICENSES/THIRD-PARTY.md)\n"
    " - KallistiOS (Dreamcast port only): BSD-style KOS License,\n"
    "   attribution required. No Sega BIOS/ROMs bundled.\n"
    " - FatFs-style FAT handling: FisixOS fat.c is original code;\n"
    "   FatFs itself (ChaN, BSD-style, 1-clause) was NOT copied wholesale.\n"
    "NOT bundled: Nintendo/Commodore/Sega/TI BIOS or ROMs, Wi-Fi\n"
    "firmware blobs, proprietary drivers, or GPL code.\n"
    "Wi-Fi firmware, if needed, is fetched online by the user, not\n"
    "bundled. Full license texts: LICENSES/ and apps/legal.txt\n";

const char *fisix_legal_text(void) {
    return legal_text;
}
