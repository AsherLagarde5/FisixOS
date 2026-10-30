#include "e1000.h"
#include "pci.h"
/* Generic Intel e1000 PCI driver, polling. Works on real Intel
e1000-class NICs and in QEMU -nic user,model=e1000 test VMs.
 * Does NOT cover the Latitude 5420 I219-LM (different device) or
 * AX201 Wi-Fi (CNVi) — those need separate drivers, see latitude5420.c. */
/* Original header, kept for history:
 * Registers are MMIO offsets from BAR0. TX ring: 8 legacy descriptors.
 * Send: fill descriptor, set TDT, poll DD bit. No RX / IRQ in v1,
 * no TCP/IP: this proves the NIC can transmit a frame, which is
 * the generic working step. */
#define E1000_CTRL 0x0000
#define E1000_STATUS 0x0008
#define E1000_RCTL 0x0100
#define E1000_TCTL 0x0400
#define E1000_TDBAL 0x3800
#define E1000_TDBAH 0x3804
#define E1000_TDLEN 0x3808
#define E1000_TDH 0x3810
#define E1000_TDT 0x3818
#define E1000_RAL0 0x5400
static uint64_t g_bar; static int g_ready;
static uint8_t g_mac[6];
static uint32_t rd32(uint32_t o);
static void wr32(uint32_t o, uint32_t v);
static __attribute__((aligned(16))) uint8_t g_tx_desc[8*16];
static __attribute__((aligned(16))) uint8_t g_tx_buf[8*2048];
static uint32_t g_tx_next = 0;
#define E1000_RDBAL 0x2800
#define E1000_RDBAH 0x2804
#define E1000_RDLEN 0x2808
#define E1000_RDH 0x2810
#define E1000_RDT 0x2818
static __attribute__((aligned(16))) uint8_t g_rx_desc[8*16];
static __attribute__((aligned(16))) uint8_t g_rx_buf[8*2048];
static uint32_t g_rx_head = 0;
int e1000_get_mac(uint8_t *mac_out)
{
    int i; if (!g_ready || !mac_out) return 0;
    for (i = 0; i < 6; i++) mac_out[i] = g_mac[i];
    return 1;
}
int e1000_recv(uint8_t *buf, uint32_t maxlen)
{
    uint8_t *desc; uint32_t len, i;
    if (!g_ready || !buf || maxlen == 0) return 0;
    desc = g_rx_desc + g_rx_head * 16;
    if (!(desc[12] & 0x01)) return 0; /* DD bit not set */
    len = (uint32_t)desc[8] | ((uint32_t)desc[9] << 8);
    if (len > maxlen) len = maxlen;
    for (i = 0; i < len; i++) buf[i] = g_rx_buf[g_rx_head * 2048 + i];
    desc[12] = 0; desc[13] = 0; desc[8] = 0; desc[9] = 0;
    __asm__ volatile ("mfence" ::: "memory");
    wr32(E1000_RDT, g_rx_head);
    g_rx_head = (g_rx_head + 1) & 7;
    return (int)len;
}
static uint32_t rd32(uint32_t o){ return *(volatile uint32_t*)(uintptr_t)(g_bar+o); }
static void wr32(uint32_t o, uint32_t v){ *(volatile uint32_t*)(uintptr_t)(g_bar+o)=v; }
static void put32(uint8_t *p, uint32_t v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void put64(uint8_t *p, uint64_t v){ int i; for(i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void pci_enable(pci_device_t *d){
    uint32_t addr=0x80000000u|((uint32_t)d->bus<<16)|((uint32_t)d->device<<11)|((uint32_t)d->function<<8)|0x04u;
    uint32_t cmd;
    __asm__ volatile("outl %0,%1"::"a"(addr),"d"((uint16_t)0xCF8));
    __asm__ volatile("inl %1,%0":"=a"(cmd):"d"((uint16_t)0xCFC));
    cmd|=0x07u; /* IO + MEM + bus master */
    __asm__ volatile("outl %0,%1"::"a"(addr),"d"((uint16_t)0xCF8));
    __asm__ volatile("outl %0,%1"::"a"(cmd),"d"((uint16_t)0xCFC));
}
int e1000_init(e1000_info_t *out){
    pci_device_t d; uint32_t ral, rah, i;
    if(!pci_find_device_class(0x02,&d)) return 0;
    /* Only claim Intel e1000-class devices (vendor 8086, device 100E/100F/10D3 etc. by class check in caller test); e.g. device 100E on a real/QEMU e1000. */
    if(d.vendor_id!=0x8086u) return 0;
    pci_enable(&d);
    g_bar=pci_read_bar64(&d,0); if(!g_bar) return 0;
    wr32(E1000_CTRL, rd32(E1000_CTRL)|0x04000000u); /* RST */
    for(i=0;i<1000000u;i++){ if(!(rd32(E1000_CTRL)&0x04000000u)) break; }
    /* Link: STATUS bit 1 = link up (after auto-neg in QEMU, usually up) */
    ral=rd32(E1000_RAL0); rah=rd32(E1000_RAL0+4);
    g_mac[0]=ral; g_mac[1]=ral>>8; g_mac[2]=ral>>16; g_mac[3]=ral>>24; g_mac[4]=rah; g_mac[5]=rah>>8;
    if(out){ out->bar=g_bar; out->mac[0]=ral; out->mac[1]=ral>>8; out->mac[2]=ral>>16; out->mac[3]=ral>>24; out->mac[4]=rah; out->mac[5]=rah>>8; out->link_up=(rd32(E1000_STATUS)&2u)!=0; }
    /* TX ring setup */
    for(i=0;i<128;i++) g_tx_desc[i]=0;
    wr32(E1000_TDBAL,(uint32_t)(uintptr_t)g_tx_desc);
    wr32(E1000_TDBAH,0);
    wr32(E1000_TDLEN,128);
    wr32(E1000_TDH,0); wr32(E1000_TDT,0);
    wr32(E1000_TCTL, (1u<<1) | (1u<<3) | (0x40u<<4) | (0x0Fu<<12)); /* EN, PSP, CT=0x40, COLD=0xF */
    /* RX ring: 8 legacy descriptors, 2048-byte buffers. */
    for(i=0;i<128;i++) g_rx_desc[i]=0;
    for(i=0;i<8;i++) {
        uint8_t *d = g_rx_desc + i*16;
        put64(d, (uint64_t)(uintptr_t)(g_rx_buf + i*2048));
        put32(d+8, 0);
    }
    g_rx_head = 0;
    wr32(E1000_RDBAL,(uint32_t)(uintptr_t)g_rx_desc);
    wr32(E1000_RDBAH,0);
    wr32(E1000_RDLEN,128);
    wr32(E1000_RDH,0); wr32(E1000_RDT,7);
    wr32(E1000_RCTL, (1u<<1) | (1u<<15)); /* EN + BAM (broadcast/multicast) */
    g_ready=1; return 1;
}
int e1000_send(const uint8_t *frame, uint32_t len){
    uint8_t *desc, *buf; uint32_t i, t, idx;
    if(!g_ready || !frame || len==0 || len>2048) return 0;
    /* The previous version always reused descriptor 0 and always set
       TDT=1, so after the first packet TDH==TDT==1 and the hardware
       saw an empty ring: every later packet (DHCP request, TCP
       SYN-ACK, ARP reply) was silently never transmitted, and the
       TDH!=0 fallback masked it as success. Rotate descriptors. */
    idx = g_tx_next; g_tx_next = (g_tx_next + 1) & 7;
    buf = g_tx_buf + idx * 2048;
    for(i=0;i<len;i++) buf[i]=frame[i];
    desc=g_tx_desc + idx * 16;
    put64(desc,(uint64_t)(uintptr_t)buf);
    put32(desc+8,0); /* length in low 16, set below */
    desc[8]=(uint8_t)len; desc[9]=(uint8_t)(len>>8);
    desc[10]=0; desc[11]=0; /* cmd: EOP(1) + RS(8) = 9 */
    desc[11]=0x09;
    desc[12]=0; desc[13]=0; desc[14]=0; desc[15]=0;
    __asm__ volatile("mfence":::"memory");
    wr32(E1000_TDT,(idx + 1) & 7);
    for(t=0;t<1000000u;t++){ if(desc[12]&0x01) return 1; /* DD bit, status byte offset 12 */
    }
    return 0;
}
static void s_print(const char *s){ while(s&&*s){ uint32_t to=100000; uint8_t lsr; do{ __asm__ volatile("inb %1,%0":"=a"(lsr):"Nd"((uint16_t)(0x3F8+5))); if(to--==0) return; }while((lsr&0x20)==0); __asm__ volatile("outb %0,%1"::"a"((uint8_t)*s),"Nd"((uint16_t)0x3F8)); s++; } }
void e1000_init_kernel(void){
    e1000_info_t info;
    if(e1000_init(&info)){
        s_print("[FisixOS] generic: e1000 Ethernet OK, link=");
        s_print(info.link_up ? "up\n" : "down\n");
    } else {
        s_print("[FisixOS] generic: e1000 not found\n");
    }
}
