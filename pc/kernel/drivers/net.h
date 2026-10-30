#ifndef FISIX_NET_H
#define FISIX_NET_H
#include <stdint.h>
void net_init(void);
void net_poll(void);
int net_has_ip(void);
uint32_t net_ip(void);
/* QEMU user-net fallback only: set 10.0.2.15/10.0.2.2 if DHCP gave no IP.
   Never call this as the primary path on real hardware. */
void net_use_static_fallback(void);
#endif
