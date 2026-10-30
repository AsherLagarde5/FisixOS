/* FisixLink: SIO Normal 8-bit, packet framing, streaming CRC16,
   ACK/NAK. See docs/LINK.md. Master clocks, slave follows. */
#include "gba.h"

static int is_master = 1;
void link_set_master(int m) { is_master = m ? 1 : 0; }

u16 link_crc_update(u16 crc, u8 byte) {
  int b;
  crc ^= (u16)byte << 8;
  for (b = 0; b < 8; b++)
    crc = (crc & 0x8000) ? (u16)((crc << 1) ^ 0x1021) : (u16)(crc << 1);
  return crc;
}
u16 link_crc16(const u8 *data, u32 len) {
  u16 crc = 0xFFFF;
  u32 i;
  for (i = 0; i < len; i++) crc = link_crc_update(crc, data[i]);
  return crc;
}

static int link_xfer_byte(u8 out, u8 *in) {
  int timeout = 200000;
  REG_SIODATA8 = out;
  REG_SIOCNT = SIO_START | (is_master ? SIO_MASTER : 0);
  while ((REG_SIOCNT & SIO_START) && --timeout > 0) { }
  if (timeout <= 0) return -1;
  *in = (u8)REG_SIODATA8;
  return 0;
}

int link_send_packet(u8 type, const u8 *payload, u16 len) {
  u16 crc = 0xFFFF, i;
  u8 dummy;
  if (len > 512) return -1;
  crc = link_crc_update(crc, type);
  crc = link_crc_update(crc, (u8)len);
  crc = link_crc_update(crc, (u8)(len >> 8));
  for (i = 0; i < len; i++) crc = link_crc_update(crc, payload[i]);
  if (link_xfer_byte(0xF1, &dummy)) return -1;
  if (link_xfer_byte(0x58, &dummy)) return -1;
  if (link_xfer_byte(type, &dummy)) return -1;
  if (link_xfer_byte((u8)len, &dummy)) return -1;
  if (link_xfer_byte((u8)(len >> 8), &dummy)) return -1;
  for (i = 0; i < len; i++)
    if (link_xfer_byte(payload[i], &dummy)) return -1;
  if (link_xfer_byte((u8)crc, &dummy)) return -1;
  if (link_xfer_byte((u8)(crc >> 8), &dummy)) return -1;
  return 0;
}

int link_recv_packet(u8 *type, u8 *payload, u16 *len) {
  u8 b1, b2;
  u16 i, l;
  u16 crc = 0xFFFF, got;
  if (link_xfer_byte(0xFF, &b1)) return -1;
  if (b1 != 0xF1) return -1;
  if (link_xfer_byte(0xFF, &b2)) return -1;
  if (b2 != 0x58) return -1;
  if (link_xfer_byte(0xFF, type)) return -1;
  crc = link_crc_update(crc, *type);
  if (link_xfer_byte(0xFF, &b1)) return -1;
  if (link_xfer_byte(0xFF, &b2)) return -1;
  crc = link_crc_update(crc, b1);
  crc = link_crc_update(crc, b2);
  l = (u16)(b1 | (b2 << 8));
  if (l > 512) return -1;
  for (i = 0; i < l; i++) {
    if (link_xfer_byte(0xFF, &payload[i])) return -1;
    crc = link_crc_update(crc, payload[i]);
  }
  if (link_xfer_byte(0xFF, &b1)) return -1;
  if (link_xfer_byte(0xFF, &b2)) return -1;
  got = (u16)(b1 | (b2 << 8));
  if (got != crc) return -1; /* caller sends NAK / retries */
  *len = l;
  return 0;
}
