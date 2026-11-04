#ifndef TEST_ETHERNETIF_H
#define TEST_ETHERNETIF_H
struct netif { int unused; };
void netif_config(void);
void ethernetif_input(struct netif *);
#endif
