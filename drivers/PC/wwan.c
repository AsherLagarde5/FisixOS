#include "wwan.h"
#include "pci.h"

/* Kernel serial helper lives in kernel.c; keep this driver free of
 * x86 port I/O so the same parsing/framing code can be unit-tested
 * on the host. wwan_init() is the only kernel-side entry point and
 * it uses a tiny local serial writer, mirroring kernel.c. */
static void w_outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static void w_print(const char *s) {
    while (s && *s) {
        uint8_t lsr = 0;
        uint32_t to = 100000;
        do {
            __asm__ volatile ("inb %1, %0" : "=a"(lsr) : "Nd"((uint16_t)(0x3F8 + 5)));
            if (to-- == 0) return;
        } while ((lsr & 0x20) == 0);
        w_outb(0x3F8, (uint8_t)*s);
        s++;
    }
}
static void w_hex16(uint16_t v) {
    const char *d = "0123456789ABCDEF";
    char b[7];
    b[0] = '0'; b[1] = 'x';
    b[2] = d[(v >> 12) & 0xF]; b[3] = d[(v >> 8) & 0xF];
    b[4] = d[(v >> 4) & 0xF]; b[5] = d[v & 0xF]; b[6] = '\0';
    w_print(b);
}

int wwan_vendor_is_known(uint16_t v) {
    return v == WWAN_VENDOR_QUALCOMM || v == WWAN_VENDOR_INTEL ||
           v == WWAN_VENDOR_FIBOCOM || v == WWAN_VENDOR_SIERRA ||
           v == WWAN_VENDOR_TELIT || v == WWAN_VENDOR_QUECTEL;
}

int wwan_detect_pci(wwan_device_t *out) {
    pci_device_t devs[PCI_MAX_DEVICES];
    int count = 0, i;
    if (!out) return 0;
    if (!pci_scan_all(devs, PCI_MAX_DEVICES, &count)) return 0;
    for (i = 0; i < count; i++) {
        int known = wwan_vendor_is_known(devs[i].vendor_id);
        /* Network class, or communication / vendor-specific class
         * from a known modem vendor. Intel 0x8086 alone is NOT
         * enough: it also makes Wi-Fi / Ethernet, so require a
         * non-network class for Intel, or any network class. */
        int cand = 0;
        /* Class 0x02 alone is Ethernet (e.g. e1000 0x100E), NOT WWAN.
         * WWAN on PCI shows as communication (0x07) or vendor-
         * specific (0xFF) from a known modem vendor. */
        if (known && (devs[i].class_code == 0x07u || devs[i].class_code == 0xFFu)) cand = 1;
        if (!cand) continue;
        out->present = 1;
        out->bus = devs[i].bus; out->device = devs[i].device;
        out->function = devs[i].function;
        out->vendor_id = devs[i].vendor_id; out->device_id = devs[i].device_id;
        out->class_code = devs[i].class_code; out->subclass = devs[i].subclass;
        out->likely_wwan = known ? 1 : 0;
        return 1;
    }
    return 0;
}

static void put32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

uint32_t wwan_mbim_build_open(uint8_t *buf, uint32_t buf_len,
                              uint32_t transaction_id, uint32_t max_control_transfer) {
    if (!buf || buf_len < 16) return 0;
    put32le(buf + 0, MBIM_TYPE_OPEN);
    put32le(buf + 4, 16);
    put32le(buf + 8, transaction_id);
    put32le(buf + 12, max_control_transfer);
    return 16;
}

uint32_t wwan_mbim_build_command(uint8_t *buf, uint32_t buf_len,
                                 uint32_t transaction_id,
                                 const uint8_t service_uuid[16],
                                 uint32_t command_id,
                                 const uint8_t *payload, uint32_t payload_len) {
    uint32_t total, i;
    if (!buf || !service_uuid) return 0;
    total = 48 + payload_len;
    if (buf_len < total) return 0;
    put32le(buf + 0, MBIM_TYPE_COMMAND);
    put32le(buf + 4, total);
    put32le(buf + 8, transaction_id);
    /* Fragment header: total fragments = 1, current = 0. */
    put32le(buf + 12, 1);
    put32le(buf + 16, 0);
    for (i = 0; i < 16; i++) buf[20 + i] = service_uuid[i];
    put32le(buf + 36, command_id);
    put32le(buf + 40, payload_len);
    /* Command buffer starts at offset 48 in the spec layout used
     * here (fragment header + UUID + CID + length + buffer). */
    put32le(buf + 44, 48);
    for (i = 0; i < payload_len; i++) buf[48 + i] = payload[i];
    return total;
}

void wwan_status_init(wwan_status_t *st) {
    int i;
    if (!st) return;
    st->signal_dbm = -999; st->signal_csq = 99;
    st->registered = 0; st->roaming = 0;
    for (i = 0; i < 48; i++) { st->ip[i] = 0; st->operator_name[i] = 0; }
}

static int parse_int_after(const char *s, const char *key, int *out) {
    const char *p = s;
    int v, neg = 0;
    if (!s || !key || !out) return 0;
    while (*p) {
        const char *k = key; const char *q = p; int ok = 1;
        while (*k) { if (*q != *k) { ok = 0; break; } q++; k++; }
        if (ok) {
            p = q;
            if (*p == '-') { neg = 1; p++; }
            if (*p < '0' || *p > '9') return 0;
            v = 0; while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            *out = neg ? -v : v;
            return 1;
        }
        p++;
    }
    return 0;
}

int wwan_at_parse_csq(const char *resp, wwan_status_t *st) {
    int csq;
    if (!parse_int_after(resp, "+CSQ: ", &csq)) return 0;
    st->signal_csq = csq;
    st->signal_dbm = (csq >= 0 && csq <= 31) ? (-113 + 2 * csq) : -999;
    return 1;
}

int wwan_at_parse_creg(const char *resp, wwan_status_t *st) {
    int stat;
    /* +CREG: <n>,<stat> -- take the last comma-separated int. */
    const char *p = resp;
    int last = -1, cur, in_num = 0;
    if (!resp) return 0;
    cur = 0;
    while (*p) {
        if (*p >= '0' && *p <= '9') { cur = cur * 10 + (*p - '0'); in_num = 1; }
        else if (in_num) { last = cur; cur = 0; in_num = 0; }
        p++;
    }
    if (in_num) last = cur;
    stat = last;
    if (stat < 0) return 0;
    st->registered = (stat == 1 || stat == 5) ? 1 : 0;
    st->roaming = (stat == 5) ? 1 : 0;
    return 1;
}

int wwan_at_parse_cgpaddr(const char *resp, wwan_status_t *st) {
    const char *q1, *q2;
    int i = 0;
    if (!resp) return 0;
    /* +CGPADDR: 1,"10.0.0.2" -- copy the quoted address. */
    q1 = resp;
    while (*q1 && *q1 != '"') q1++;
    if (!*q1) return 0;
    q1++;
    q2 = q1;
    while (*q2 && *q2 != '"' && i < 47) { st->ip[i++] = *q2; q2++; }
    st->ip[i] = '\0';
    return i > 0 ? 1 : 0;
}

void wwan_init(void) {
    wwan_device_t d;
    w_print("[FisixOS] WWAN/LTE: ");
    if (wwan_detect_pci(&d)) {
        w_print("PCI candidate vendor=");
        w_hex16(d.vendor_id);
        w_print(" device=");
        w_hex16(d.device_id);
        w_print(d.likely_wwan ? " (known WWAN vendor, MBIM/AT stage not yet live)\n"
                              : " (network class, not confirmed WWAN)\n");
    } else {
        w_print("no PCI WWAN candidate found (USB MBIM needs xHCI enumeration first)\n");
    }
}
