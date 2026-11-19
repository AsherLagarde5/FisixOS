/* deb_inflate.h - gzip / raw DEFLATE decompression for FisixOS .deb support.
 *
 * Small, self-contained inflate written from RFC 1951 (DEFLATE) and
 * RFC 1952 (gzip). No zlib, no libc, no heap: the caller supplies the
 * output buffer. Used by deb.c to unpack control.tar.gz / data.tar.gz.
 *
 * .deb files from real Debian are usually xz-compressed; those are
 * detected by deb.c and reported as unsupported (see docs/DEB.md and
 * tools/deb_repack_gz.py, which repacks an xz .deb to a gz .deb on
 * the host). This file only handles gzip (and raw deflate).
 */
#ifndef FISIX_DEB_INFLATE_H
#define FISIX_DEB_INFLATE_H
#include <stdint.h>
#include <stddef.h>

/* Decompress a gzip stream.
 * Returns 0 on success, -1 on bad input, -2 if the output buffer is
 * too small. On success *out_len is the decompressed size (which must
 * equal the gzip ISIZE trailer; that is checked).
 * The gzip CRC32 is NOT verified (FisixOS has a crc32 in lib, but a
 * truncated/corrupt stream already fails the inflate or ISIZE check;
 * a host-side repack/verify step is the integrity check).
 */
int deb_gunzip(const uint8_t *in, uint32_t in_len, uint8_t *out,
               uint32_t out_cap, uint32_t *out_len);

/* Raw DEFLATE stream (no gzip wrapper). Same return codes. */
int deb_inflate(const uint8_t *in, uint32_t in_len, uint8_t *out,
                uint32_t out_cap, uint32_t *out_len);

#endif
