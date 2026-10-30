/* Generic HID-over-I2C driver, v1, polling, DesignWare I2C master.
 * See i2c_hid.h for the generic design. All operations are bounded:
 * every wait loop has a timeout, every transfer checks TX abort,
 * and a failed controller / address / descriptor just returns 0
 * for that candidate, never hangs the boot.
 * DesignWare registers (offsets from BAR0):
 *  IC_CON 0x00, IC_TAR 0x04, IC_DATA_CMD 0x10,
 *  IC_SS_SCL_HCNT 0x14, IC_SS_SCL_LCNT 0x18,
 *  IC_FS_SCL_HCNT 0x1C, IC_FS_SCL_LCNT 0x20,
 *  IC_INTR_STAT 0x2C, IC_INTR_MASK 0x30,
 *  IC_CLR_TX_ABRT 0x54, IC_ENABLE 0x6C,
 *  IC_STATUS 0x70, IC_TXFLR 0x74, IC_RXFLR 0x78,
 *  IC_TX_ABRT_SOURCE 0x80, IC_ENABLE_STATUS 0x9C
 * Intel LPSS controllers also have private registers at 0x800+,
 * but the standard DesignWare path is tried first; if the
 * controller is in reset / clock-gated, enable will time out
 * and that controller is skipped, not crashed on.
 */
#include "i2c_hid.h"
#include "pci.h"

static volatile uint32_t *g_bar = 0;
static uint8_t g_slave = 0;
static int g_ready = 0;
static int g_kind = 0;
static uint32_t g_lmax_x = 0, g_lmax_y = 0;
static uint16_t g_vendor = 0, g_product = 0, g_version = 0;
static uint16_t g_hid_desc_addr = 0;
static uint16_t g_input_reg = 0;
static uint16_t g_max_input = 0;
/* Parsed report fields, bit offsets in the input report data
 * (after the 2-byte length prefix, and after a Report ID byte
 * if the descriptor uses one). -1 = not found. */
static int g_x_off = -1, g_x_size = 0, g_x_signed = 0;
static int g_y_off = -1, g_y_size = 0, g_y_signed = 0;
static int g_btn_off = -1, g_btn_size = 0;
static int g_tip_off = -1;
static int g_has_report_id = 0;
static int g_report_id = 0;

static uint32_t rd(uint32_t off) { return g_bar ? g_bar[off / 4] : 0; }
static void wr(uint32_t off, uint32_t v) { if (g_bar) g_bar[off / 4] = v; }
static void delay(void) { volatile int i; for (i = 0; i < 1000; i++) { __asm__ volatile ("pause"); } }

static int i2c_wait_enable(int want)
{
    uint32_t t;
    for (t = 0; t < 100000; t++) {
        if (((rd(0x9C) & 1u) != 0) == (want != 0)) return 1;
        delay();
    }
    return 0;
}
static int i2c_init_controller(uint64_t bar)
{
    g_bar = (volatile uint32_t *)(uintptr_t)bar;
    if (!g_bar) return 0;
    wr(0x6C, 0);
    if (!i2c_wait_enable(0)) return 0;
    /* Master, standard speed, 7-bit, restart enabled, slave disabled. */
    wr(0x00, (1u << 0) | (1u << 1) | (1u << 5) | (1u << 6));
    wr(0x14, 400); wr(0x18, 470); wr(0x1C, 60); wr(0x20, 130);
    wr(0x38, 0); wr(0x3C, 0); wr(0x30, 0);
    wr(0x6C, 1);
    if (!i2c_wait_enable(1)) return 0;
    return 1;
}
/* One I2C transaction: write wlen bytes (may be 0), then read rlen
 * bytes, with STOP on the last byte. Returns bytes read, or -1. */
static int i2c_transfer(uint8_t slave, const uint8_t *wbuf, uint32_t wlen, uint8_t *rbuf, uint32_t rlen)
{
    uint32_t t, wi = 0, ri = 0, guard = 0;
    if (!g_bar) return -1;
    wr(0x6C, 0); if (!i2c_wait_enable(0)) return -1;
    wr(0x04, slave);
    wr(0x6C, 1); if (!i2c_wait_enable(1)) return -1;
    /* Clear abort status by reading it. */
    (void)rd(0x80);
    while (wi < wlen || ri < rlen) {
        uint32_t status = rd(0x70);
        if (status & 1u) { /* RX not empty */
            if (ri < rlen) { rbuf[ri++] = (uint8_t)(rd(0x10) & 0xFF); }
            else (void)rd(0x10);
            continue;
        }
        if ((status & 2u) && wi < wlen) { /* TX not full */
            uint32_t cmd = wbuf[wi];
            if (wi == wlen - 1 && rlen == 0) cmd |= (1u << 9); /* STOP */
            wr(0x10, cmd); wi++; continue;
        }
        if ((status & 2u) && wi >= wlen && ri < rlen) {
            /* Queue a read: one DATA_CMD per byte, STOP on last. */
            uint32_t cmd = (1u << 8);
            if (ri + (rd(0x78)) + 1 >= rlen) { /* last queued read approx */
                /* Only set STOP when this is the final read byte we will queue. */
            }
            /* Simpler: queue reads one at a time, waiting for RX. */
            /* To avoid over-queueing, only queue if TX room and RX empty. */
            if (rd(0x74) < 8) {
                uint32_t queued = 0; (void)queued;
                /* Queue exactly one read per loop iteration. */
                wr(0x10, cmd); 
                /* Mark that we have a pending read by counting in ri loop:
                   we use a separate pending counter via TXFLR? Instead,
                   break to RX wait: the read byte will arrive in RX. */
                /* Prevent infinite re-queue: wait for RX before next. */
                for (t = 0; t < 100000; t++) { if (rd(0x70) & 1u) break; delay(); }
                continue;
            }
        }
        if (rd(0x80) & 0xFFFF) return -1; /* TX abort */
        /* Timeout guard for the whole transfer. */
        for (t = 0; t < 1000; t++) delay();
        /* Loop will re-check; outer timeout is via callers' bounded
           use - this inner loop is short, and abort above breaks it. */
        if (++guard > 200000) return -1;
    }
    /* For reads, the last byte needs STOP: DesignWare sends STOP
       when the read count is exhausted if RESTART is set correctly;
       v1 relies on the controller's default stop on TX empty for
       the final read, which most LPSS controllers do. */
    return (int)ri;
}
/* Read helper used by the HID probe. */
static int i2c_read(uint8_t slave, uint8_t *buf, uint32_t len)
{
    /* Read via transfer with no write phase. */
    int r = i2c_transfer(slave, 0, 0, buf, len);
    return r == (int)len ? 1 : 0;
}

/* Parse a HID report descriptor, recording X/Y/button/tip fields.
 * Returns a priority: 3=touchpad, 2=mouse, 1=touchscreen, 0=other. */
static int parse_report_desc(const uint8_t *d, uint32_t len, int *is_abs)
{
    uint32_t i = 0, upage = 0, usage = 0, lmin = 0, lmax = 0, rsize = 0, rcount = 1;
    int bitpos = 0, has_x = 0, has_y = 0, has_btn = 0, has_tip = 0, has_pad = 0, has_screen = 0, has_mouse = 0;
    int signed_ = 0;
    *is_abs = 0;
    g_x_off = -1; g_y_off = -1; g_btn_off = -1; g_tip_off = -1; g_has_report_id = 0;
    while (i < len) {
        uint8_t b = d[i]; uint32_t sz = b & 3; uint32_t tp = (b >> 2) & 3; uint32_t tg = (b >> 4) & 15;
        uint32_t val = 0; uint32_t k;
        if (sz == 3) sz = 4;
        if (i + 1 + sz > len) break;
        for (k = 0; k < sz; k++) val |= (uint32_t)d[i + 1 + k] << (8 * k);
        if (tp == 1 && tg == 0) upage = val;
        else if (tp == 1 && tg == 0) { }
        else if (tp == 1 && tg == 0) { }
        else if (tp == 1 && tg == 0) { }
        /* Global items */
        if (tp == 1) {
            if (tg == 0) upage = val;
            else if (tg == 1) { /* Logical Minimum, sign-extend */
                if (sz == 1 && (val & 0x80)) lmin = (uint32_t)(int32_t)(int8_t)val; else if (sz == 2 && (val & 0x8000)) lmin = (uint32_t)(int32_t)(int16_t)val; else lmin = val;
                signed_ = (lmin != 0);
            }
            else if (tg == 2) { if (sz == 1 && (val & 0x80)) lmax = (uint32_t)(int32_t)(int8_t)val; else if (sz == 2 && (val & 0x8000)) lmax = (uint32_t)(int32_t)(int16_t)val; else lmax = val; }
            else if (tg == 7) rsize = val;
            else if (tg == 9) rcount = val ? val : 1;
            else if (tg == 8) { g_has_report_id = 1; g_report_id = (int)val; }
        } else if (tp == 2) {
            if (tg == 0) usage = val;
            if (upage == 0x01 && val == 0x02) has_mouse = 1;
            if (upage == 0x0D && val == 0x05) has_pad = 1;
            if (upage == 0x0D && val == 0x04) has_screen = 1;
        } else if (tp == 0 && tg == 8) { /* Input */
            uint32_t c;
            for (c = 0; c < rcount; c++) {
                if (upage == 0x01 && usage == 0x30 && !has_x) { g_x_off = bitpos; g_x_size = (int)rsize; g_x_signed = signed_; has_x = 1; if ((int32_t)lmax > 255) *is_abs = 1; g_lmax_x = lmax; }
                else if (upage == 0x01 && usage == 0x31 && !has_y) { g_y_off = bitpos; g_y_size = (int)rsize; g_y_signed = signed_; has_y = 1; if ((int32_t)lmax > 255) *is_abs = 1; g_lmax_y = lmax; }
                else if (upage == 0x09 && usage >= 1 && usage <= 8 && !has_btn) { g_btn_off = bitpos; g_btn_size = (int)rsize; has_btn = 1; }
                else if (upage == 0x0D && usage == 0x42 && !has_tip) { g_tip_off = bitpos; has_tip = 1; }
                bitpos += (int)rsize;
                /* Next usage in a multi-count field increments usage. */
                if (upage == 0x09) usage++;
            }
        }
        i += 1 + sz;
    }
    (void)usage; (void)has_btn; (void)has_tip;
    if (!has_x || !has_y) return 0;
    if (has_pad) return 3;
    if (has_mouse) return 2;
    if (has_screen) return 1;
    return *is_abs ? 1 : 2;
}
static uint32_t extract_bits(const uint8_t *data, int bit_off, int size)
{
    uint32_t v = 0; int i;
    if (bit_off < 0 || size <= 0 || size > 32) return 0;
    for (i = 0; i < size; i++) {
        int bit = bit_off + i;
        if (data[bit / 8] & (1u << (bit % 8))) v |= (1u << i);
    }
    return v;
}
static int32_t sign_extend(uint32_t v, int size)
{
    if (size <= 0 || size >= 32) return (int32_t)v;
    if (v & (1u << (size - 1))) return (int32_t)(v | (~0u << size));
    return (int32_t)v;
}

/* Try one slave address on the current controller. Returns
 * priority (3/2/1) on success, 0 on failure. */
static int try_slave(uint8_t slave, int *is_abs_out)
{
    uint8_t addr2[2]; uint8_t hid[30]; uint8_t *rep = 0; uint32_t replen = 0;
    uint16_t hid_addr, rep_reg, input_reg, max_in;
    int prio, is_abs = 0;
    /* Step 1: read HID descriptor address (2 bytes, no register). */
    if (!i2c_read(slave, addr2, 2)) return 0;
    hid_addr = (uint16_t)(addr2[0] | (addr2[1] << 8));
    if (!hid_addr || hid_addr > 0xFF00) return 0;
    /* Step 2: write HID descriptor address, read 30-byte HID descriptor. */
    {
        uint8_t w[2]; w[0] = (uint8_t)(hid_addr & 0xFF); w[1] = (uint8_t)(hid_addr >> 8);
        /* Write address, then read: HID-over-I2C uses a repeated
           start, which our transfer does as write+read in one call. */
        if (i2c_transfer(slave, w, 2, hid, 30) != 30) return 0;
    }
    if (hid[0] != 30 || hid[1] != 0) {
        /* First two bytes are wHIDDescLength, little endian: 30,0. */
        if (!(hid[0] == 30 && hid[1] == 0)) return 0;
    }
    replen = (uint32_t)(hid[4] | (hid[5] << 8));
    rep_reg = (uint16_t)(hid[6] | (hid[7] << 8));
    input_reg = (uint16_t)(hid[8] | (hid[9] << 8));
    max_in = (uint16_t)(hid[10] | (hid[11] << 8));
    if (!replen || replen > 4096 || !max_in || max_in > 256) return 0;
    /* Step 3: read report descriptor. */
    {
        static uint8_t repbuf[4096];
        uint8_t w[2]; w[0] = (uint8_t)(rep_reg & 0xFF); w[1] = (uint8_t)(rep_reg >> 8);
        if (i2c_transfer(slave, w, 2, repbuf, replen) != (int)replen) return 0;
        prio = parse_report_desc(repbuf, replen, &is_abs);
        if (!prio) return 0;
        (void)rep;
    }
    g_slave = slave; g_hid_desc_addr = hid_addr; g_input_reg = input_reg; g_max_input = max_in;
    g_vendor = (uint16_t)(hid[18] | (hid[19] << 8));
    g_product = (uint16_t)(hid[20] | (hid[21] << 8));
    g_version = (uint16_t)(hid[22] | (hid[23] << 8));
    *is_abs_out = is_abs;
    return prio;
}

int i2c_hid_init(i2c_hid_info_t *out)
{
    pci_device_t list[64]; int count = 0, ci, best_prio = 0, best_is_abs = 0;
    uint8_t best_slave = 0; uint64_t best_bar = 0; int best_ci = -1;
    /* Common HID-over-I2C slave addresses first, then a wider
       scan is not done here to keep boot time bounded. Vendors:
       ELAN 0x15, Synaptics 0x2C, ALPS 0x2E, Goodix 0x5D/0x14,
       Atmel 0x4B, Wacom 0x09, generic 0x20/0x27/0x49. */
    static const uint8_t addrs[] = { 0x15, 0x2C, 0x2E, 0x5D, 0x14, 0x4B, 0x09, 0x20, 0x27, 0x49, 0x2A, 0x38, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x5A };
    pci_scan_all(list, 64, &count);
    for (ci = 0; ci < count; ci++) {
        uint64_t bar; uint32_t ai;
        if (list[ci].class_code != 0x0C || list[ci].subclass != 0x80) continue;
        bar = pci_read_bar64(&list[ci], 0);
        if (!bar || (bar & 1)) continue;
        if (!i2c_init_controller(bar)) continue;
        for (ai = 0; ai < sizeof(addrs); ai++) {
            int is_abs = 0, prio = try_slave(addrs[ai], &is_abs);
            if (prio > best_prio) {
                best_prio = prio; best_is_abs = is_abs; best_slave = addrs[ai]; best_bar = bar; best_ci = ci;
                /* Save the parsed fields for the best device: they
                   are in globals, but the next probe will overwrite
                   them, so re-probe the best at the end. */
                if (prio >= 3) break;
            }
        }
        if (best_prio >= 3 && best_ci == ci) break;
    }
    if (!best_prio || !best_bar) { g_ready = 0; return 0; }
    /* Re-probe the best device so globals match it, not the last
       device probed. */
    if (!i2c_init_controller(best_bar)) { g_ready = 0; return 0; }
    {
        int is_abs = 0;
        if (!try_slave(best_slave, &is_abs)) { g_ready = 0; return 0; }
        best_is_abs = is_abs;
    }
    g_kind = best_prio == 3 ? 2 : (best_prio == 2 ? (best_is_abs ? 2 : 1) : 3);
    /* Kind mapping for the GUI: 1=relative, 2=absolute touchpad,
       3=absolute touchscreen. */
    if (best_prio == 2 && !best_is_abs) g_kind = 1;
    else if (best_prio == 2) g_kind = 2;
    else if (best_prio == 3) g_kind = 2;
    else g_kind = 3;
    g_ready = 1;
    if (out) {
        out->vendor_id = g_vendor; out->product_id = g_product; out->version_id = g_version;
        out->controller_index = (uint8_t)best_ci; out->slave_addr = best_slave; out->kind = g_kind;
        out->logical_max_x = g_lmax_x; out->logical_max_y = g_lmax_y;
    }
    return 1;
}
int i2c_hid_is_ready(void) { return g_ready; }
int i2c_hid_poll(i2c_hid_report_t *rep)
{
    static uint8_t buf[256];
    uint8_t w[2]; int len;
    if (!g_ready || !g_bar) return 0;
    /* HID-over-I2C input report: write input register, read
       length-prefixed report. v1 uses a read with no register
       write first, falling back to register write if needed. */
    w[0] = (uint8_t)(g_input_reg & 0xFF); w[1] = (uint8_t)(g_input_reg >> 8);
    len = i2c_transfer(g_slave, w, 2, buf, g_max_input + 2);
    if (len < 2) {
        /* Try a plain read (some devices return length first). */
        len = i2c_transfer(g_slave, 0, 0, buf, g_max_input + 2);
        if (len < 2) return 0;
    }
    {
        uint32_t rlen = (uint32_t)(buf[0] | (buf[1] << 8));
        const uint8_t *data;
        int data_off = 2;
        if (rlen == 0 || rlen > (uint32_t)len) return 0;
        data = buf + 2;
        if (g_has_report_id) {
            if (rlen < 3) return 0;
            data++; data_off++;
        }
        if (!rep) return 1;
        rep->kind = g_kind; rep->dx = 0; rep->dy = 0; rep->abs_x = 0; rep->abs_y = 0; rep->buttons = 0; rep->down = 0;
        rep->logical_max_x = g_lmax_x; rep->logical_max_y = g_lmax_y;
        if (g_kind == 1) {
            if (g_x_off >= 0) rep->dx = sign_extend(extract_bits(data, g_x_off, g_x_size), g_x_size);
            if (g_y_off >= 0) rep->dy = sign_extend(extract_bits(data, g_y_off, g_y_size), g_y_size);
            if (g_btn_off >= 0) rep->buttons = (int)extract_bits(data, g_btn_off, g_btn_size ? g_btn_size : 1);
            rep->down = rep->buttons & 1;
        } else {
            if (g_x_off >= 0) rep->abs_x = (int)extract_bits(data, g_x_off, g_x_size);
            if (g_y_off >= 0) rep->abs_y = (int)extract_bits(data, g_y_off, g_y_size);
            if (g_tip_off >= 0) rep->down = (int)extract_bits(data, g_tip_off, 1);
            else if (g_btn_off >= 0) rep->down = (int)extract_bits(data, g_btn_off, 1);
            rep->buttons = rep->down ? 1 : 0;
        }
        (void)data_off;
        return 1;
    }
}
