#ifndef FISIX_FF_H
#define FISIX_FF_H
#include <stdint.h>
#include <stddef.h>
/* FisixFF reader, matching tools/make_ff.py. Stored only in v1. */
int ff_list(const uint8_t *img, size_t len, char *out, size_t out_size);
int ff_read(const uint8_t *img, size_t len, const char *name, const uint8_t **data, uint32_t *size);
#endif
