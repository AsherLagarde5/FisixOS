#ifndef FISIX_JPEG_H
#define FISIX_JPEG_H
#include <stdint.h>
int jpeg_dimensions(const uint8_t *data, uint32_t len, uint32_t *w, uint32_t *h);
/* Decode a baseline (SOF0, 8-bit, Huffman) JPEG into RGB, 3 bytes per
   pixel, row-major. out must hold w*h*3 bytes for the dimensions
   returned by jpeg_dimensions. Returns 1 on success, 0 on failure
   (progressive JPEGs, 16-bit quant tables, arithmetic coding, and
   4-component CMYK are not baseline and are rejected honestly). */
int jpeg_decode_rgb(const uint8_t *data, uint32_t len, uint8_t *out, uint32_t out_w, uint32_t out_h);
#endif
