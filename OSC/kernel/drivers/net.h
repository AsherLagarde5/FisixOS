#ifndef FISIX_NET_H
#define FISIX_NET_H
#include <stdint.h>
void net_init(void);
void net_poll(void);
int net_has_ip(void);
uint32_t net_ip(void);
#endif
