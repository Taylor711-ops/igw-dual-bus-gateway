#ifndef TEST_UDP_H
#define TEST_UDP_H
#include "ip_addr.h"
#include "pbuf.h"
struct udp_pcb { int unused; };
struct udp_pcb *udp_new(void);
err_t udp_bind(struct udp_pcb *,const ip_addr_t *,u16_t);
void udp_remove(struct udp_pcb *);
err_t udp_sendto(struct udp_pcb *,struct pbuf *,const ip_addr_t *,u16_t);
#endif
