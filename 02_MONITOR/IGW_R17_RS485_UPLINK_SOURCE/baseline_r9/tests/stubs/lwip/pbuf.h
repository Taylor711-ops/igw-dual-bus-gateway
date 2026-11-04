#ifndef TEST_PBUF_H
#define TEST_PBUF_H
#include <stdint.h>
typedef int8_t err_t;
typedef uint16_t u16_t;
#define ERR_OK 0
#define ERR_MEM -1
typedef enum { PBUF_TRANSPORT } pbuf_layer;
typedef enum { PBUF_RAM } pbuf_type;
struct pbuf { void *payload; u16_t len; };
struct pbuf *pbuf_alloc(pbuf_layer,u16_t,pbuf_type);
err_t pbuf_take(struct pbuf *,const void *,u16_t);
uint8_t pbuf_free(struct pbuf *);
#endif
