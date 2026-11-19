/* deb_inflate.c - gzip / raw DEFLATE decompression, written from RFC 1951.
 *
 * Freestanding: no libc, no heap, no floating point. All state lives
 * in a small struct on the caller's stack; the output buffer is
 * caller-supplied. Tested on host against gzip -9 output (see
 * tools/deb_host_test.c).
 */
#include "deb_inflate.h"

typedef struct {
    const uint8_t *in;
    uint32_t in_len;
    uint32_t in_pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    uint32_t out_cap;
    uint32_t out_pos;
} inf_t;

static int inf_bits(inf_t *s, int n)
{
    int v = 0, i;
    for (i = 0; i < n; i++) {
        if (s->bitcnt == 0) {
            if (s->in_pos >= s->in_len) return -1;
            s->bitbuf = s->in[s->in_pos++];
            s->bitcnt = 8;
        }
        v |= ((s->bitbuf & 1) << i);
        s->bitbuf >>= 1;
        s->bitcnt--;
    }
    return v;
}

static void inf_align_byte(inf_t *s)
{
    s->bitcnt = 0;
    s->bitbuf = 0;
}

static int inf_put(inf_t *s, uint8_t b)
{
    if (s->out_pos >= s->out_cap) return -1;
    s->out[s->out_pos++] = b;
    return 0;
}

/* ---- canonical Huffman tables ---- */
#define INF_MAX_SYMS 288
#define INF_MAX_BITS 15
typedef struct {
    uint16_t count[INF_MAX_BITS + 1];
    uint16_t symbol[INF_MAX_SYMS];
    int n_symbols;
} huff_t;

static int huff_build(huff_t *h, const uint8_t *lengths, int n)
{
    int i, offs[INF_MAX_BITS + 2];
    for (i = 0; i <= INF_MAX_BITS; i++) h->count[i] = 0;
    for (i = 0; i < n; i++) {
        if (lengths[i] > INF_MAX_BITS) return -1;
        h->count[lengths[i]]++;
    }
    if (h->count[0] == (uint16_t)n) return -1; /* no codes at all */
    h->count[0] = 0;
    offs[1] = 0;
    for (i = 1; i <= INF_MAX_BITS; i++) offs[i + 1] = offs[i] + h->count[i];
    for (i = 0; i < n; i++) {
        if (lengths[i]) h->symbol[offs[lengths[i]]++] = (uint16_t)i;
    }
    h->n_symbols = n;
    return 0;
}

static int huff_decode(inf_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len <= INF_MAX_BITS; len++) {
        int bit = inf_bits(s, 1);
        if (bit < 0) return -1;
        code |= bit;
        if (code - first < h->count[len])
            return h->symbol[index + (code - first)];
        index += h->count[len];
        first += h->count[len];
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const uint8_t dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int inf_block(inf_t *s, const huff_t *lit, const huff_t *dist)
{
    for (;;) {
        int sym = huff_decode(s, lit);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (inf_put(s, (uint8_t)sym) != 0) return -2;
        } else if (sym == 256) {
            return 0;
        } else {
            int li, length, dsym, d;
            uint32_t distance, i;
            if (sym > 285) return -1;
            li = sym - 257;
            length = len_base[li];
            if (len_extra[li]) {
                int e = inf_bits(s, len_extra[li]);
                if (e < 0) return -1;
                length += e;
            }
            dsym = huff_decode(s, dist);
            if (dsym < 0 || dsym > 29) return -1;
            distance = dist_base[dsym];
            if (dist_extra[dsym]) {
                int e = inf_bits(s, dist_extra[dsym]);
                if (e < 0) return -1;
                distance += (uint32_t)e;
            }
            if (distance > s->out_pos) return -1;
            for (i = 0, d = (int)distance; i < (uint32_t)length; i++) {
                uint8_t b = s->out[s->out_pos - (uint32_t)d];
                if (inf_put(s, b) != 0) return -2;
            }
        }
    }
}

static int inf_fixed(inf_t *s)
{
    uint8_t lit_len[288], dist_len[32];
    huff_t lit, dist;
    int i;
    for (i = 0; i < 144; i++) lit_len[i] = 8;
    for (; i < 256; i++) lit_len[i] = 9;
    for (; i < 280; i++) lit_len[i] = 7;
    for (; i < 288; i++) lit_len[i] = 8;
    for (i = 0; i < 32; i++) dist_len[i] = 5;
    if (huff_build(&lit, lit_len, 288) != 0) return -1;
    if (huff_build(&dist, dist_len, 32) != 0) return -1;
    return inf_block(s, &lit, &dist);
}

static int inf_dynamic(inf_t *s)
{
    static const uint8_t order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    uint8_t cl_len[19], lit_len[288], dist_len[32];
    huff_t cl, lit, dist;
    int hlit, hdist, hclen, i, n;
    hlit = inf_bits(s, 5);
    hdist = inf_bits(s, 5);
    hclen = inf_bits(s, 4);
    if (hlit < 0 || hdist < 0 || hclen < 0) return -1;
    hlit += 257; hdist += 1; hclen += 4;
    if (hlit > 288 || hdist > 32) return -1;
    for (i = 0; i < 19; i++) cl_len[i] = 0;
    for (i = 0; i < hclen; i++) {
        int v = inf_bits(s, 3);
        if (v < 0) return -1;
        cl_len[order[i]] = (uint8_t)v;
    }
    if (huff_build(&cl, cl_len, 19) != 0) return -1;
    n = hlit + hdist;
    i = 0;
    while (i < n) {
        int sym = huff_decode(s, &cl);
        if (sym < 0) return -1;
        if (sym < 16) {
            uint8_t *arr = i < hlit ? lit_len : dist_len;
            int idx = i < hlit ? i : i - hlit;
            arr[idx] = (uint8_t)sym;
            i++;
        } else if (sym == 16) {
            int rep, prev, j;
            uint8_t *arr;
            int idx;
            if (i == 0) return -1;
            rep = inf_bits(s, 2);
            if (rep < 0) return -1;
            rep += 3;
            prev = i - 1 < hlit ? lit_len[i - 1] : dist_len[i - 1 - hlit];
            for (j = 0; j < rep && i < n; j++, i++) {
                arr = i < hlit ? lit_len : dist_len;
                idx = i < hlit ? i : i - hlit;
                arr[idx] = (uint8_t)prev;
            }
        } else if (sym == 17) {
            int rep = inf_bits(s, 3);
            int j;
            if (rep < 0) return -1;
            rep += 3;
            for (j = 0; j < rep && i < n; j++, i++) {
                uint8_t *arr = i < hlit ? lit_len : dist_len;
                int idx = i < hlit ? i : i - hlit;
                arr[idx] = 0;
            }
        } else if (sym == 18) {
            int rep = inf_bits(s, 7);
            int j;
            if (rep < 0) return -1;
            rep += 11;
            for (j = 0; j < rep && i < n; j++, i++) {
                uint8_t *arr = i < hlit ? lit_len : dist_len;
                int idx = i < hlit ? i : i - hlit;
                arr[idx] = 0;
            }
        } else {
            return -1;
        }
    }
    for (; i < 288; i++) lit_len[i] = 0;
    for (i = hdist; i < 32; i++) dist_len[i] = 0;
    if (huff_build(&lit, lit_len, hlit) != 0) return -1;
    if (huff_build(&dist, dist_len, hdist) != 0) return -1;
    return inf_block(s, &lit, &dist);
}

int deb_inflate(const uint8_t *in, uint32_t in_len, uint8_t *out,
                uint32_t out_cap, uint32_t *out_len)
{
    inf_t s;
    int r = -1;
    if (!in || !out || !out_len) return -1;
    s.in = in; s.in_len = in_len; s.in_pos = 0;
    s.bitbuf = 0; s.bitcnt = 0;
    s.out = out; s.out_cap = out_cap; s.out_pos = 0;
    for (;;) {
        int bfinal = inf_bits(&s, 1);
        int btype;
        if (bfinal < 0) { r = -1; break; }
        btype = inf_bits(&s, 2);
        if (btype < 0) { r = -1; break; }
        if (btype == 0) {
            uint32_t len, nlen, i;
            inf_align_byte(&s);
            if (s.in_pos + 4 > s.in_len) { r = -1; break; }
            len = (uint32_t)s.in[s.in_pos] | ((uint32_t)s.in[s.in_pos + 1] << 8);
            nlen = (uint32_t)s.in[s.in_pos + 2] | ((uint32_t)s.in[s.in_pos + 3] << 8);
            s.in_pos += 4;
            if ((len ^ nlen) != 0xFFFF) { r = -1; break; }
            if (s.in_pos + len > s.in_len) { r = -1; break; }
            if (s.out_pos + len > s.out_cap) { r = -2; break; }
            for (i = 0; i < len; i++) s.out[s.out_pos++] = s.in[s.in_pos++];
            r = 0;
        } else if (btype == 1) {
            r = inf_fixed(&s);
        } else if (btype == 2) {
            r = inf_dynamic(&s);
        } else {
            r = -1;
            break;
        }
        if (r != 0) break;
        if (bfinal) { r = 0; break; }
    }
    *out_len = s.out_pos;
    return r;
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int deb_gunzip(const uint8_t *in, uint32_t in_len, uint8_t *out,
               uint32_t out_cap, uint32_t *out_len)
{
    uint32_t pos, isize;
    int r;
    if (!in || in_len < 18) return -1;
    if (in[0] != 0x1F || in[1] != 0x8B || in[2] != 8) return -1;
    pos = 10;
    if (in[3] & 0x04) { /* FEXTRA */
        uint32_t xlen;
        if (pos + 2 > in_len) return -1;
        xlen = (uint32_t)in[pos] | ((uint32_t)in[pos + 1] << 8);
        pos += 2 + xlen;
    }
    if (in[3] & 0x08) { /* FNAME, NUL-terminated */
        while (pos < in_len && in[pos]) pos++;
        pos++;
    }
    if (in[3] & 0x10) { /* FCOMMENT */
        while (pos < in_len && in[pos]) pos++;
        pos++;
    }
    if (in[3] & 0x02) pos += 2; /* FHCRC */
    if (pos >= in_len) return -1;
    r = deb_inflate(in + pos, in_len - pos - 8, out, out_cap, out_len);
    if (r != 0) return r;
    isize = rd32le(in + in_len - 4);
    if (isize != *out_len) return -1;
    return 0;
}
