/* deb_zstd.c - zstd decompression for FisixOS .deb support.
 *
 * Wrapper over the vendored zstd single-file decoder
 * (kernel/drivers/zstd/zstddeclib.c, generated from facebook/zstd
 * v1.5.7, BSD-3-Clause/GPLv2 dual; see LICENSE/COPYING there and
 * the FISIXOS PATCH note at the top of zstddeclib.c).
 *
 * Memory story (no libc, no heap):
 * - One static pool, DEB_ZSTD_POOL_SIZE bytes of .bss, holds
 *   everything zstd allocates while decoding. Measured on real
 *   Debian .zst tars: exactly one allocation, the DCtx, 95,968
 *   bytes (see deb_zstd_alloc_peak()). There is deliberately NO
 *   window buffer: one-shot decompression (ZSTD_decompressDCtx)
 *   decodes straight into the caller's output buffer and uses
 *   the already-decoded output as its history window; the frame
 *   header's windowSize is only a validation bound on match
 *   offsets, never an allocation. That is why a frame with an
 *   8 MiB window (zstd -19) still decodes fine as long as its
 *   content fits the output buffer: the host test proves it with
 *   tools/big19.tar.zst. The pool is 256 KiB, ~2.6x the measured
 *   DCtx, so the only way to exhaust it is a bigger DCtx in a
 *   future zstd version, which fails cleanly.
 * - The pool is a bump allocator: frees are no-ops and the pointer
 *   is reset on every deb_zstdunzip() call. A frame that needs more
 *   than the pool fails cleanly (DEB_ZSTD_WINDOW_TOO_BIG) instead
 *   of overflowing; allocation failure inside zstd surfaces as a
 *   ZSTD error, which the wrapper maps the same way.
 * - Output: caller-supplied (deb.c's 2 MiB tar scratch buffer).
 *   If the frame header carries a content size bigger than the
 *   output buffer, that is detected up front; otherwise a too-small
 *   output buffer surfaces as ZSTD_error_dstSize_tooSmall.
 *
 * Not reentrant: one static pool, and the .deb install path is
 * single-threaded. Do not call deb_zstdunzip() from an interrupt
 * handler or from two threads at once.
 */
#include "deb_zstd.h"
/* ZSTD_customMem / ZSTD_createDCtx_advanced live behind
 * ZSTD_STATIC_LINKING_ONLY in zstd.h. */
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd/zstd.h"

/* DCtx only: one-shot decompression needs no window buffer (the
 * output buffer is the window). Measured DCtx: 95,968 bytes. */
#define DEB_ZSTD_POOL_SIZE (256u * 1024u)
static uint8_t g_zstd_pool[DEB_ZSTD_POOL_SIZE];
static uint32_t g_zstd_pool_used = 0;
static uint32_t g_zstd_pool_peak = 0;
static int g_zstd_alloc_failed = 0;

void *fisix_zstd_malloc(size_t size)
{
	uint32_t aligned, off;
	if (size == 0)
		size = 1;
	/* 16-byte alignment: zstd assumes malloc-style alignment. */
	aligned = (uint32_t)((size + 15u) & ~15u);
	off = g_zstd_pool_used;
	if (off + aligned > sizeof(g_zstd_pool)) {
		g_zstd_alloc_failed = 1;
		return 0;
	}
	g_zstd_pool_used = off + aligned;
	if (g_zstd_pool_used > g_zstd_pool_peak)
		g_zstd_pool_peak = g_zstd_pool_used;
	return g_zstd_pool + off;
}

void *fisix_zstd_calloc(size_t n, size_t size)
{
	uint8_t *p;
	size_t total, i;
	/* Overflow-safe multiply: both factors are zstd-internal
	 * sizes, but a wrap here would hand out a too-small buffer. */
	if (n != 0 && size > (size_t)-1 / n)
		return 0;
	total = n * size;
	p = (uint8_t *)fisix_zstd_malloc(total);
	if (p == 0)
		return 0;
	for (i = 0; i < total; i++)
		p[i] = 0;
	return p;
}

void fisix_zstd_free(void *ptr)
{
	/* No-op by design: the pool is reset wholesale by
	 * deb_zstdunzip() after each decode. */
	(void)ptr;
}

uint32_t deb_zstd_alloc_peak(void)
{
	return g_zstd_pool_peak;
}

/* Adapters for the ZSTD_customMem function-pointer types, which
 * take an opaque pointer first. The pool ignores opaque. */
static void *zstd_alloc_adapter(void *opaque, size_t size)
{
	(void)opaque;
	return fisix_zstd_malloc(size);
}

static void zstd_free_adapter(void *opaque, void *ptr)
{
	(void)opaque;
	fisix_zstd_free(ptr);
}

int deb_zstdunzip(const uint8_t *in, uint32_t in_len, uint8_t *out,
		  uint32_t out_cap, uint32_t *out_len)
{
	ZSTD_customMem mem;
	ZSTD_DCtx *dctx;
	size_t r;
	unsigned long long content_size;
	if (!in || !out || !out_len)
		return DEB_ZSTD_BAD;
	if (out_cap == 0 || in_len == 0)
		return DEB_ZSTD_BAD;
	/* Fast path: the frame header usually carries the content
	 * size (dpkg-written .zst does). Refuse up front if it
	 * cannot fit the caller's buffer. */
	content_size = ZSTD_findDecompressedSize(in, in_len);
	if (content_size != ZSTD_CONTENTSIZE_UNKNOWN &&
	    content_size != ZSTD_CONTENTSIZE_ERROR &&
	    content_size > out_cap)
		return DEB_ZSTD_TOO_BIG;
	/* Fresh pool for this decode. */
	g_zstd_pool_used = 0;
	g_zstd_alloc_failed = 0;
	mem.customAlloc = zstd_alloc_adapter;
	mem.customFree = zstd_free_adapter;
	mem.opaque = 0;
	dctx = ZSTD_createDCtx_advanced(mem);
	if (dctx == 0)
		return g_zstd_alloc_failed ? DEB_ZSTD_WINDOW_TOO_BIG
					   : DEB_ZSTD_BAD;
	r = ZSTD_decompressDCtx(dctx, out, out_cap, in, in_len);
	{
		int rc;
		if (ZSTD_isError(r)) {
			if (g_zstd_alloc_failed)
				rc = DEB_ZSTD_WINDOW_TOO_BIG;
			else if (ZSTD_getErrorCode(r) ==
				 ZSTD_error_dstSize_tooSmall)
				rc = DEB_ZSTD_TOO_BIG;
			else
				rc = DEB_ZSTD_BAD;
		} else {
			*out_len = (uint32_t)r;
			rc = DEB_ZSTD_OK;
		}
		ZSTD_freeDCtx(dctx);
		/* Reclaim the pool: frees above were no-ops. */
		g_zstd_pool_used = 0;
		return rc;
	}
}
