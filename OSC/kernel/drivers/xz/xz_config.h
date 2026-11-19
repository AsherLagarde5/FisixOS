/* SPDX-License-Identifier: 0BSD */

/*
 * FisixOS configuration for XZ Embedded (xz-embedded, public domain,
 * see COPYING in this directory).
 *
 * Adapted from the upstream userspace xz_config.h (same project,
 * same license) for the freestanding FisixOS kernel: no libc, no
 * heap. Differences from upstream's userspace config:
 * - Only XZ_DEC_SINGLE is enabled: single-call mode uses the caller's
 *   output buffer as the LZMA2 dictionary, so no dictionary buffer
 *   is ever allocated and no vmalloc is needed. This is what
 *   deb_xzunzip() in ../deb_xz.c uses.
 * - Only the x86 BCJ filter is enabled (FisixOS is x86-64; ARM/PowerPC/
 *   IA64/SPARC BCJ decoders are not vendored).
 * - CRC64 is enabled: real Debian .xz streams use CRC64 checks.
 * - SHA-256 checks, concatenated streams, and XZ_DEC_ANY_CHECK are
 *   NOT enabled: a stream using a SHA-256 check fails with a clear
 *   "unsupported" error instead of decoding unverified. See
 *   ../deb_xz.c and docs/DEB.md.
 * - kmalloc/kfree/vmalloc/vfree are mapped to the static bump
 *   allocator in ../deb_xz.c (fisix_xz_alloc/fisix_xz_free). In
 *   single-call mode the decoder makes exactly three allocations
 *   per xz_dec_init() (xz_dec, xz_dec_bcj, xz_dec_lzma2 state) and
 *   frees them with xz_dec_end(); the bump pointer is reset by
 *   deb_xzunzip() between calls. This is not reentrant and never
 *   runs concurrently: the kernel .deb path is single-threaded.
 * - memeq/memzero are byte loops, not libc memcmp/memset, so this
 *   header does not depend on the kernel's memcpy/memset shims.
 *
 * This file is FisixOS-written (0BSD, same as upstream) and lives
 * next to the vendored upstream files so that their
 * #include "xz_config.h" picks it up. The vendored upstream files
 * themselves are unmodified.
 */

#ifndef FISIX_XZ_CONFIG_H
#define FISIX_XZ_CONFIG_H

/* Single-call mode only: no preallocated/dynamic dictionary. */
#define XZ_DEC_SINGLE

/* CRC64 integrity checks (what Debian .xz streams use). */
#define XZ_USE_CRC64

/* x86 BCJ filter only: .debs are x86-64. */
#define XZ_DEC_X86

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h> /* declarations for memcpy/memmove, defined in deb.c */

#include "xz.h"

/* Static bump allocator for the decoder state; defined in
 * ../deb_xz.c. The second argument (GFP flags) is ignored. */
void *fisix_xz_alloc(size_t size);
void fisix_xz_free(void *ptr);

#define kmalloc(size, flags) fisix_xz_alloc(size)
#define kfree(ptr) fisix_xz_free(ptr)
#define vmalloc(size) fisix_xz_alloc(size)
#define vfree(ptr) fisix_xz_free(ptr)

/* Byte-loop memeq/memzero: no libc dependency. */
static inline int fisix_xz_memeq(const uint8_t *a, const uint8_t *b,
				 size_t size)
{
	size_t i;
	for (i = 0; i < size; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}

static inline void fisix_xz_memzero(uint8_t *buf, size_t size)
{
	size_t i;
	for (i = 0; i < size; i++)
		buf[i] = 0;
}

#define memeq(a, b, size) (fisix_xz_memeq((const uint8_t *)(a), \
					 (const uint8_t *)(b), (size)) == 1)
#define memzero(buf, size) fisix_xz_memzero((uint8_t *)(buf), (size))

#ifndef min
#	define min(x, y) ((x) < (y) ? (x) : (y))
#endif
#define min_t(type, x, y) min(x, y)

#ifndef fallthrough
#	if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311
#		define fallthrough [[fallthrough]]
#	elif (defined(__GNUC__) && __GNUC__ >= 7) \
		|| (defined(__clang_major__) && __clang_major__ >= 10)
#		define fallthrough __attribute__((__fallthrough__))
#	else
#		define fallthrough do {} while (0)
#	endif
#endif

/*
 * Some functions have been marked with __always_inline to keep the
 * performance reasonable even when the compiler is optimizing for
 * small code size.
 */
#ifndef __always_inline
#	ifdef __GNUC__
#		define __always_inline \
			inline __attribute__((__always_inline__))
#	else
#		define __always_inline inline
#	endif
#endif

/* Inline functions to access unaligned unsigned 32-bit integers */
#ifndef get_unaligned_le32
static inline uint32_t get_unaligned_le32(const uint8_t *buf)
{
	return (uint32_t)buf[0]
			| ((uint32_t)buf[1] << 8)
			| ((uint32_t)buf[2] << 16)
			| ((uint32_t)buf[3] << 24);
}
#endif

#ifndef get_unaligned_be32
static inline uint32_t get_unaligned_be32(const uint8_t *buf)
{
	return (uint32_t)((uint32_t)buf[0] << 24)
			| ((uint32_t)buf[1] << 16)
			| ((uint32_t)buf[2] << 8)
			| (uint32_t)buf[3];
}
#endif

#ifndef put_unaligned_le32
static inline void put_unaligned_le32(uint32_t val, uint8_t *buf)
{
	buf[0] = (uint8_t)val;
	buf[1] = (uint8_t)(val >> 8);
	buf[2] = (uint8_t)(val >> 16);
	buf[3] = (uint8_t)(val >> 24);
}
#endif

#ifndef put_unaligned_be32
static inline void put_unaligned_be32(uint32_t val, uint8_t *buf)
{
	buf[0] = (uint8_t)(val >> 24);
	buf[1] = (uint8_t)(val >> 16);
	buf[2] = (uint8_t)(val >> 8);
	buf[3] = (uint8_t)val;
}
#endif

#ifndef get_le32
#	define get_le32 get_unaligned_le32
#endif
#ifndef get_be32
#	define get_be32 get_unaligned_be32
#endif

#endif
