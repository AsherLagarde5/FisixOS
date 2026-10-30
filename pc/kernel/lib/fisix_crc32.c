/* See fisix_crc32.h for license / origin note. */
#include "fisix_crc32.h"

static uint32_t crc_table[256];
static int crc_table_ready = 0;

static void fisix_crc32_make_table(void) {
    uint32_t i, j, c;
    for (i = 0; i < 256; i++) {
        c = i;
        for (j = 0; j < 8; j++) {
            if (c & 1) c = 0xEDB88320U ^ (c >> 1);
            else c >>= 1;
        }
        crc_table[i] = c;
    }
    crc_table_ready = 1;
}

uint32_t fisix_crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    size_t i;
    if (!crc_table_ready) fisix_crc32_make_table();
    /* Internal form: start with 0xFFFFFFFF, do NOT XOR here.
       fisix_crc32() applies the final XOR once at the end. */
    for (i = 0; i < len; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    }
    return crc;
}

uint32_t fisix_crc32(const uint8_t *data, size_t len) {
    if (!data && len) return 0;
    return fisix_crc32_update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
}
