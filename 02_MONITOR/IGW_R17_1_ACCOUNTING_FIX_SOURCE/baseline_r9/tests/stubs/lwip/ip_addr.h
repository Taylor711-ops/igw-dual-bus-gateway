#ifndef TEST_IP_ADDR_H
#define TEST_IP_ADDR_H
#include <stdint.h>
typedef struct { uint32_t addr; } ip_addr_t;
extern const ip_addr_t test_any_address;
#define IP_ADDR_ANY (&test_any_address)
int ipaddr_aton(const char *,ip_addr_t *);
#endif
