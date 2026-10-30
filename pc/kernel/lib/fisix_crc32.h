/* fisix_crc32.h - FisixOS-original CRC-32 (IEEE 802.3, polynomial 0xEDB88320).
 *
 * License: GPLv2 (see LICENSE at tree root).
 * pending Derek's final choice of FisixOS license).
 * This file is original FisixOS code, written for this OS from the
 * public CRC-32 specification (reflected polynomial, init 0xFFFFFFFF,
 * final XOR 0xFFFFFFFF). It is NOT copied from any third-party CRC
 * implementation. The algorithm itself is a public standard.
 * Freestanding: no libc, only <stdint.h>/<stddef.h>.
 * Use: file / update integrity checks, e.g. FisixOS update files,
 * before trusting a payload.
 */
#ifndef FISIX_CRC32_H
#define FISIX_CRC32_H
#include <stdint.h>
#include <stddef.h>
uint32_t fisix_crc32(const uint8_t *data, size_t len);
/* update() takes/returns the INTERNAL form (start 0xFFFFFFFF,
   final XOR is applied only by fisix_crc32()). */
uint32_t fisix_crc32_update(uint32_t crc, const uint8_t *data, size_t len);
/* Known test vector: fisix_crc32("123456789", 9) == 0xCBF43926 */
#endif
