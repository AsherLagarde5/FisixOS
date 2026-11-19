/* deb_zstd.h - zstd decompression for FisixOS .deb support.
 *
 * Thin wrapper over the vendored zstd single-file decoder
 * (kernel/drivers/zstd/zstddeclib.c, BSD-3-Clause/GPLv2 dual,
 * see LICENSE/COPYING there). Same call contract as deb_gunzip()
 * in deb_inflate.h so deb.c can use it the same way for
 * control.tar.zst / data.tar.zst.
 *
 * Freestanding: no libc, no heap. All zstd allocation goes to a
 * static pool in deb_zstd.c (see DEB_ZSTD_POOL_SIZE there): in
 * practice exactly one allocation, the DCtx (95,968 bytes
 * measured). One-shot decompression needs no window buffer: it
 * decodes into the caller's output buffer and uses the decoded
 * output as its history window, so a frame's window size never
 * costs pool memory. If allocation ever fails (e.g. a bigger DCtx
 * in a future zstd version), the call fails cleanly with
 * DEB_ZSTD_WINDOW_TOO_BIG instead of overflowing.
 *
 * Not reentrant: one static pool, single-threaded .deb path only.
 */
#ifndef FISIX_DEB_ZSTD_H
#define FISIX_DEB_ZSTD_H
#include <stdint.h>
#include <stddef.h>

#define DEB_ZSTD_OK 0
#define DEB_ZSTD_BAD (-1)             /* bad/corrupt input */
#define DEB_ZSTD_TOO_BIG (-2)         /* output buffer too small */
#define DEB_ZSTD_WINDOW_TOO_BIG (-3)  /* static pool exhausted (DCtx alloc failed) */

/* Decompress one .zst frame (handles multi-frame .zst files too).
 * Returns DEB_ZSTD_OK and sets *out_len on success.
 * DEB_ZSTD_TOO_BIG: the decompressed data does not fit out_cap.
 * DEB_ZSTD_WINDOW_TOO_BIG: the decoder ran out of static pool
 *   (the DCtx allocation failed; not reachable with the current
 *   256 KiB pool and the measured 95,968-byte DCtx — kept as a
 *   clean failure instead of an overflow).
 * DEB_ZSTD_BAD: anything else (bad magic, corrupt data, truncated).
 */
int deb_zstdunzip(const uint8_t *in, uint32_t in_len, uint8_t *out,
		  uint32_t out_cap, uint32_t *out_len);

/* Peak bytes used from the static pool, across all calls so far.
 * Host-test/diagnostic use: proves the pool size. */
uint32_t deb_zstd_alloc_peak(void);

/* Static bump allocator for the zstd decoder, used via the
 * ZSTD_malloc/ZSTD_calloc/ZSTD_free macros and the XXH_malloc/
 * XXH_free hooks in zstd/zstddeclib.c, and via the ZSTD_customMem
 * passed to ZSTD_createDCtx_advanced(). Frees are no-ops;
 * deb_zstdunzip() resets the pool between calls. Allocations are
 * 16-byte aligned: zstd assumes malloc-style alignment. */
void *fisix_zstd_malloc(size_t size);
void *fisix_zstd_calloc(size_t n, size_t size);
void fisix_zstd_free(void *ptr);

#endif
