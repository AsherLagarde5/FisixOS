/* deb_xz.c - xz decompression for FisixOS .deb support.
 *
 * Wrapper over the vendored xz-embedded decoder
 * (kernel/drivers/xz/, public domain, upstream commit
 * ae63ae3a36ed01724674e8f3d750dc47bf125410, 2024-12-30).
 *
 * Why single-call mode: xz_dec_init(XZ_SINGLE) uses the caller's
 * output buffer as the LZMA2 dictionary, so the decoder never
 * allocates a dictionary buffer. The only allocations are the three
 * decoder-state structs (xz_dec, xz_dec_bcj, xz_dec_lzma2); those
 * come from the static pool below. That is the whole memory story:
 *   - decoder-state pool: DEB_XZ_POOL_SIZE bytes of .bss (measured
 *     peak for real Debian .debs is reported by
 *     deb_xz_alloc_peak(); see docs/DEB.md)
 *   - dictionary: 0 bytes; the output buffer is the dictionary
 *   - output: caller-supplied (deb.c's 2 MiB tar scratch buffer)
 * No heap, no per-stream allocation, nothing to free at runtime:
 * the bump pointer is reset on every call.
 *
 * Not reentrant: one static pool, and the .deb install path is
 * single-threaded. Do not call deb_xzunzip() from an interrupt
 * handler or from two threads at once.
 */
#include "deb_xz.h"
/* Include xz_config.h (not just xz.h): it defines XZ_USE_CRC64,
 * which both declares xz_crc64_init() in xz.h and makes the
 * decoder verify CRC64 checks. Without it the CRC64 table is
 * never initialized and every CRC64 stream (i.e. every real
 * Debian .xz) fails its block check. */
#include "xz/xz_config.h"

/* Decoder-state pool. The three state structs total well under
 * this; the exact peak is measured by the host test and written up
 * in docs/DEB.md. If an allocation ever exceeds the pool,
 * fisix_xz_alloc() returns NULL and xz_dec_init() fails cleanly
 * instead of overflowing. */
#define DEB_XZ_POOL_SIZE (64u * 1024u)
static uint8_t g_xz_pool[DEB_XZ_POOL_SIZE];
static uint32_t g_xz_pool_used = 0;
static uint32_t g_xz_pool_peak = 0;

void *fisix_xz_alloc(size_t size)
{
	uint32_t aligned, off;
	if (size == 0)
		size = 1;
	/* 8-byte alignment: the state structs contain uint64_t. */
	aligned = (uint32_t)((size + 7u) & ~7u);
	off = g_xz_pool_used;
	if (off + aligned > sizeof(g_xz_pool))
		return 0;
	g_xz_pool_used = off + aligned;
	if (g_xz_pool_used > g_xz_pool_peak)
		g_xz_pool_peak = g_xz_pool_used;
	return g_xz_pool + off;
}

void fisix_xz_free(void *ptr)
{
	/* No-op by design: the pool is reset wholesale by
	 * deb_xzunzip() after each decode. */
	(void)ptr;
}

uint32_t deb_xz_alloc_peak(void)
{
	return g_xz_pool_peak;
}

int deb_xzunzip(const uint8_t *in, uint32_t in_len, uint8_t *out,
		uint32_t out_cap, uint32_t *out_len)
{
	static int crc_ready = 0;
	struct xz_dec *s;
	struct xz_buf b;
	enum xz_ret ret;
	if (!in || !out || !out_len)
		return DEB_XZ_BAD;
	if (out_cap == 0 || in_len == 0)
		return DEB_XZ_BAD;
	if (!crc_ready) {
		xz_crc32_init();
#ifdef XZ_USE_CRC64
		xz_crc64_init();
#endif
		crc_ready = 1;
	}
	/* Fresh pool for this decode. */
	g_xz_pool_used = 0;
	s = xz_dec_init(XZ_SINGLE, 0);
	if (s == 0)
		return DEB_XZ_BAD;
	b.in = in;
	b.in_pos = 0;
	b.in_size = in_len;
	b.out = out;
	b.out_pos = 0;
	b.out_size = out_cap;
	ret = xz_dec_run(s, &b);
	*out_len = (uint32_t)b.out_pos;
	xz_dec_end(s);
	/* Reclaim the pool: frees above were no-ops. */
	g_xz_pool_used = 0;
	if (ret == XZ_STREAM_END)
		return DEB_XZ_OK;
	if (ret == XZ_BUF_ERROR)
		return DEB_XZ_TOO_BIG;
	if (ret == XZ_OPTIONS_ERROR)
		return DEB_XZ_UNSUPPORTED;
	return DEB_XZ_BAD;
}
