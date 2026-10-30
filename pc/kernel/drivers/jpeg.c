#include "jpeg.h"

static const uint8_t zz[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
    28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37,
    44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47,
    55, 62, 63
};

/* cos(k*pi/16) for k=0..8, the rest follow by symmetry. */
static const float cos16[9] = {
    1.0f, 0.98078528f, 0.92387953f, 0.83146961f, 0.70710678f,
    0.55557023f, 0.38268343f, 0.19509032f, 0.0f
};
static float cos_pi16(int k)
{
    k %= 32; if (k < 0) k += 32;
    if (k <= 8) return cos16[k];
    if (k <= 16) return -cos16[16 - k];
    if (k <= 24) return -cos16[k - 16];
    return cos16[32 - k];
}

typedef struct { uint8_t count[16]; uint8_t sym[256]; } huff_t;
typedef struct {
    uint8_t id; uint8_t h, v; uint8_t tq; uint8_t dc, ac;
    uint16_t pw, ph; /* plane size in pixels */
    uint8_t *plane;  /* pw*ph bytes, allocated from a static pool */
} comp_t;

static uint16_t quant[4][64];
static int quant_ok[4];
static huff_t hdc[4], hac[4];
static int hdc_ok[4], hac_ok[4];
static comp_t comps[4];
static int ncomp;
static uint32_t img_w, img_h;
static uint8_t max_h, max_v;
static uint16_t restart_interval;
static int dc_pred[4];

/* Static plane pool: enough for a 640x480 image with 3 planes at
   full resolution in the worst (1x1) case is 640*480*3, but the
   kernel viewer uses at most 320x200, and the host test uses
   graceful sizes. Cap at 1024x768 total plane pixels. */
static uint8_t plane_pool[1024 * 768];
static uint32_t pool_used;

typedef struct {
    const uint8_t *p, *end;
    uint8_t cur; int bits; int err;
} bitr_t;
static int br_load(bitr_t *b)
{
    uint8_t raw;
    if (b->p >= b->end) { b->err = 1; return 0; }
    raw = *b->p++;
    if (raw == 0xFF) {
        if (b->p >= b->end) { b->err = 1; return 0; }
        uint8_t nxt = *b->p;
        if (nxt == 0x00) { b->p++; return 0xFF; }
        /* A marker here is not data; the MCU loop handles restarts
           at byte boundaries, so mid-MCU this is an error. */
        b->err = 1; return 0;
    }
    return raw;
}
static int br_bit(bitr_t *b)
{
    if (b->bits == 0) {
        int v = br_load(b); if (b->err) return 0;
        b->cur = (uint8_t)v; b->bits = 8;
    }
    int bit = (b->cur >> 7) & 1;
    b->cur <<= 1; b->bits--;
    return bit;
}
static int br_bits(bitr_t *b, int n)
{
    int v = 0, i;
    for (i = 0; i < n; i++) v = (v << 1) | br_bit(b);
    return v;
}
static int huff_decode(bitr_t *b, const huff_t *h)
{
    int code = 0, first = 0, idx = 0, len;
    for (len = 1; len <= 16; len++) {
        code = (code << 1) | br_bit(b);
        if (b->err) return -1;
        int cnt = h->count[len - 1];
        if (code - first < cnt) return h->sym[idx + (code - first)];
        idx += cnt; first = (first + cnt) << 1;
    }
    return -1;
}
static int extend(int v, int n)
{
    if (n == 0) return 0;
    if (v < (1 << (n - 1))) return v - ((1 << n) - 1);
    return v;
}
static void idct_block(const int *in, uint8_t *out, int stride)
{
    float tmp[64]; int x, y, u, v;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            float s = 0;
            for (u = 0; u < 8; u++) {
                float a = (u == 0) ? 0.70710678f : 1.0f;
                s += a * (float)in[y * 8 + u] * cos_pi16((2 * x + 1) * u);
            }
            tmp[y * 8 + x] = s * 0.5f;
        }
    }
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            float s = 0;
            for (v = 0; v < 8; v++) {
                float a = (v == 0) ? 0.70710678f : 1.0f;
                s += a * tmp[v * 8 + x] * cos_pi16((2 * y + 1) * v);
            }
            float val = s * 0.5f + 128.0f;
            int iv = (int)(val + 0.5f);
            if (iv < 0) iv = 0; if (iv > 255) iv = 255;
            out[y * stride + x] = (uint8_t)iv;
        }
    }
}
static uint16_t rd16b(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* Parse everything up to SOS. Returns the scan data pointer, or 0. */
static const uint8_t *parse_headers(const uint8_t *data, uint32_t len,
                                    const uint8_t **scan_end, uint32_t *scan_len_out,
                                    comp_t *scan_comps, int *nscan)
{
    const uint8_t *p = data, *end = data + len;
    int i;
    for (i = 0; i < 4; i++) { quant_ok[i] = 0; hdc_ok[i] = 0; hac_ok[i] = 0; }
    ncomp = 0; restart_interval = 0; img_w = 0; img_h = 0; max_h = 0; max_v = 0;
    if (len < 2 || data[0] != 0xFF || data[1] != 0xD8) return 0;
    p += 2;
    while (p + 4 <= end) {
        if (p[0] != 0xFF) return 0;
        uint8_t marker = p[1];
        if (marker == 0xD9) return 0; /* EOI before SOS */
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { p += 2; continue; }
        uint16_t seglen = rd16b(p + 2);
        if (seglen < 2 || p + 2 + seglen > end) return 0;
        const uint8_t *seg = p + 4; uint32_t segdata = seglen - 2;
        if (marker == 0xDB) { /* DQT */
            uint32_t off = 0;
            while (off < segdata) {
                uint8_t pq = seg[off] >> 4, tq = seg[off] & 15; off++;
                if (pq != 0 || tq >= 4 || off + 64 > segdata) return 0;
                for (i = 0; i < 64; i++) quant[tq][i] = seg[off + i];
                quant_ok[tq] = 1; off += 64;
            }
        } else if (marker == 0xC0) { /* SOF0 only */
            if (segdata < 6) return 0;
            if (seg[0] != 8) return 0; /* 8-bit only */
            img_h = rd16b(seg + 1); img_w = rd16b(seg + 3);
            ncomp = seg[5];
            if (ncomp < 1 || ncomp > 4 || segdata < 6 + (uint32_t)ncomp * 3) return 0;
            for (i = 0; i < ncomp; i++) {
                comps[i].id = seg[6 + i * 3];
                comps[i].h = seg[7 + i * 3] >> 4;
                comps[i].v = seg[7 + i * 3] & 15;
                comps[i].tq = seg[8 + i * 3];
                if (comps[i].h > max_h) max_h = comps[i].h;
                if (comps[i].v > max_v) max_v = comps[i].v;
            }
        } else if (marker == 0xC4) { /* DHT */
            uint32_t off = 0;
            while (off < segdata) {
                uint8_t tc = seg[off] >> 4, th = seg[off] & 15; off++;
                if (off + 16 > segdata) return 0;
                huff_t *h = (tc == 0) ? &hdc[th] : &hac[th];
                int *ok = (tc == 0) ? &hdc_ok[th] : &hac_ok[th];
                if (th >= 4) return 0;
                int total = 0;
                for (i = 0; i < 16; i++) { h->count[i] = seg[off + i]; total += h->count[i]; }
                off += 16;
                if (off + (uint32_t)total > segdata || total > 256) return 0;
                for (i = 0; i < total; i++) h->sym[i] = seg[off + i];
                off += total; *ok = 1;
            }
        } else if (marker == 0xDD) { /* DRI */
            if (segdata >= 2) restart_interval = rd16b(seg);
        } else if (marker == 0xDA) { /* SOS */
            if (segdata < 1) return 0;
            int ns = seg[0];
            if (ns < 1 || ns > 4 || segdata < 1 + (uint32_t)ns * 2 + 3) return 0;
            *nscan = ns;
            for (i = 0; i < ns; i++) {
                uint8_t cid = seg[1 + i * 2]; uint8_t tbl = seg[2 + i * 2];
                int j, found = -1;
                for (j = 0; j < ncomp; j++) if (comps[j].id == cid) found = j;
                if (found < 0) return 0;
                scan_comps[i] = comps[found];
                scan_comps[i].dc = tbl >> 4; scan_comps[i].ac = tbl & 15;
            }
            const uint8_t *scan = p + 2 + seglen;
            /* Scan runs to the next unstuffed FF D9 / FF D0-D7 handled
               by the bit reader; its end is the end of the file for
               our purposes, since EOI follows the scan. */
            *scan_end = end; *scan_len_out = (uint32_t)(end - scan);
            return scan;
        } else if (marker == 0xC2) {
            return 0; /* progressive: not baseline, reject */
        }
        p += 2 + seglen;
    }
    return 0;
}

int jpeg_dimensions(const uint8_t *data, uint32_t len, uint32_t *w, uint32_t *h)
{
    const uint8_t *scan_end = 0; uint32_t sl = 0; comp_t sc[4]; int ns = 0;
    const uint8_t *scan = parse_headers(data, len, &scan_end, &sl, sc, &ns);
    if (!scan || !img_w || !img_h) return 0;
    *w = img_w; *h = img_h; return 1;
}

int jpeg_decode_rgb(const uint8_t *data, uint32_t len, uint8_t *out, uint32_t out_w, uint32_t out_h)
{
    const uint8_t *scan_end = 0; uint32_t scan_len = 0; comp_t sc[4]; int ns = 0;
    const uint8_t *scan = parse_headers(data, len, &scan_end, &scan_len, sc, &ns);
    uint32_t i, mx, my, ci, by, bx;
    if (!scan || !img_w || !img_h || out_w < img_w || out_h < img_h) return 0;
    if (ncomp != ns) {
        /* Interleaved scans must cover all components; single-component
           scans in a multi-component file are not supported here. */
        if (!(ncomp == 1 && ns == 1)) return 0;
    }
    for (i = 0; i < (uint32_t)ncomp; i++) {
        if (!quant_ok[comps[i].tq]) return 0;
        comps[i].pw = (uint32_t)((img_w * comps[i].h + max_h - 1) / max_h);
        comps[i].ph = (uint32_t)((img_h * comps[i].v + max_v - 1) / max_v);
    }
    pool_used = 0;
    for (i = 0; i < (uint32_t)ncomp; i++) {
        uint32_t need = comps[i].pw * comps[i].ph;
        if (pool_used + need > sizeof(plane_pool)) return 0;
        comps[i].plane = plane_pool + pool_used; pool_used += need;
        for (uint32_t k = 0; k < need; k++) comps[i].plane[k] = 0;
    }
    /* Map scan components back to comps indices for tables. */
    {
        int scan_idx[4];
        for (i = 0; i < (uint32_t)ns; i++) {
            int j, f = -1;
            for (j = 0; j < ncomp; j++) if (comps[j].id == sc[i].id) f = j;
            scan_idx[i] = f;
            if (f < 0 || !hdc_ok[sc[i].dc] || !hac_ok[sc[i].ac]) return 0;
        }
        bitr_t br; br.p = scan; br.end = scan_end; br.cur = 0; br.bits = 0; br.err = 0;
        for (i = 0; i < 4; i++) dc_pred[i] = 0;
        uint32_t mcu_w = (uint32_t)max_h * 8, mcu_h = (uint32_t)max_v * 8;
        uint32_t nmx = (img_w + mcu_w - 1) / mcu_w;
        uint32_t nmy = (img_h + mcu_h - 1) / mcu_h;
        uint32_t mcus_since_rst = 0; int rst_idx = 0;
        for (my = 0; my < nmy; my++) {
            for (mx = 0; mx < nmx; mx++) {
                if (restart_interval && mcus_since_rst == restart_interval) {
                    /* Align to byte boundary, consume the RST marker. */
                    br.bits = 0;
                    if (br.p + 2 > br.end || br.p[0] != 0xFF ||
                        br.p[1] < 0xD0 || br.p[1] > 0xD7) return 0;
                    br.p += 2; rst_idx = (rst_idx + 1) & 7;
                    for (i = 0; i < 4; i++) dc_pred[i] = 0;
                    mcus_since_rst = 0;
                }
                for (ci = 0; ci < (uint32_t)ns; ci++) {
                    int cidx = scan_idx[ci];
                    comp_t *c = &comps[cidx];
                    for (by = 0; by < c->v; by++) {
                        for (bx = 0; bx < c->h; bx++) {
                            int coeff[64]; int raster[64];
                            for (i = 0; i < 64; i++) coeff[i] = 0;
                            int cat = huff_decode(&br, &hdc[sc[ci].dc]);
                            if (cat < 0 || br.err) return 0;
                            int diff = (cat == 0) ? 0 : extend(br_bits(&br, cat), cat);
                            if (br.err) return 0;
                            dc_pred[cidx] += diff;
                            coeff[0] = dc_pred[cidx];
                            int k = 1;
                            while (k < 64) {
                                int sym = huff_decode(&br, &hac[sc[ci].ac]);
                                if (sym < 0 || br.err) return 0;
                                if (sym == 0x00) break;
                                if (sym == 0xF0) { k += 16; continue; }
                                int run = sym >> 4, sz = sym & 15;
                                k += run;
                                if (k >= 64) return 0;
                                int v = (sz == 0) ? 0 : extend(br_bits(&br, sz), sz);
                                if (br.err) return 0;
                                coeff[k] = v; k++;
                            }
                            for (i = 0; i < 64; i++) {
                                int q = quant[c->tq][i];
                                raster[zz[i]] = coeff[i] * q;
                            }
                            /* Place the 8x8 block into the component plane. */
                            uint32_t px0 = (mx * c->h + bx) * 8;
                            uint32_t py0 = (my * c->v + by) * 8;
                            uint8_t blk[64];
                            idct_block(raster, blk, 8);
                            for (uint32_t yy = 0; yy < 8; yy++) {
                                for (uint32_t xx = 0; xx < 8; xx++) {
                                    uint32_t px = px0 + xx, py = py0 + yy;
                                    if (px < c->pw && py < c->ph)
                                        c->plane[py * c->pw + px] = blk[yy * 8 + xx];
                                }
                            }
                        }
                    }
                }
                mcus_since_rst++;
            }
        }
    }
    /* Upsample to RGB. */
    {
        int yidx = 0, cbidx = 1, cridx = 2;
        if (ncomp == 1) { yidx = 0; }
        else {
            /* Find components by ID 1/2/3, else fall back to order. */
            for (i = 0; i < (uint32_t)ncomp; i++) {
                if (comps[i].id == 1) yidx = (int)i;
                if (comps[i].id == 2) cbidx = (int)i;
                if (comps[i].id == 3) cridx = (int)i;
            }
        }
        for (uint32_t y = 0; y < img_h; y++) {
            for (uint32_t x = 0; x < img_w; x++) {
                uint8_t *px = out + (y * out_w + x) * 3;
                int Y = comps[yidx].plane[y * comps[yidx].pw + x];
                if (ncomp == 1) { px[0] = px[1] = px[2] = (uint8_t)Y; continue; }
                uint32_t cx = x * comps[cbidx].pw / img_w;
                uint32_t cy = y * comps[cbidx].ph / img_h;
                int Cb = comps[cbidx].plane[cy * comps[cbidx].pw + cx];
                int Cr = comps[cridx].plane[cy * comps[cridx].pw + cx];
                float fcb = (float)Cb - 128.0f, fcr = (float)Cr - 128.0f;
                int r = (int)((float)Y + 1.402f * fcr + 0.5f);
                int g = (int)((float)Y - 0.34414f * fcb - 0.71414f * fcr + 0.5f);
                int b = (int)((float)Y + 1.772f * fcb + 0.5f);
                if (r < 0) r = 0; if (r > 255) r = 255;
                if (g < 0) g = 0; if (g > 255) g = 255;
                if (b < 0) b = 0; if (b > 255) b = 255;
                px[0] = (uint8_t)r; px[1] = (uint8_t)g; px[2] = (uint8_t)b;
            }
        }
    }
    return 1;
}

#ifdef JPEG_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: jpeg_test in.jpg out.ppm\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(n); fread(data, 1, n, f); fclose(f);
    uint32_t w = 0, h = 0;
    if (!jpeg_dimensions(data, n, &w, &h)) { fprintf(stderr, "no dimensions\n"); return 1; }
    uint8_t *out = malloc(w * h * 3);
    if (!jpeg_decode_rgb(data, n, out, w, h)) { fprintf(stderr, "decode failed\n"); return 1; }
    FILE *o = fopen(argv[2], "wb");
    fprintf(o, "P6\n%u %u\n255\n", w, h); fwrite(out, 1, w * h * 3, o); fclose(o);
    printf("decoded %ux%u\n", w, h);
    return 0;
}
#endif
