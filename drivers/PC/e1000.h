#ifndef E1000_H
#define E1000_H
#include <stdint.h>
typedef struct { uint64_t bar; uint8_t mac[6]; int link_up; } e1000_info_t;
int e1000_init(e1000_info_t *out);
int e1000_send(const uint8_t *frame, uint32_t len);
int e1000_recv(uint8_t *buf, uint32_t maxlen);
int e1000_get_mac(uint8_t *mac_out);
void e1000_init_kernel(void);
#endif
