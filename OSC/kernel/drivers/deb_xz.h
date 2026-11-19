/* deb_xz.h - xz decompression for FisixOS .deb support.
 *
 * Thin wrapper over the vendored xz-embedded decoder
 * (kernel/drivers/xz/, public domain, see COPYING there).
 * Same call contract as deb_gunzip() in deb_inflate.h so deb.c can
 * use it the same way for control.tar.xz / data.tar.xz.
 *
 * Freestanding: no libc, no heap. The decoder state lives in a
 * static pool in deb_xz.c (see DEB_XZ_POOL_SIZE there); the LZMA2
 * dictionary is the caller's output buffer (xz-embedded single-call
 * mode), so no dictionary buffer is allocated at all and any
 * dictionary size the stream declares is fine as long as the
 * decompressed tar fits the output buffer.
 *
 * Limits (see docs/DEB.md for the full list):
 * - SHA-256 integrity checks are not supported (returns
 *   DEB_XZ_UNSUPPORTED); CRC32/CRC64/None are.
 * - Delta filter and non-x86 BCJ filters are not supported.
 * - Not reentrant: one static pool, single-threaded .deb path only.
 */
#ifndef FISIX_DEB_XZ_H
#define FISIX_DEB_XZ_H
#include <stdint.h>
#include <stddef.h>

#define DEB_XZ_OK 0
#define DEB_XZ_BAD (-1)         /* bad/corrupt input */
#define DEB_XZ_TOO_BIG (-2)     /* output buffer too small */
#define DEB_XZ_UNSUPPORTED (-3) /* stream option this decoder lacks */

/* Decompress one .xz stream (single stream, not concatenated).
 * Returns DEB_XZ_OK and sets *out_len on success.
 * DEB_XZ_TOO_BIG: the decompressed data does not fit out_cap.
 * DEB_XZ_UNSUPPORTED: the stream uses an option this decoder does
 *   not support (SHA-256 check, Delta filter, non-x86 BCJ, ...).
 * DEB_XZ_BAD: anything else (bad magic, corrupt data, truncated).
 */
int deb_xzunzip(const uint8_t *in, uint32_t in_len, uint8_t *out,
		uint32_t out_cap, uint32_t *out_len);

/* Peak bytes used from the static decoder-state pool, across all
 * calls so far. Host-test/diagnostic use: proves the pool size. */
uint32_t deb_xz_alloc_peak(void);

/* Static bump allocator for the xz-embedded decoder state, used
 * via the kmalloc/kfree macros in xz/xz_config.h. Frees are
 * no-ops; deb_xzunzip() resets the pool between calls. */
void *fisix_xz_alloc(size_t size);
void fisix_xz_free(void *ptr);

#endif
