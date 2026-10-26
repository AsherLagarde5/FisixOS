#include "xhci.h"

static uint64_t g_bar;
static uint64_t g_op; /* operational register base */
static uint64_t g_rt; /* runtime register base */

static __attribute__((aligned(64))) uint8_t g_dcbaa[2048];
static __attribute__((aligned(64))) uint8_t g_cmd_ring[64 * 16];
static __attribute__((aligned(64))) uint8_t g_event_ring[64 * 16];
static __attribute__((aligned(64))) uint8_t g_erst[16];

static void put32b(uint8_t *p, uint32_t v);
static void put64b(uint8_t *p, uint64_t v);
static void x_serial(const char *s);
static void x_u32(uint32_t v);
static uint32_t cap32(uint32_t off) { return *(volatile uint32_t *)(uintptr_t)(g_bar + off); }
static uint32_t op32(uint32_t off) { return *(volatile uint32_t *)(uintptr_t)(g_op + off); }
static void op32w(uint32_t off, uint32_t v) { *(volatile uint32_t *)(uintptr_t)(g_op + off) = v; }
static void op64w(uint32_t off, uint64_t v) { *(volatile uint64_t *)(uintptr_t)(g_op + off) = v; }
static uint32_t rt32(uint32_t off) { return *(volatile uint32_t *)(uintptr_t)(g_rt + off); }
static uint64_t rt64(uint32_t off) { return *(volatile uint64_t *)(uintptr_t)(g_rt + off); }
static uint64_t op64(uint32_t off) { return *(volatile uint64_t *)(uintptr_t)(g_op + off); }
static void rt32w(uint32_t off, uint32_t v) { *(volatile uint32_t *)(uintptr_t)(g_rt + off) = v; }
static void rt64w(uint32_t off, uint64_t v) { *(volatile uint64_t *)(uintptr_t)(g_rt + off) = v; }

int xhci_detect(xhci_info_t *out)
{
    pci_device_t dev;
    uint32_t hcsparams1;
    if (!out) return 0;
    out->bar = 0; out->version = 0; out->ports = 0; out->running = 0; out->connected = 0;
    if (!pci_find_device(0x0C, 0x03, 0x30, &dev)) return 0;
    out->pci = dev;
    out->bar = pci_read_bar64(&dev, 0);
    if (!out->bar) return 0;
    g_bar = out->bar;
    out->version = (uint16_t)((cap32(0) >> 16) & 0xFFFF);
    hcsparams1 = cap32(0x04);
    out->ports = (uint8_t)((hcsparams1 >> 24) & 0xFF);
    return 1;
}

int xhci_init(xhci_info_t *out)
{
    pci_device_t dev;
    uint32_t caplength, hcsparams1, maxslots, ports, i;
    uint32_t cmd, sts, t;
    if (!pci_find_device(0x0C, 0x03, 0x30, &dev)) return 0;
    g_bar = pci_read_bar64(&dev, 0);
    if (!g_bar) return 0;
    /* Enable bus mastering + memory space. */
    {
        uint32_t addr = 0x80000000u | ((uint32_t)dev.bus << 16) | ((uint32_t)dev.device << 11) | ((uint32_t)dev.function << 8) | 0x04u;
        uint32_t val;
        __asm__ volatile ("outl %0, %1" : : "a"(addr), "d"((uint16_t)0xCF8u));
        __asm__ volatile ("inl %1, %0" : "=a"(val) : "d"((uint16_t)0xCFCu));
        val |= 0x06u;
        __asm__ volatile ("outl %0, %1" : : "a"(addr), "d"((uint16_t)0xCF8u));
        __asm__ volatile ("outl %0, %1" : : "a"(val), "d"((uint16_t)0xCFCu));
    }
    caplength = cap32(0) & 0xFF;
    g_op = g_bar + caplength;
    /* Runtime register offset is capability offset 0x18. */
    g_rt = g_bar + cap32(0x18);
    hcsparams1 = cap32(0x04);
    maxslots = hcsparams1 & 0xFF;
    ports = (hcsparams1 >> 24) & 0xFF;
    if (ports > 32) ports = 32;
    if (maxslots == 0) maxslots = 32; /* use full hardware MaxSlots (64 on QEMU) like UEFI does */
    /* Halt: RS=0, wait HCHalted (USBSTS bit 0) = 1. */
    cmd = op32(0x00); cmd &= ~1u; op32w(0x00, cmd);
    for (t = 0; t < 5000000u; t++) { if (op32(0x04) & 1u) break; }
    if (!(op32(0x04) & 1u)) return 0;
    /* Reset: HCRESET (USBCMD bit 1) = 1, wait for it to clear and CNR (USBSTS bit 11) = 0. */
    cmd = op32(0x00); cmd |= 2u; op32w(0x00, cmd);
    for (t = 0; t < 5000000u; t++) { if (!(op32(0x00) & 2u)) break; }
    if (op32(0x00) & 2u) return 0;
    for (t = 0; t < 5000000u; t++) { if (!(op32(0x04) & (1u << 11))) break; }
    /* Zero DCBAA, command ring, event ring, ERST. */
    for (i = 0; i < sizeof(g_dcbaa); i++) g_dcbaa[i] = 0;
    for (i = 0; i < sizeof(g_cmd_ring); i++) g_cmd_ring[i] = 0;
    for (i = 0; i < sizeof(g_event_ring); i++) g_event_ring[i] = 0;
    for (i = 0; i < sizeof(g_erst); i++) g_erst[i] = 0;
    /* Command ring link TRB at entry 63, so the ring is circular. */
    {
        uint8_t *link = g_cmd_ring + 63 * 16;
        put64b(link, (uint64_t)(uintptr_t)g_cmd_ring);
        put32b(link + 8, 0);
        put32b(link + 12, 1u | (6u << 10) | (1u << 1));
    }
    /* DCBAAP, CONFIG MaxSlotsEn, command ring with cycle state 1. */
    op64w(0x30, (uint64_t)(uintptr_t)g_dcbaa);
    op32w(0x38, maxslots);
    op64w(0x18, (uint64_t)(uintptr_t)g_cmd_ring | 1u);
    /* Event ring segment table: 1 segment, 64 TRBs, base = event ring. */
    {
        uint64_t *erst = (uint64_t *)(uintptr_t)g_erst;
        erst[0] = (uint64_t)(uintptr_t)g_event_ring;
        *(uint32_t *)(uintptr_t)(g_erst + 8) = 64;
    }
    /* Runtime interrupter 0: ERSTSZ MUST be 1 BEFORE ERSTBA is written.
       QEMU (and the spec's er_reset path) caches the event ring when
       ERSTBA high is written, reading the current ERSTSZ at that moment.
       The old order wrote ERSTSZ=0, then ERSTBA, then ERSTSZ=1, so QEMU
       cached er_start=0, er_size=0 (disabled ring); every later event
       then failed the ERDP bounds check (erdp >= 0+0) and set HCE, with
       no event ever queued. UEFI writes ERSTSZ=1 first, which is why
       UEFI's driver worked on the same QEMU setup. */
    rt32w(0x28, 1); /* ERSTSZ=1 first */
    rt64w(0x30, (uint64_t)(uintptr_t)g_erst); /* ERSTBA: triggers er_reset, caches er_start/er_size */
    rt64w(0x38, (uint64_t)(uintptr_t)g_event_ring); /* ERDP = er_start, in bounds */
    rt32w(0x20, 3); /* IMAN: IP=1 clears any pending interrupt left by UEFI, IE=1 enables */
    /* Clear any pending USBSTS status left by UEFI (EINT/HCE are RWC: write 1 clears). */
    op32w(0x04, (1u << 3) | (1u << 12));
    /* Run: RS=1, wait HCHalted=0. */
    cmd = op32(0x00); cmd |= 1u | 4u; op32w(0x00, cmd); /* RS + INTE */
    for (t = 0; t < 5000000u; t++) { if (!(op32(0x04) & 1u)) break; }
    sts = op32(0x04);
    x_serial("[xhci] after run usbsts="); x_u32(sts); x_serial(" crcr="); x_u32((uint32_t)op64(0x18)); x_serial("\n");
    if (out) {
        out->pci = dev; out->bar = g_bar;
        out->version = (uint16_t)((cap32(0) >> 16) & 0xFFFF);
        out->ports = (uint8_t)ports;
        out->running = (sts & 1u) == 0;
        out->connected = 0;
        for (i = 0; i < ports; i++) {
            uint32_t psc = op32(0x400 + i * 0x10);
            out->portsc[i] = psc;
            if (psc & 1u) out->connected++;
        }
    }
    return (sts & 1u) == 0;
}

/* --- Enumeration stage 1: command ring + Enable Slot + Address Device --- */
static __attribute__((aligned(4096))) uint8_t g_input_ctx[4096];
static __attribute__((aligned(64))) uint8_t g_dev_ctx[32 * 1024];
static __attribute__((aligned(64))) uint8_t g_ep0_ring[64 * 16];
static uint16_t g_cmd_tail, g_cmd_cycle = 1;
static uint16_t g_ev_head, g_ev_cycle = 1;
static uint32_t g_dboff;

static void put32b(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void put64b(uint8_t *p, uint64_t v) { int i; for (i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void db_write(uint32_t idx, uint32_t v) { *(volatile uint32_t *)(uintptr_t)(g_bar + g_dboff + idx * 4) = v; }


static void x_serial(const char *s){ while(s&&*s){ uint32_t to=100000; uint8_t lsr; do{ __asm__ volatile("inb %1,%0":"=a"(lsr):"Nd"((uint16_t)(0x3F8+5))); if(to--==0) return; }while((lsr&0x20)==0); __asm__ volatile("outb %0,%1"::"a"((uint8_t)*s),"Nd"((uint16_t)0x3F8)); s++; } }
static void x_u64(uint64_t v){ char buf[20]; int i=0; const char *hex="0123456789ABCDEF"; if(v==0){ x_serial("0"); return; } while(v>0){ buf[i++]=hex[v&0xF]; v>>=4; } while(i>0){ char c[2]; c[0]=buf[--i]; c[1]=0; x_serial(c); } }
static void x_u32(uint32_t v){ char buf[12]; int i=0; if(v==0){ x_serial("0"); return; } while(v>0){ buf[i++]=(char)('0'+v%10); v/=10; } while(i>0){ char c[2]; c[0]=buf[--i]; c[1]=0; x_serial(c); } }

/* Wait for a command completion event. Returns slot id in *slot_out and
   completion code in *code_out, 1 on success. */
static int cmd_wait_event(uint8_t *slot_out, uint8_t *code_out)
{
    uint32_t i;
    for (i = 0; i < 5000000u; i++) {
        volatile uint8_t *ev = g_event_ring + (uintptr_t)g_ev_head * 16;
        uint32_t dw3 = (uint32_t)ev[12] | ((uint32_t)ev[13] << 8) | ((uint32_t)ev[14] << 16) | ((uint32_t)ev[15] << 24);
        uint32_t type = (dw3 >> 10) & 0x3F;
        if (((dw3 & 1u) == g_ev_cycle) && type == 33) {
            if (slot_out) *slot_out = (uint8_t)(dw3 >> 24);
            if (code_out) *code_out = ev[11];
            g_ev_head = (uint16_t)((g_ev_head + 1) & 63);
            if (g_ev_head == 0) g_ev_cycle ^= 1;
            rt64w(0x38, (uint64_t)(uintptr_t)g_event_ring + (uintptr_t)g_ev_head * 16 | 8u); /* ERDP + EHB */
            return 1;
        }
        /* Skip non-command events with matching cycle (e.g. port status). */
        if ((dw3 & 1u) == g_ev_cycle && type != 0) {
            g_ev_head = (uint16_t)((g_ev_head + 1) & 63);
            if (g_ev_head == 0) g_ev_cycle ^= 1;
            rt64w(0x38, (uint64_t)(uintptr_t)g_event_ring + (uintptr_t)g_ev_head * 16 | 8u); /* ERDP + EHB */
        }
    }
    return 0;
}

static int cmd_send(uint64_t ptr, uint32_t type, uint32_t slot, uint32_t extra_dw3, uint8_t *slot_out, uint8_t *code_out)
{
    uint16_t tail = g_cmd_tail;
    uint8_t *trb;
    uint32_t dw3;
    if (tail >= 63) { /* Link TRB at entry 63 is set up in xhci_enumerate. */
        tail = 0;
        g_cmd_cycle ^= 1;
        g_cmd_tail = 0;
    }
    trb = g_cmd_ring + (uintptr_t)tail * 16;
    put64b(trb, ptr);
    put32b(trb + 8, 0);
    dw3 = g_cmd_cycle | (type << 10) | (slot << 24) | extra_dw3;
    put32b(trb + 12, dw3);
    __asm__ volatile ("mfence" ::: "memory");
    g_cmd_tail = (uint16_t)(tail + 1);
    db_write(0, 0);
    return cmd_wait_event(slot_out, code_out);
}

static uint8_t g_enum_slot = 0;
static uint32_t g_enum_port = 0;
static uint32_t g_enum_speed = 0;
int xhci_enumerate_first(uint8_t *slot_out, uint32_t *portsc_out)
{
    uint32_t ports, i, port = 0, psc = 0, speed = 0, t;
    uint8_t slot = 0, code = 0;
    uint32_t *dcbaa;
    if (!g_bar || !g_op) return 0;
    g_dboff = cap32(0x14);
    /* Find first connected port. */
    ports = cap32(0x04); ports = (ports >> 24) & 0xFF;
    if (ports > 32) ports = 32;
    for (i = 0; i < ports; i++) {
        psc = op32(0x400 + i * 0x10);
        if (psc & 1u) { port = i + 1; break; }
    }
    if (!port) { x_serial("[xhci] no connected port\n"); return 0; }
    if (portsc_out) *portsc_out = psc;
    speed = (psc >> 10) & 0xF;
    /* Reset the port: set PR (bit 4), wait for PR to clear and PED (bit 1). */
    op32w(0x400 + (port - 1) * 0x10, psc | (1u << 4));
    for (t = 0; t < 5000000u; t++) {
        psc = op32(0x400 + (port - 1) * 0x10);
        if (!(psc & (1u << 4)) && (psc & 2u)) break;
    }
    if (!(psc & 2u)) { x_serial("[xhci] port reset failed psc="); x_u32(psc); x_serial("\n"); return 0; }
    /* Command ring link TRB at entry 63: pointer to ring, type 6, TC=1. */
    {
        uint8_t *link = g_cmd_ring + 63 * 16;
        put64b(link, (uint64_t)(uintptr_t)g_cmd_ring);
        put32b(link + 8, 0);
        put32b(link + 12, g_cmd_cycle | (6u << 10) | (1u << 1));
    }
    g_cmd_tail = 0; g_cmd_cycle = 1;
    /* DCBAA must point at real Device Contexts before Enable Slot:
       a null DCBAA entry is a host-controller error on some
       controllers, and the Input Context is a different structure
       (control context first) from a Device Context (slot first),
       so it must never be used as the DCBAA target. */
    {
        uint32_t di;
        for (di = 1; di <= 32; di++)
            *(uint64_t *)(uintptr_t)(g_dcbaa + (uintptr_t)di * 8) = (uint64_t)(uintptr_t)(g_dev_ctx + (di - 1) * 1024);
    }
    /* Enable Slot. */
    if (!cmd_send(0, 9, 0, 0, &slot, &code)) { x_serial("[xhci] enable slot timeout dboff="); x_u32(g_dboff); x_serial(" bar="); x_u32((uint32_t)g_bar); x_serial(" usbcmd="); x_u32(op32(0x00)); x_serial(" usbsts="); x_u32(op32(0x04)); x_serial(" evdw3="); { volatile uint8_t *ev=g_event_ring; uint32_t dw3=(uint32_t)ev[12]|((uint32_t)ev[13]<<8)|((uint32_t)ev[14]<<16)|((uint32_t)ev[15]<<24); x_u32(dw3); } x_serial("\n"); return 0; }
    if (code != 1 || !slot) { x_serial("[xhci] enable slot code="); x_u32(code); x_serial(" slot="); x_u32(slot); x_serial("\n"); return 0; }
    /* Input context: A0 (slot) + A1 (EP0). */
    for (i = 0; i < sizeof(g_input_ctx); i++) g_input_ctx[i] = 0;
    for (i = 0; i < sizeof(g_ep0_ring); i++) g_ep0_ring[i] = 0;
    {
        uint8_t *link = g_ep0_ring + 63 * 16;
        put64b(link, (uint64_t)(uintptr_t)g_ep0_ring);
        put32b(link + 8, 0);
        put32b(link + 12, 1u | (6u << 10) | (1u << 1));
    }
    /* Input Control Context: DW0 = drop flags, DW1 = add flags.
       The old code wrote A0+A1 into DW0, i.e. it DROPPED slot+EP0
       and added nothing, which QEMU rejects with TRB Error on
       Address Device. Add flags go at +4. */
    put32b(g_input_ctx + 0, 0); /* drop nothing */
    put32b(g_input_ctx + 4, (1u << 0) | (1u << 1)); /* add slot + EP0 */
    /* Slot context at +32: speed, context entries=1, root hub port. */
    put32b(g_input_ctx + 32, ((speed & 0xF) << 20) | (1u << 27));
    put32b(g_input_ctx + 32 + 4, (port << 16));
    /* EP0 context at +64: type=control (4), CErr=3, max packet 64 (full/high) or 8 (low). */
    {
        uint32_t mps = (speed == 2) ? 8u : 64u;
        put32b(g_input_ctx + 64 + 4, (3u << 1) | (4u << 3) | (mps << 16));
        put64b(g_input_ctx + 64 + 8, (uint64_t)(uintptr_t)g_ep0_ring | 1u); /* deq at +8, DCS=1 */
        put32b(g_input_ctx + 64 + 16, 8); /* tx_info: avg TRB length */
    }
    (void)dcbaa;
    /* DCBAA already points at the Device Context for this slot;
       Address Device's Input Context is passed only via the TRB. */
    /* Address Device. */
    if (!cmd_send((uint64_t)(uintptr_t)g_input_ctx, 11, slot, 0, &slot, &code)) { x_serial("[xhci] address timeout\n"); return 0; }
    if (code != 1) { x_serial("[xhci] address code="); x_u32(code); x_serial("\n"); return 0; }
    g_enum_slot = slot; g_enum_port = port; g_enum_speed = speed;
    if (slot_out) *slot_out = slot;
    return 1;
}

/* ================= HID stage 2 =================
   Event ring consumer shared by command and transfer waits. An event
   is live when its cycle bit equals the consumer cycle. Consuming
   advances the dequeue pointer and tells the controller (ERDP). */
static int ev_next(uint32_t *type_out, uint8_t *slot_out, uint8_t *ep_out,
                   uint8_t *code_out, uint32_t *resid_out, uint64_t *ptr_out)
{
    volatile uint8_t *ev = g_event_ring + (uintptr_t)g_ev_head * 16;
    uint32_t dw2 = (uint32_t)ev[8] | ((uint32_t)ev[9] << 8) | ((uint32_t)ev[10] << 16) | ((uint32_t)ev[11] << 24);
    uint32_t dw3 = (uint32_t)ev[12] | ((uint32_t)ev[13] << 8) | ((uint32_t)ev[14] << 16) | ((uint32_t)ev[15] << 24);
    uint64_t ptr = 0; int i;
    if ((dw3 & 1u) != g_ev_cycle) return 0;
    for (i = 0; i < 8; i++) ptr |= (uint64_t)ev[i] << (8 * i);
    if (type_out) *type_out = (dw3 >> 10) & 0x3F;
    if (slot_out) *slot_out = (uint8_t)(dw3 >> 24);
    if (ep_out) *ep_out = (uint8_t)((dw3 >> 16) & 0x1F);
    if (code_out) *code_out = (uint8_t)(dw2 >> 24);
    if (resid_out) *resid_out = dw2 & 0xFFFFFFu;
    if (ptr_out) *ptr_out = ptr;
    g_ev_head = (uint16_t)((g_ev_head + 1) & 63);
    if (g_ev_head == 0) g_ev_cycle ^= 1;
    rt64w(0x38, (uint64_t)(uintptr_t)g_event_ring + (uintptr_t)g_ev_head * 16 | 8u); /* ERDP + EHB */
    return 1;
}

/* EP0 transfer ring. After Address Device the EP0 ring dequeue is at
   index 0 with cycle 1, matching the context's DCS=1. */
static __attribute__((aligned(64))) uint8_t g_ep0_data[512];
static uint16_t g_ep0_tail = 0;
static uint8_t g_ep0_cycle = 1;
static void ep0_queue(uint64_t f0, uint32_t f2, uint32_t f3extra)
{
    uint8_t *trb;
    if (g_ep0_tail >= 63) {
        uint8_t *link = g_ep0_ring + 63 * 16;
        put32b(link + 12, (uint32_t)g_ep0_cycle | (6u << 10) | (1u << 1));
        g_ep0_cycle ^= 1; g_ep0_tail = 0;
    }
    trb = g_ep0_ring + (uintptr_t)g_ep0_tail * 16;
    put64b(trb, f0);
    put32b(trb + 8, f2);
    put32b(trb + 12, (uint32_t)g_ep0_cycle | f3extra);
    __asm__ volatile ("mfence" ::: "memory");
    g_ep0_tail++;
}
static int xfer_wait(uint8_t slot, uint8_t ep, uint8_t *code_out, uint32_t *resid_out)
{
    uint32_t i;
    for (i = 0; i < 5000000u; i++) {
        uint32_t type = 0; uint8_t s = 0, e = 0, code = 0; uint32_t resid = 0;
        if (!ev_next(&type, &s, &e, &code, &resid, 0)) continue;
        if (type == 32 && s == slot && e == ep) {
            if (code_out) *code_out = code;
            if (resid_out) *resid_out = resid;
            return 1;
        }
        /* Other events (port status, command completions) are consumed
           and skipped; the transfer event is still to come. */
    }
    return 0;
}
/* One control transfer. Returns bytes transferred, or -1. buf is the
   data-stage buffer (IN: filled, OUT: sent). len==0 means setup+status. */
static int xhci_control(uint8_t slot, uint8_t reqtype, uint8_t req, uint16_t value,
                        uint16_t index, uint16_t len, uint8_t *buf, int is_in)
{
    uint64_t setup = (uint64_t)reqtype | ((uint64_t)req << 8) | ((uint64_t)value << 16)
                   | ((uint64_t)index << 32) | ((uint64_t)len << 48);
    uint32_t trt = (len == 0) ? 0u : (is_in ? 2u : 3u);
    uint8_t code = 0; uint32_t resid = 0;
    ep0_queue(setup, 8, (2u << 10) | (1u << 6) | (trt << 16) | ((len != 0) ? (1u << 4) : 0u));
    if (len != 0) {
        ep0_queue((uint64_t)(uintptr_t)buf, len,
                  (3u << 10) | (1u << 4) | ((uint32_t)is_in << 16));
    }
    /* Status stage direction is opposite the data direction; for a
       no-data transfer the status stage is IN. */
    ep0_queue(0, 0, (4u << 10) | (1u << 5) | ((uint32_t)(!is_in || len == 0) << 16));
    db_write(slot, 1);
    if (!xfer_wait(slot, 1, &code, &resid)) return -1;
    if (code != 1 && code != 13) return -1; /* 1=success, 13=short packet */
    if (len == 0 || !is_in) return (int)len;
    return (int)len - (int)resid;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static __attribute__((aligned(4096))) uint8_t g_cfg_ctx[4096];
static __attribute__((aligned(64))) uint8_t g_int_ring[64 * 16];
static __attribute__((aligned(64))) uint8_t g_int_buf[128];
static uint16_t g_int_tail = 0;
static uint8_t g_int_cycle = 1;
static int g_int_pending = 0;
static xhci_hid_t g_hid;

int xhci_hid_init(xhci_hid_t *out)
{
    uint8_t slot = 0, code = 0;
    uint8_t dev[18]; uint8_t cfg9[9];
    static uint8_t cfg[512]; static uint8_t rep[512];
    uint16_t total = 0, cfgval = 0, report_len = 0;
    uint8_t ifnum = 0, ifproto = 0, epaddr = 0, interval = 0;
    uint16_t mps = 0; uint8_t dci = 0; int found_ep = 0;
    uint32_t lmax_x = 0, lmax_y = 0, size_x = 0, size_y = 0; /* 0 = not seen in descriptor; do NOT default to 32767 here or every mouse looks absolute */
    int kind = XHCI_HID_TABLET, is_digitizer = 0, is_mouse_usage = 0;
    uint32_t i;
    if (!g_bar) return 0;
    if (!g_enum_slot) {
        uint32_t psc = 0;
        if (!xhci_enumerate_first(&slot, &psc)) return 0;
    }
    slot = g_enum_slot;
    /* Device descriptor. */
    if (xhci_control(slot, 0x80, 6, 0x0100, 0, 18, dev, 1) < 18) return 0;
    /* Config descriptor, header first, then the whole thing. */
    if (xhci_control(slot, 0x80, 6, 0x0200, 0, 9, cfg9, 1) < 9) return 0;
    total = rd16(cfg9 + 2); cfgval = cfg9[5];
    if (total == 0 || total > sizeof(cfg)) return 0;
    if (xhci_control(slot, 0x80, 6, 0x0200, 0, total, cfg, 1) < (int)total) return 0;
    /* Walk the config tree: interface -> HID -> endpoint. */
    {
        uint8_t cur_class = 0, cur_proto = 0, cur_if = 0; int in_hid_if = 0;
        for (i = 0; i + 2 <= total;) {
            uint8_t blen = cfg[i]; uint8_t btype = cfg[i + 1];
            if (blen < 2) break;
            if (btype == 4 && blen >= 9) {
                cur_if = cfg[i + 2]; cur_class = cfg[i + 5]; cur_proto = cfg[i + 7];
                in_hid_if = (cur_class == 3);
            } else if (btype == 0x21 && blen >= 9 && in_hid_if) {
                report_len = rd16(cfg + i + 7);
            } else if (btype == 5 && blen >= 7 && in_hid_if && !found_ep) {
                uint8_t addr = cfg[i + 2]; uint8_t attr = cfg[i + 3];
                if ((addr & 0x80) && (attr & 3) == 3) {
                    ifnum = cur_if; ifproto = cur_proto; epaddr = addr;
                    mps = rd16(cfg + i + 4); interval = cfg[i + 6]; found_ep = 1;
                }
            }
            i += blen;
        }
    }
    if (!found_ep || !mps || mps > sizeof(g_int_buf)) return 0;
    dci = (uint8_t)(((epaddr & 0x0F) * 2) + 1);
    /* HID report descriptor, then classify it. */
    if (report_len && report_len <= sizeof(rep)) {
        if (xhci_control(slot, 0x81, 6, 0x2200, ifnum, report_len, rep, 1) >= (int)report_len) {
            uint32_t upage = 0, lmax = 0, rsize = 0;
            uint32_t pend_page[8]; uint32_t pend_val[8]; uint32_t npend = 0;
            for (i = 0; i < report_len;) {
                uint8_t b = rep[i]; uint32_t sz = b & 3; uint32_t tp = (b >> 2) & 3; uint32_t tg = (b >> 4) & 15;
                uint32_t val = 0; uint32_t k;
                if (sz == 3) sz = 4;
                if (i + 1 + sz > report_len) break;
                for (k = 0; k < sz; k++) val |= (uint32_t)rep[i + 1 + k] << (8 * k);
                if (tp == 1 && tg == 0) upage = val;               /* Usage Page */
                else if (tp == 1 && tg == 2) lmax = val;          /* Logical Maximum */
                else if (tp == 1 && tg == 7) rsize = val;         /* Report Size */
                else if (tp == 2 && tg == 0) {                    /* Usage: remember until Input */
                    if (upage == 1 && val == 0x02) is_mouse_usage = 1;
                    if (upage == 0x0D) is_digitizer = 1;
                    if (npend < 8) { pend_page[npend] = upage; pend_val[npend] = val; npend++; }
                } else if (tp == 0 && tg == 8) {                  /* Input: globals now apply to pending usages */
                    for (k = 0; k < npend; k++) {
                        if (pend_page[k] == 1 && pend_val[k] == 0x30) { lmax_x = lmax; size_x = rsize; }
                        if (pend_page[k] == 1 && pend_val[k] == 0x31) { lmax_y = lmax; size_y = rsize; }
                    }
                    npend = 0;
                }
                i += 1 + sz;
            }
        }
    }
    if (ifproto == 2) kind = XHCI_HID_MOUSE;
    else if (ifproto == 1) kind = XHCI_HID_KEYBOARD;
    else if (is_digitizer) kind = XHCI_HID_TOUCH;
    else if (is_mouse_usage) kind = XHCI_HID_MOUSE;
    else kind = XHCI_HID_TABLET;
    /* QEMU's usb-tablet reports Usage Page 1 / Usage Mouse, but with
       16-bit absolute X/Y (logical max 32767), not 8-bit relative
       deltas (logical max 127). A "mouse" with a large X logical max
       is really an absolute tablet; parsing it as relative would
       turn the low byte of X into a wild cursor jump. */
    if (kind == XHCI_HID_MOUSE && (lmax_x > 255 || size_x == 16)) kind = XHCI_HID_TABLET;
    if (kind == XHCI_HID_MOUSE && !lmax_x) lmax_x = 32767;
    /* SET_CONFIGURATION, then Configure Endpoint for the IN endpoint. */
    if (xhci_control(slot, 0x00, 9, cfgval, 0, 0, 0, 0) < 0) return 0;
    for (i = 0; i < sizeof(g_cfg_ctx); i++) g_cfg_ctx[i] = 0;
    for (i = 0; i < sizeof(g_int_ring); i++) g_int_ring[i] = 0;
    {
        uint8_t *link = g_int_ring + 63 * 16;
        put64b(link, (uint64_t)(uintptr_t)g_int_ring);
        put32b(link + 8, 0);
        put32b(link + 12, 1u | (6u << 10) | (1u << 1));
    }
    put32b(g_cfg_ctx + 0, 0); /* drop nothing */
    put32b(g_cfg_ctx + 4, (1u << 0) | (1u << dci)); /* add slot + interrupt EP (same drop/add swap as Address Device) */
    put32b(g_cfg_ctx + 32, ((g_enum_speed & 0xF) << 20) | ((uint32_t)dci << 27));
    put32b(g_cfg_ctx + 32 + 4, (g_enum_port << 16));
    {
        uint32_t off = 32 + (uint32_t)dci * 32;
        put32b(g_cfg_ctx + off, (uint32_t)interval); /* DW0: interval in low byte, not <<16 */
        put32b(g_cfg_ctx + off + 4, (3u << 1) | (7u << 3) | ((uint32_t)mps << 16));
        put64b(g_cfg_ctx + off + 8, (uint64_t)(uintptr_t)g_int_ring | 1u); /* deq at +8, DCS=1 */
        put32b(g_cfg_ctx + off + 16, mps); /* tx_info: avg TRB length */
    }
    if (!cmd_send((uint64_t)(uintptr_t)g_cfg_ctx, 12, slot, 0, &slot, &code)) return 0;
    if (code != 1) return 0;
    g_hid.slot = g_enum_slot; g_hid.vendor_id = rd16(dev + 8); g_hid.product_id = rd16(dev + 10);
    g_hid.interface_number = ifnum; g_hid.interface_protocol = ifproto;
    g_hid.endpoint_address = epaddr; g_hid.dci = dci; g_hid.max_packet = mps;
    g_hid.interval = interval; g_hid.kind = kind;
    g_hid.logical_max_x = lmax_x; g_hid.logical_max_y = lmax_y;
    g_int_tail = 0; g_int_cycle = 1; g_int_pending = 0;
    if (out) *out = g_hid;
    return 1;
}
static void int_queue(void)
{
    uint8_t *trb;
    if (g_int_tail >= 63) {
        uint8_t *link = g_int_ring + 63 * 16;
        put32b(link + 12, (uint32_t)g_int_cycle | (6u << 10) | (1u << 1));
        g_int_cycle ^= 1; g_int_tail = 0;
    }
    trb = g_int_ring + (uintptr_t)g_int_tail * 16;
    put64b(trb, (uint64_t)(uintptr_t)g_int_buf);
    put32b(trb + 8, g_hid.max_packet);
    put32b(trb + 12, (uint32_t)g_int_cycle | (1u << 10) | (1u << 5));
    __asm__ volatile ("mfence" ::: "memory");
    g_int_tail++;
    db_write(g_hid.slot, g_hid.dci);
    g_int_pending = 1;
}
int xhci_hid_poll(xhci_hid_report_t *rep)
{
    uint32_t tries;
    if (!g_hid.slot) return 0;
    if (!g_int_pending) int_queue();
    for (tries = 0; tries < 32; tries++) {
        uint32_t type = 0; uint8_t s = 0, e = 0, code = 0; uint32_t resid = 0;
        if (!ev_next(&type, &s, &e, &code, &resid, 0)) return 0; /* still pending */
        if (type != 32 || s != g_hid.slot || e != g_hid.dci) continue;
        g_int_pending = 0;
        if (code != 1 && code != 13) return -1;
        if (!rep) return 1;
        {
            uint32_t actual = (g_hid.max_packet > resid) ? (g_hid.max_packet - resid) : 0;
            uint32_t i;
            if (actual > sizeof(rep->data)) actual = sizeof(rep->data);
            rep->kind = g_hid.kind; rep->len = (uint8_t)actual;
            rep->dx = 0; rep->dy = 0; rep->buttons = 0; rep->abs_x = 0; rep->abs_y = 0; rep->down = 0;
            rep->logical_max_x = g_hid.logical_max_x; rep->logical_max_y = g_hid.logical_max_y;
            for (i = 0; i < actual; i++) rep->data[i] = g_int_buf[i];
            if (g_hid.kind == XHCI_HID_MOUSE && actual >= 3) {
                rep->buttons = g_int_buf[0]; rep->dx = (int8_t)g_int_buf[1]; rep->dy = (int8_t)g_int_buf[2];
                rep->down = (g_int_buf[0] & 1) != 0;
            } else if ((g_hid.kind == XHCI_HID_TABLET || g_hid.kind == XHCI_HID_TOUCH) && actual >= 5) {
                uint32_t x = (uint32_t)g_int_buf[1] | ((uint32_t)g_int_buf[2] << 8);
                uint32_t y = (uint32_t)g_int_buf[3] | ((uint32_t)g_int_buf[4] << 8);
                rep->buttons = g_int_buf[0]; rep->down = (g_int_buf[0] & 1) != 0;
                rep->abs_x = (int)x; rep->abs_y = (int)y;
            }
            return 1;
        }
    }
    return 0;
}
