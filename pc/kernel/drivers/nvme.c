#include "nvme.h"

/* NVMe register offsets. */
#define NVME_REG_CAP    0x00
#define NVME_REG_VS     0x08
#define NVME_REG_CC     0x14
#define NVME_REG_CSTS   0x1C
#define NVME_REG_AQA    0x24
#define NVME_REG_ASQ    0x28
#define NVME_REG_ACQ    0x30
#define NVME_REG_DBBASE 0x1000

#define NVME_QENTRIES 64
#define NVME_QMASK (NVME_QENTRIES - 1)

static uint64_t g_bar;
static uint32_t g_dstrd; /* doorbell stride, in 4-byte units shift */
static nvme_info_t g_info;
static int g_ready;

/* Queue storage: static, 4K aligned, in low kernel memory (<4 GB),
   so the controller can DMA to it without an IOMMU on real UEFI
   identity-mapped boots and in QEMU test VMs. */
static __attribute__((aligned(4096))) uint8_t g_admin_sq[NVME_QENTRIES * 64];
static __attribute__((aligned(4096))) uint8_t g_admin_cq[NVME_QENTRIES * 16];
static __attribute__((aligned(4096))) uint8_t g_io_sq[NVME_QENTRIES * 64];
static __attribute__((aligned(4096))) uint8_t g_io_cq[NVME_QENTRIES * 16];
static __attribute__((aligned(4096))) uint8_t g_ident[4096];
static __attribute__((aligned(4096))) uint8_t g_io_buf[4096];

static uint16_t g_admin_sq_tail, g_admin_cq_head;
static uint8_t g_admin_cq_phase = 1;
static uint16_t g_io_sq_tail, g_io_cq_head;
static uint8_t g_io_cq_phase = 1;
static uint16_t g_next_cid = 1;

static uint32_t rd32(uint32_t off) { return *(volatile uint32_t *)(uintptr_t)(g_bar + off); }
static void wr32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(uintptr_t)(g_bar + off) = v; }
static void wr64(uint32_t off, uint64_t v) { *(volatile uint64_t *)(uintptr_t)(g_bar + off) = v; }
static void barrier(void) { __asm__ volatile ("mfence" ::: "memory"); }
static uint32_t doorbell(uint16_t qid, int is_sq)
{
    uint32_t stride = 4u << g_dstrd;
    return NVME_REG_DBBASE + (uint32_t)(2u * qid) * stride + (is_sq ? 0 : stride);
}

static int wait_csts_ready(int want)
{
    uint32_t t = 0;
    while (t < 5000000u) {
        uint32_t csts = rd32(NVME_REG_CSTS);
        if (((csts & 1u) != 0) == (want != 0)) return 1;
        t++;
    }
    return 0;
}

/* Submit one 64-byte admin command (already filled, except CID) and
   wait for its completion entry. Returns 1 if status field is 0. */
static int admin_cmd(uint8_t *cmd)
{
    uint16_t tail = g_admin_sq_tail;
    uint8_t *sq = g_admin_sq + (uintptr_t)tail * 64;
    uint16_t cid = g_next_cid++;
    uint32_t i;
    for (i = 0; i < 64; i++) sq[i] = cmd[i];
    sq[2] = (uint8_t)(cid & 0xFF); sq[3] = (uint8_t)(cid >> 8);
    barrier();
    g_admin_sq_tail = (uint16_t)((tail + 1) & NVME_QMASK);
    wr32(doorbell(0, 1), g_admin_sq_tail);
    /* Poll completion queue. */
    for (i = 0; i < 5000000u; i++) {
        volatile uint8_t *cq = g_admin_cq + (uintptr_t)g_admin_cq_head * 16;
        uint32_t dw3; uint32_t status;
        if (((cq[14] >> 0) & 1u) == g_admin_cq_phase) {
            /* DW3 is bytes 12-15: CID 0-15, phase bit 16, status 17-31. */
            dw3 = (uint32_t)cq[12] | ((uint32_t)cq[13] << 8) | ((uint32_t)cq[14] << 16) | ((uint32_t)cq[15] << 24);
            status = dw3 >> 17;
            g_admin_cq_head = (uint16_t)((g_admin_cq_head + 1) & NVME_QMASK);
            if (g_admin_cq_head == 0) g_admin_cq_phase ^= 1;
            wr32(doorbell(0, 0), g_admin_cq_head);
            return status == 0;
        }
    }
    return 0;
}

static int io_cmd(uint8_t *cmd)
{
    uint16_t tail = g_io_sq_tail;
    uint8_t *sq = g_io_sq + (uintptr_t)tail * 64;
    uint16_t cid = g_next_cid++;
    uint32_t i;
    for (i = 0; i < 64; i++) sq[i] = cmd[i];
    sq[2] = (uint8_t)(cid & 0xFF); sq[3] = (uint8_t)(cid >> 8);
    barrier();
    g_io_sq_tail = (uint16_t)((tail + 1) & NVME_QMASK);
    wr32(doorbell(1, 1), g_io_sq_tail);
    for (i = 0; i < 5000000u; i++) {
        volatile uint8_t *cq = g_io_cq + (uintptr_t)g_io_cq_head * 16;
        uint32_t dw3; uint32_t status;
        if (((cq[14] >> 0) & 1u) == g_io_cq_phase) {
            dw3 = (uint32_t)cq[12] | ((uint32_t)cq[13] << 8) | ((uint32_t)cq[14] << 16) | ((uint32_t)cq[15] << 24);
            status = dw3 >> 17;
            g_io_cq_head = (uint16_t)((g_io_cq_head + 1) & NVME_QMASK);
            if (g_io_cq_head == 0) g_io_cq_phase ^= 1;
            wr32(doorbell(1, 0), g_io_cq_head);
            return status == 0;
        }
    }
    return 0;
}

static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void put64(uint8_t *p, uint64_t v) { int i; for (i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | ((uint64_t)get32(p+4)<<32); }

static void copy_trim(char *dst, const uint8_t *src, int n)
{
    int i, end = n;
    while (end > 0 && (src[end-1] == ' ' || src[end-1] == 0)) end--;
    for (i = 0; i < end; i++) dst[i] = (char)src[i];
    dst[end] = '\0';
}

int nvme_detect(nvme_info_t *out)
{
    pci_device_t dev;
    if (!out) return 0;
    out->bar = 0; out->version = 0; out->cap = 0; out->csts = 0; out->ready = 0;
    if (!pci_find_device(0x01, 0x08, 0x02, &dev)) return 0;
    out->pci = dev;
    out->bar = pci_read_bar64(&dev, 0);
    if (!out->bar) return 0;
    g_bar = out->bar;
    out->version = rd32(NVME_REG_VS);
    out->cap = ((uint64_t)rd32(NVME_REG_CAP + 4) << 32) | rd32(NVME_REG_CAP);
    out->csts = rd32(NVME_REG_CSTS);
    out->ready = (out->csts & 0x01) != 0;
    return 1;
}

int nvme_init(nvme_info_t *out)
{
    pci_device_t dev;
    uint64_t cap;
    uint32_t cc;
    uint8_t cmd[64];
    int i;
    if (!pci_find_device(0x01, 0x08, 0x02, &dev)) return 0;
    g_bar = pci_read_bar64(&dev, 0);
    if (!g_bar) return 0;
    /* Enable bus mastering + memory space in PCI command (offset 0x04).
       Done via config-space I/O ports, same mechanism as pci_read. */
    {
        uint32_t addr = 0x80000000u | ((uint32_t)dev.bus << 16) | ((uint32_t)dev.device << 11) | ((uint32_t)dev.function << 8) | 0x04u;
        uint32_t val;
        __asm__ volatile ("outl %0, %1" : : "a"(addr), "d"((uint16_t)0xCF8u));
        __asm__ volatile ("inl %1, %0" : "=a"(val) : "d"((uint16_t)0xCFCu));
        val |= 0x06u; /* MEM + BUS MASTER */
        __asm__ volatile ("outl %0, %1" : : "a"(addr), "d"((uint16_t)0xCF8u));
        __asm__ volatile ("outl %0, %1" : : "a"(val), "d"((uint16_t)0xCFCu));
    }
    cap = ((uint64_t)rd32(NVME_REG_CAP + 4) << 32) | rd32(NVME_REG_CAP);
    g_dstrd = (uint32_t)((cap >> 32) & 0xFu);
    /* Disable controller, wait for RDY=0. */
    cc = rd32(NVME_REG_CC);
    cc &= ~1u;
    wr32(NVME_REG_CC, cc);
    if (!wait_csts_ready(0)) { return 0; }
    /* Admin queue attributes: 63 entries each (size-1). */
    wr32(NVME_REG_AQA, (uint32_t)((63u << 16) | 63u));
    wr64(NVME_REG_ASQ, (uint64_t)(uintptr_t)g_admin_sq);
    wr64(NVME_REG_ACQ, (uint64_t)(uintptr_t)g_admin_cq);
    g_admin_sq_tail = 0; g_admin_cq_head = 0; g_admin_cq_phase = 1;
    /* CC: EN=1, CSS=0 (NVM), MPS=0 (4K), AMS=0, IOSQES=6, IOCQES=6. */
    cc = (1u) | (6u << 16) | (4u << 20); /* IOSQES=6 (64B SQ), IOCQES=4 (16B CQ), log2 sizes */
    wr32(NVME_REG_CC, cc);
    if (!wait_csts_ready(1)) { return 0; }
    /* Identify controller, CNS=1, PRP1=identify buffer. */
    for (i = 0; i < 64; i++) cmd[i] = 0;
    cmd[0] = 0x06; /* Identify */
    put32(cmd + 40, 1); /* CDW10: CNS=1 */
    put64(cmd + 24, (uint64_t)(uintptr_t)g_ident);
    if (!admin_cmd(cmd)) { return 0; }
    g_info.pci = dev; g_info.bar = g_bar;
    g_info.version = rd32(NVME_REG_VS); g_info.cap = cap;
    g_info.csts = rd32(NVME_REG_CSTS); g_info.ready = 1;
    copy_trim(g_info.model, g_ident + 24, 40);
    copy_trim(g_info.serial, g_ident + 4, 20);
    g_info.max_transfer_pages = 0;
    /* Identify namespace 1, CNS=0, NSID=1. */
    for (i = 0; i < 64; i++) cmd[i] = 0;
    cmd[0] = 0x06;
    put32(cmd + 4, 1); /* NSID */
    put64(cmd + 24, (uint64_t)(uintptr_t)g_ident);
    if (!admin_cmd(cmd)) { return 0; }
    g_info.ns_size_lba = get64(g_ident + 0); /* NSZE */
    {
        uint8_t lbaf = g_ident[26] & 0x0F;
        /* LBA format entry: byte 0 = metadata size, byte 1 = LBADS. */
        uint16_t lbads = (uint16_t)(g_ident[128 + lbaf * 4 + 1] & 0xFF);
        g_info.lba_data_size = (lbads == 0) ? 512u : (uint32_t)(1u << lbads);
        if (g_info.lba_data_size < 512 || g_info.lba_data_size > 4096) g_info.lba_data_size = 512;
    }
    g_info.ns_valid = g_info.ns_size_lba != 0;
    /* Set Features: Number of Queues (0x07), request 1 IO CQ + 1 IO SQ. */
    for (i = 0; i < 64; i++) cmd[i] = 0;
    cmd[0] = 0x09; /* Set Features */
    put32(cmd + 40, 0x07); /* CDW10: Feature ID 0x07 */
    put32(cmd + 44, 0); /* CDW11: NCQ=0 (1 CQ), NSQ=0 (1 SQ) */
    if (!admin_cmd(cmd)) { return 0; }
    /* Create I/O completion queue 1, then submission queue 1. */
    for (i = 0; i < 64; i++) cmd[i] = 0;
    cmd[0] = 0x05; /* Create I/O CQ */
    put32(cmd + 4, 0); /* PRP1 filled below, queue id in CDW10 */
    put32(cmd + 40, (1u) | (63u << 16)); /* QID=1, QSIZE=63 */
    put32(cmd + 44, 0x01); /* PC=1, IEN=0, polled, IV=0 */
    put64(cmd + 24, (uint64_t)(uintptr_t)g_io_cq);
    if (!admin_cmd(cmd)) { return 0; }
    /* Create I/O SQ */
    for (i = 0; i < 64; i++) cmd[i] = 0;
    cmd[0] = 0x01; /* Create I/O SQ */
    put32(cmd + 40, (1u) | (63u << 16)); /* QID=1, QSIZE=63 */
    put32(cmd + 44, (1u) | (1u << 16)); /* CQID=1 in high word, PC=1 in low word */
    put64(cmd + 24, (uint64_t)(uintptr_t)g_io_sq);
    if (!admin_cmd(cmd)) { return 0; }
    g_io_sq_tail = 0; g_io_cq_head = 0; g_io_cq_phase = 1;
    g_ready = 1;
    if (out) *out = g_info;
    return 1;
}

int nvme_read_lba(uint64_t lba, uint8_t *buf)
{
    uint8_t cmd[64];
    int i;
    uint32_t nlb;
    if (!g_ready || !buf) return 0;
    nlb = g_info.lba_data_size / 512;
    if (nlb == 0) nlb = 1;
    /* For a 512-byte LBA, one block. For 4096, 8 blocks, still one PRP page. */
    for (i = 0; i < 64; i++) cmd[i] = 0;
    cmd[0] = 0x02; /* Read */
    put32(cmd + 4, 1); /* NSID */
    put64(cmd + 40, lba); /* SLBA in CDW10/11: bytes 40..47 */
    put32(cmd + 48, nlb - 1); /* NLB, CDW12 */
    put64(cmd + 24, (uint64_t)(uintptr_t)g_io_buf);
    if (!io_cmd(cmd)) return 0;
    for (i = 0; i < (int)g_info.lba_data_size; i++) buf[i] = g_io_buf[i];
    return 1;
}
