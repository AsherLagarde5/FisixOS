#include "net.h"
#include "e1000.h"

static void s_print(const char *s){ while(s&&*s){ uint32_t to=100000; uint8_t lsr; do{ __asm__ volatile("inb %1,%0":"=a"(lsr):"Nd"((uint16_t)(0x3F8+5))); if(to--==0) return; }while((lsr&0x20)==0); __asm__ volatile("outb %0,%1"::"a"((uint8_t)*s),"Nd"((uint16_t)0x3F8)); s++; } }
static void s_u32(uint32_t v){ char buf[12]; int i=0; if(v==0){ s_print("0"); return; } while(v>0){ buf[i++]=(char)('0'+v%10); v/=10; } while(i>0){ char c[2]; c[0]=buf[--i]; c[1]=0; s_print(c); } }
static void s_ip(uint32_t ip){ s_u32(ip&0xFF); s_print("."); s_u32((ip>>8)&0xFF); s_print("."); s_u32((ip>>16)&0xFF); s_print("."); s_u32((ip>>24)&0xFF); }

static uint8_t g_mac[6];
static uint32_t g_ip = 0; /* little-endian byte order: byte0 = first octet */
static uint32_t g_gw = 0;
static int g_ready = 0;
static int g_dhcp_stage = 0; /* 0=done/static, 1=waiting offer, 2=waiting ack */
static uint16_t g_ip_id = 0;
static uint32_t g_tcp_seq = 1000;
static int g_tcp_state = 0; /* 0=closed, 1=synack sent, 2=established */

static uint32_t mk_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{ return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24); }
static void put_ip(uint8_t *p, uint32_t ip){ p[0]=ip&0xFF; p[1]=(ip>>8)&0xFF; p[2]=(ip>>16)&0xFF; p[3]=(ip>>24)&0xFF; }
static uint32_t get_ip(const uint8_t *p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static void put16(uint8_t *p, uint16_t v){ p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static uint16_t get16(const uint8_t *p){ return (uint16_t)((p[0]<<8)|p[1]); }
static uint16_t csum(const uint8_t *p, uint32_t len)
{
    uint32_t sum = 0; uint32_t i;
    for (i = 0; i + 1 < len; i += 2) sum += (uint32_t)((p[i] << 8) | p[i+1]);
    if (len & 1) sum += (uint32_t)(p[len-1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}
static void send_frame(uint8_t *f, uint32_t len)
{
    uint32_t i; /* Ethernet minimum is 60 bytes; pad with zeros. */
    if (len < 60) { for (i = len; i < 60; i++) f[i] = 0; len = 60; }
    e1000_send(f, len);
}
static void handle_arp(uint8_t *f, uint32_t len)
{
    uint8_t *a; uint32_t target;
    if (len < 42) return;
    a = f + 14;
    if (get16(a) != 1 || get16(a+2) != 0x0800 || a[4] != 6 || a[5] != 4) return;
    if (get16(a+6) != 1) return; /* only answer requests */
    target = get_ip(a+24);
    if (target != g_ip) return;
    /* Reply in place. ARP layout after the Ethernet header:
       hwtype 0-1, proto 2-3, hlen 4, plen 5, opcode 6-7,
       sender MAC 8-13, sender IP 14-17, target MAC 18-23,
       target IP 24-27. */
    uint8_t sender_mac[6]; uint8_t sender_ip[4]; int i;
    for (i = 0; i < 6; i++) sender_mac[i] = a[8+i];
    for (i = 0; i < 4; i++) sender_ip[i] = a[14+i];
    for (i = 0; i < 6; i++) { f[i] = sender_mac[i]; f[6+i] = g_mac[i]; }
    for (i = 0; i < 6; i++) a[8+i] = g_mac[i];
    put_ip(a+14, g_ip);
    for (i = 0; i < 6; i++) a[18+i] = sender_mac[i];
    for (i = 0; i < 4; i++) a[24+i] = sender_ip[i];
    put16(a+6, 2);
    send_frame(f, 42);
}
static void handle_icmp(uint8_t *f, uint32_t len, uint8_t *src_mac, uint32_t src_ip, uint8_t *ip, uint32_t ip_len)
{
    uint8_t *ic = ip + 20;
    if (ip_len < 20 + 8 || ic[0] != 8) return; /* echo request only */
    uint8_t out[2048]; uint32_t i;
    if (len > sizeof(out)) return;
    for (i = 0; i < len; i++) out[i] = f[i];
    for (i = 0; i < 6; i++) { out[i] = src_mac[i]; out[6+i] = g_mac[i]; }
    uint8_t *oip = out + 14;
    put_ip(oip+12, g_ip); put_ip(oip+16, src_ip);
    put16(oip+10, 0); put16(oip+10, csum(oip, 20));
    uint8_t *oic = oip + 20;
    oic[0] = 0; oic[1] = 0; put16(oic+2, 0);
    put16(oic+2, csum(oic, ip_len - 20));
    send_frame(out, len);
}
static void handle_udp(uint8_t *f, uint32_t len, uint8_t *src_mac, uint32_t src_ip, uint8_t *ip, uint32_t ip_len)
{
    uint8_t *u = ip + 20; uint16_t sport, dport;
    if (ip_len < 28) return;
    sport = get16(u); dport = get16(u+2);
    if (dport != 7) return; /* UDP echo service only */
    uint8_t out[2048]; uint32_t i;
    if (len > sizeof(out)) return;
    for (i = 0; i < len; i++) out[i] = f[i];
    for (i = 0; i < 6; i++) { out[i] = src_mac[i]; out[6+i] = g_mac[i]; }
    uint8_t *oip = out + 14;
    put_ip(oip+12, g_ip); put_ip(oip+16, src_ip);
    put16(oip+10, 0); put16(oip+10, csum(oip, 20));
    uint8_t *ou = oip + 20;
    put16(ou, 7); put16(ou+2, sport); put16(ou+6, 0); /* UDP checksum 0 = none */
    send_frame(out, len);
}
static uint16_t tcp_csum(uint32_t src, uint32_t dst, const uint8_t *tcp, uint32_t tcp_len)
{
    uint32_t sum = 0, i;
    /* src/dst are stored little-endian (byte0 = first octet), but the
       pseudo-header checksum sums network-order words: (a<<8)|b and
       (c<<8)|d for an address a.b.c.d. The previous version summed
       the swapped words, so every TCP reply had a bad checksum and
       was dropped by the peer. */
    sum += ((src & 0xFF) << 8) | ((src >> 8) & 0xFF);
    sum += ((src >> 16) & 0xFF) << 8 | ((src >> 24) & 0xFF);
    sum += ((dst & 0xFF) << 8) | ((dst >> 8) & 0xFF);
    sum += ((dst >> 16) & 0xFF) << 8 | ((dst >> 24) & 0xFF);
    sum += 6; sum += tcp_len;
    for (i = 0; i + 1 < tcp_len; i += 2) sum += (uint32_t)((tcp[i] << 8) | tcp[i+1]);
    if (tcp_len & 1) sum += (uint32_t)(tcp[tcp_len-1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}
static void handle_tcp(uint8_t *f, uint32_t len, uint8_t *src_mac, uint32_t src_ip, uint8_t *ip, uint32_t ip_len)
{
    uint8_t *t = ip + 20; uint32_t tcp_len, data_len;
    uint16_t sport, dport; uint32_t seq, ack; uint8_t flags;
    if (ip_len < 40) return;
    sport = get16(t); dport = get16(t+2);
    seq = ((uint32_t)t[4]<<24)|((uint32_t)t[5]<<16)|((uint32_t)t[6]<<8)|t[7];
    flags = t[13];
    tcp_len = ip_len - 20;
    data_len = tcp_len - ((t[12] >> 4) * 4);
    (void)data_len; (void)dport;
    uint8_t out[256];
    uint32_t i;
    for (i = 0; i < 6; i++) { out[i] = src_mac[i]; out[6+i] = g_mac[i]; }
    out[12] = 0x08; out[13] = 0x00;
    uint8_t *oip = out + 14;
    oip[0] = 0x45; oip[1] = 0; put16(oip+2, 40); put16(oip+4, g_ip_id++);
    put16(oip+6, 0); oip[8] = 64; oip[9] = 6; put16(oip+10, 0);
    put_ip(oip+12, g_ip); put_ip(oip+16, src_ip);
    put16(oip+10, csum(oip, 20));
    uint8_t *ot = oip + 20;
    put16(ot, dport); put16(ot+2, sport);
    ack = seq;
    if (flags & 0x02) { /* SYN -> SYN+ACK */
        put16(ot+12, 0x5000); /* data offset 5, no options */
        ot[13] = 0x12; g_tcp_state = 1; g_tcp_seq = 1000;
        put16(ot+14, 8192); put16(ot+16, 0); put16(ot+18, 0);
        ot[4] = (uint8_t)(g_tcp_seq >> 24); ot[5] = (uint8_t)(g_tcp_seq >> 16); ot[6] = (uint8_t)(g_tcp_seq >> 8); ot[7] = (uint8_t)g_tcp_seq;
        ack = seq + 1;
    } else if (flags & 0x01) { /* FIN -> FIN+ACK */
        put16(ot+12, 0x5000); ot[13] = 0x11; g_tcp_state = 0;
        ot[4] = (uint8_t)(g_tcp_seq >> 24); ot[5] = (uint8_t)(g_tcp_seq >> 16); ot[6] = (uint8_t)(g_tcp_seq >> 8); ot[7] = (uint8_t)g_tcp_seq;
        ack = seq + 1;
    } else if ((flags & 0x10) && data_len == 0) { /* bare ACK, no payload */
        if (g_tcp_state == 1) g_tcp_state = 2;
        return; /* no reply needed for a bare ACK */
        /* Note: PSH+ACK (flags 0x18) has the ACK bit set too, so it
           must NOT match here: it carries GET data and falls through
           to the HTTP reply branch below. Previously it was swallowed
           here, which is why the client retransmitted GET. */
    } else if (flags & 0x04) { g_tcp_state = 0; return; }
    else { /* PSH/data -> reply with a tiny HTTP response, PSH+ACK */
        const char *resp = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 20\r\n\r\nHello from FisixOS!\n";
        uint32_t rl = 0; while (resp[rl]) rl++;
        uint8_t out2[512]; uint32_t i2;
        for (i2 = 0; i2 < 14 + 40; i2++) out2[i2] = out[i2];
        /* Rewrite IP total length and TCP header for the data packet. */
        put16(out2+14+2, (uint16_t)(40 + rl));
        put16(out2+14+10, 0); put16(out2+14+10, csum(out2+14, 20));
        uint8_t *ot2 = out2 + 14 + 20;
        put16(ot2+12, 0x5000); ot2[13] = 0x18; /* PSH+ACK */
        ot2[4] = (uint8_t)((g_tcp_seq + 1) >> 24); ot2[5] = (uint8_t)((g_tcp_seq + 1) >> 16); ot2[6] = (uint8_t)((g_tcp_seq + 1) >> 8); ot2[7] = (uint8_t)(g_tcp_seq + 1);
        ack = seq + data_len;
        ot2[8] = (uint8_t)(ack >> 24); ot2[9] = (uint8_t)(ack >> 16); ot2[10] = (uint8_t)(ack >> 8); ot2[11] = (uint8_t)ack;
        put16(ot2+14, 8192); put16(ot2+16, 0); put16(ot2+18, 0);
        for (i2 = 0; i2 < rl; i2++) ot2[20+i2] = (uint8_t)resp[i2];
        put16(ot2+16, tcp_csum(g_ip, src_ip, ot2, 20 + rl));
        send_frame(out2, 14 + 40 + rl);
        g_tcp_seq = g_tcp_seq + 1 + rl;
        return;
    }
    ot[8] = (uint8_t)(ack >> 24); ot[9] = (uint8_t)(ack >> 16); ot[10] = (uint8_t)(ack >> 8); ot[11] = (uint8_t)ack;
    put16(ot+16, tcp_csum(g_ip, src_ip, ot, 20));
    (void)len; (void)f;
    send_frame(out, 14 + 40);
}

/* Returns 1 if this frame was a DHCP message, filling yiaddr/server. */
static int dhcp_parse(const uint8_t *f, uint32_t len, uint32_t *yiaddr, uint32_t *server, uint8_t *msgtype)
{
    const uint8_t *ip, *u, *d; uint32_t i;
    if (len < 14 + 20 + 8 + 240) return 0;
    ip = f + 14; if (ip[9] != 17) return 0;
    u = ip + 20; if (get16(u+2) != 68) return 0;
    d = u + 8;
    if (d[0] != 2) return 0;
    if (d[4] != 0x12 || d[5] != 0x34 || d[6] != 0x56 || d[7] != 0x78) return 0; /* our xid */
    *yiaddr = get_ip(d + 16); *msgtype = 0;
    i = 240; /* options start: 236 fixed + 4 magic */
    if (!(d[236] == 99 && d[237] == 130 && d[238] == 83 && d[239] == 99)) return 0;
    while (i + 2 <= (len - 14 - 20 - 8) && d[i] != 255) {
        uint8_t code = d[i], ln = d[i+1];
        if (code == 0) { i++; continue; }
        if (i + 2 + ln > (len - 14 - 20 - 8)) break;
        if (code == 53 && ln >= 1) *msgtype = d[i+2];
        if (code == 54 && ln >= 4) *server = get_ip(d + i + 2);
        if (code == 3 && ln >= 4) g_gw = get_ip(d + i + 2);
        i += 2 + ln;
    }
    return 1;
}
static void dhcp_send(uint8_t msgtype, uint32_t server_ip, uint32_t req_ip)
{
    uint8_t f[1024]; uint32_t i;
    for (i = 0; i < sizeof(f); i++) f[i] = 0;
    for (i = 0; i < 6; i++) f[i] = 0xFF;
    for (i = 0; i < 6; i++) f[6+i] = g_mac[i];
    f[12] = 0x08; f[13] = 0x00;
    uint8_t *ip = f + 14;
    ip[0] = 0x45; ip[1] = 0; put16(ip+4, g_ip_id++); put16(ip+6, 0);
    ip[8] = 64; ip[9] = 17; put16(ip+10, 0);
    put_ip(ip+12, 0); put_ip(ip+16, 0xFFFFFFFFu);
    uint8_t *u = ip + 20;
    put16(u, 68); put16(u+2, 67);
    uint8_t *d = u + 8;
    d[0] = 1; d[1] = 1; d[2] = 6; d[3] = 0;
    d[4] = 0x12; d[5] = 0x34; d[6] = 0x56; d[7] = 0x78;
    put16(d+8, 0); put16(d+10, 0x8000);
    for (i = 0; i < 6; i++) d[28+i] = g_mac[i];
    d[236] = 99; d[237] = 130; d[238] = 83; d[239] = 99;
    uint32_t o = 240;
    d[o++] = 53; d[o++] = 1; d[o++] = msgtype;
    if (msgtype == 3) {
        d[o++] = 50; d[o++] = 4; put_ip(d+o, req_ip); o += 4;
        d[o++] = 54; d[o++] = 4; put_ip(d+o, server_ip); o += 4;
    }
    d[o++] = 55; d[o++] = 3; d[o++] = 1; d[o++] = 3; d[o++] = 6;
    d[o++] = 255;
    uint32_t dhcp_len = o;
    put16(u+4, (uint16_t)(8 + dhcp_len)); put16(u+6, 0);
    put16(ip+2, (uint16_t)(20 + 8 + dhcp_len));
    put16(ip+10, csum(ip, 20));
    s_print("[net] dhcp send type "); s_u32(msgtype); s_print("\n");
    send_frame(f, 14 + 20 + 8 + dhcp_len);
}
static int dhcp_wait(uint8_t want, uint32_t *yiaddr, uint32_t *server)
{
    uint32_t tries; uint8_t buf[2048];
    for (tries = 0; tries < 8000000u; tries++) {
        int n = e1000_recv(buf, sizeof(buf));
        if (n > 0) {
            s_print("[net] dhcp_wait got frame len "); s_u32((uint32_t)n); s_print(" eth "); s_u32(get16(buf+12)); s_print("\n");
            uint32_t y = 0, s = 0; uint8_t mt = 0;
            if (dhcp_parse(buf, (uint32_t)n, &y, &s, &mt) && mt == want) {
                *yiaddr = y; *server = s; return 1;
            }
            /* Other traffic during DHCP is handled normally below. */
            if (get16(buf+12) == 0x0806) handle_arp(buf, (uint32_t)n);
        }
    }
    return 0;
}
static void handle_frame(uint8_t *f, uint32_t len)
{
    uint16_t eth; uint8_t *ip; uint32_t src_ip, ip_len;
    if (len < 14) return;
    eth = get16(f+12);
    if (eth == 0x0806) { handle_arp(f, len); return; }
    if (eth != 0x0800 || len < 34) return;
    ip = f + 14;
    if ((ip[0] >> 4) != 4) return;
    src_ip = get_ip(ip+12);
    if (get_ip(ip+16) != g_ip) return;
    ip_len = get16(ip+2);
    if (ip_len < 20 || 14 + ip_len > len) return;
    if (ip[9] == 1) handle_icmp(f, len, f+6, src_ip, ip, ip_len);
    else if (ip[9] == 17) handle_udp(f, len, f+6, src_ip, ip, ip_len);
    else if (ip[9] == 6) handle_tcp(f, len, f+6, src_ip, ip, ip_len);
}

void net_init(void)
{
    e1000_info_t info;
    if (!e1000_init(&info)) { s_print("[FisixOS] net: e1000 not found\n"); return; }
    if (!e1000_get_mac(g_mac)) return;
    g_ready = 1;
    /* Static fallback first (QEMU user-net's usual .15), then DHCP
       asynchronously in net_poll: the offer often arrives after the
       first blocking wait would have timed out, and blocking the
       whole boot on DHCP is wrong anyway. */
    g_ip = mk_ip(10, 0, 2, 15); g_gw = mk_ip(10, 0, 2, 2);
    dhcp_send(1, 0, 0);
    g_dhcp_stage = 1;
    s_print("[FisixOS] net: static ip ");
    s_ip(g_ip); s_print(", dhcp discover sent\n");
}
static void net_dhcp_poll(uint8_t *f, uint32_t len)
{
    uint32_t y = 0, sv = 0; uint8_t mt = 0;
    if (g_dhcp_stage == 0) return;
    if (!dhcp_parse(f, len, &y, &sv, &mt)) return;
    if (g_dhcp_stage == 1 && mt == 2 && y) {
        dhcp_send(3, sv, y);
        g_dhcp_stage = 2;
        s_print("[net] dhcp offer, requesting\n");
    } else if (g_dhcp_stage == 2 && mt == 5 && y) {
        g_ip = y; g_dhcp_stage = 0;
        s_print("[net] dhcp ack, ip "); s_ip(g_ip); s_print("\n");
    }
}
void net_poll(void)
{
    uint8_t buf[2048]; int n; static int dbg_frames = 0;
    if (!g_ready) return;
    n = e1000_recv(buf, sizeof(buf));
    if (n > 0) { if (get16(buf+12)==0x0800 && buf[23]==17 && get16(buf+34+2)==68) { net_dhcp_poll(buf, (uint32_t)n); return; } if (dbg_frames < 20) { s_print("[net] poll frame len "); s_u32((uint32_t)n); s_print(" eth "); s_u32(get16(buf+12)); if (get16(buf+12)==0x0800 && n>=34) { s_print(" proto "); s_u32(buf[23]); s_print(" src "); s_ip(get_ip(buf+26)); s_print(" dst "); s_ip(get_ip(buf+30)); if (buf[23]==6 && n>=54) { s_print(" tcp flags "); s_u32(buf[47]); } if (buf[23]==17 && n>=42) { s_print(" udp "); s_u32(get16(buf+34)); s_print("->"); s_u32(get16(buf+36)); } } s_print("\n"); dbg_frames++; } handle_frame(buf, (uint32_t)n); }
}
int net_has_ip(void) { return g_ready; }
uint32_t net_ip(void) { return g_ip; }
