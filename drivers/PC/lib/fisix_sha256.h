/* fisix_sha256.h - FisixOS-original SHA-256 (FIPS 180-4).
 *
 * License: GPLv2 (see LICENSE at tree root), pending
 * Derek's final choice). This file is original FisixOS code, written
 * from the public SHA-256 specification (initial values = fractional
 * parts of sqrt of first 8 primes, round constants = fractional parts
 * of cube roots of first 64 primes, standard compression function).
 * It is NOT a verbatim copy of Brad Conte's public-domain
 * crypto-algorithms, or any other SHA-256 implementation, although
 * that public-domain work was consulted as a reference for the
 * spec layout. SHA-256 itself is a public standard.
 * Freestanding: no libc, only <stdint.h>/<stddef.h>.
 * API mirrors the familiar init/update/final pattern for ease of use.
 * Test vectors: SHA256("") =
 *   e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
 * SHA256("abc") =
 *   ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
 */
#ifndef FISIX_SHA256_H
#define FISIX_SHA256_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} fisix_sha256_ctx;
void fisix_sha256_init(fisix_sha256_ctx *ctx);
void fisix_sha256_update(fisix_sha256_ctx *ctx, const uint8_t *data, size_t len);
void fisix_sha256_final(fisix_sha256_ctx *ctx, uint8_t hash[32]);
void fisix_sha256(const uint8_t *data, size_t len, uint8_t hash[32]);
#endif
