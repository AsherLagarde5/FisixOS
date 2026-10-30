/* See fisix_printf.h for license / origin note. */
#include "fisix_printf.h"

static void out_char(char *buf, size_t size, size_t *pos, char c) {
    if (*pos + 1 < size) buf[*pos] = c;
    (*pos)++;
}

static void out_str(char *buf, size_t size, size_t *pos, const char *s) {
    if (!s) s = "(null)";
    while (*s) out_char(buf, size, pos, *s++);
}

static void out_uint(char *buf, size_t size, size_t *pos, unsigned long long v,
                     int base, int upper, int width, int zero_pad) {
    char tmp[32];
    int i = 0, pad;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = digits[v % (unsigned)base]; v /= (unsigned)base; }
    pad = width - i;
    if (pad < 0) pad = 0;
    if (!zero_pad) while (pad-- > 0) out_char(buf, size, pos, ' ');
    else while (pad-- > 0) out_char(buf, size, pos, '0');
    while (i > 0) out_char(buf, size, pos, tmp[--i]);
}

static void out_int(char *buf, size_t size, size_t *pos, long long v,
                    int width, int zero_pad) {
    if (v < 0) {
        out_char(buf, size, pos, '-');
        if (width > 0) width--;
        out_uint(buf, size, pos, (unsigned long long)(-v), 10, 0, width, zero_pad);
    } else {
        out_uint(buf, size, pos, (unsigned long long)v, 10, 0, width, zero_pad);
    }
}

int fisix_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t pos = 0;
    if (!buf || size == 0) return 0;
    buf[0] = '\0';
    while (fmt && *fmt) {
        if (*fmt != '%') { out_char(buf, size, &pos, *fmt++); continue; }
        fmt++;
        {
            int width = 0, zero_pad = 0, longs = 0;
            if (*fmt == '0') { zero_pad = 1; fmt++; }
            while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
            if (*fmt == 'l') { longs = 1; fmt++; if (*fmt == 'l') { longs = 2; fmt++; } }
            switch (*fmt) {
                case 'd': case 'i':
                    if (longs == 2) out_int(buf, size, &pos, va_arg(ap, long long), width, zero_pad);
                    else if (longs == 1) out_int(buf, size, &pos, va_arg(ap, long), width, zero_pad);
                    else out_int(buf, size, &pos, va_arg(ap, int), width, zero_pad);
                    break;
                case 'u':
                    if (longs == 2) out_uint(buf, size, &pos, va_arg(ap, unsigned long long), 10, 0, width, zero_pad);
                    else if (longs == 1) out_uint(buf, size, &pos, va_arg(ap, unsigned long), 10, 0, width, zero_pad);
                    else out_uint(buf, size, &pos, va_arg(ap, unsigned int), 10, 0, width, zero_pad);
                    break;
                case 'x':
                    if (longs == 2) out_uint(buf, size, &pos, va_arg(ap, unsigned long long), 16, 0, width, zero_pad);
                    else if (longs == 1) out_uint(buf, size, &pos, va_arg(ap, unsigned long), 16, 0, width, zero_pad);
                    else out_uint(buf, size, &pos, va_arg(ap, unsigned int), 16, 0, width, zero_pad);
                    break;
                case 'X':
                    if (longs == 2) out_uint(buf, size, &pos, va_arg(ap, unsigned long long), 16, 1, width, zero_pad);
                    else if (longs == 1) out_uint(buf, size, &pos, va_arg(ap, unsigned long), 16, 1, width, zero_pad);
                    else out_uint(buf, size, &pos, va_arg(ap, unsigned int), 16, 1, width, zero_pad);
                    break;
                case 's':
                    out_str(buf, size, &pos, va_arg(ap, const char *));
                    break;
                case 'c':
                    out_char(buf, size, &pos, (char)va_arg(ap, int));
                    break;
                case 'p':
                    out_str(buf, size, &pos, "0x");
                    out_uint(buf, size, &pos, (unsigned long long)va_arg(ap, void *), 16, 0, 0, 0);
                    break;
                case '%':
                    out_char(buf, size, &pos, '%');
                    break;
                case '\0':
                    goto done;
                default:
                    out_char(buf, size, &pos, '%');
                    out_char(buf, size, &pos, *fmt);
                    break;
            }
            if (*fmt) fmt++;
        }
    }
done:
    if (pos < size) buf[pos] = '\0';
    else buf[size - 1] = '\0';
    return (int)pos;
}

int fisix_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = fisix_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}
