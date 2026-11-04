#ifndef IGW_UDP_PACKET_H
#define IGW_UDP_PACKET_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "igw_driver.h"
typedef struct {
    uint32_t attempts,ok,timeouts,tx_fail,exceptions,mismatches,sw_bad;
    uint32_t udp_submit,udp_errors;
    uint64_t last_good_us;
} igw_udp_metrics;
/* One JSON object per UDP datagram, bounded below a normal Ethernet MTU.
 * Only CRC-valid, matched FC03 replies can be emitted as kind=rtu.
 * Status packets contain no stored/previous register values. */
static inline size_t igw_format_udp(char *out,size_t capacity,int is_rtu,
                                    uint64_t boot,uint32_t sequence,
                                    const igw_udp_metrics *m,igw_counters c,
                                    uint32_t clock_hz,const uint8_t *adu,size_t n,
                                    uint64_t pl_ticks,uint32_t rtt_us,int64_t age_ms)
{
    static const char hex[]="0123456789abcdef";
    char encoded[513];
    size_t i;
    int result;
    if (!out || !capacity || !m || !sequence || !clock_hz || n>256u) return 0;
    if (is_rtu && (!adu || igw_parse_read(adu,n,1,4,NULL)!=1)) return 0;
    if (!is_rtu && (n || adu)) return 0;
    for (i=0;i<n;i++) { encoded[2*i]=hex[adu[i]>>4]; encoded[2*i+1]=hex[adu[i]&15]; }
    encoded[2*n]=0;
    result=snprintf(out,capacity,
        "{\"schema\":\"IGW_RS485_UDP_1\",\"kind\":\"%s\",\"test_data\":true,"
        "\"boot\":\"%016llx\",\"seq\":%lu,\"unit\":1,\"start\":0,\"count\":4,"
        "\"adu\":\"%s\",\"pl_ticks\":%llu,\"clock_hz\":%lu,\"rtt_us\":%lu,\"age_ms\":%lld,"
        "\"attempts\":%lu,\"ok\":%lu,\"timeouts\":%lu,\"tx_fail\":%lu,"
        "\"exceptions\":%lu,\"mismatches\":%lu,\"sw_bad\":%lu,"
        "\"udp_submit\":%lu,\"udp_errors\":%lu,"
        "\"pl\":{\"good\":%lu,\"tx\":%lu,\"crc\":%lu,\"uart\":%lu,\"gap\":%lu,\"overrun\":%lu,\"wd\":%lu}}",
        is_rtu ? "rtu" : "status",(unsigned long long)boot,(unsigned long)sequence,
        encoded,(unsigned long long)pl_ticks,(unsigned long)clock_hz,(unsigned long)rtt_us,
        (long long)age_ms,(unsigned long)m->attempts,(unsigned long)m->ok,
        (unsigned long)m->timeouts,(unsigned long)m->tx_fail,(unsigned long)m->exceptions,
        (unsigned long)m->mismatches,(unsigned long)m->sw_bad,
        (unsigned long)m->udp_submit,(unsigned long)m->udp_errors,
        (unsigned long)c.good,(unsigned long)c.tx,(unsigned long)c.crc,
        (unsigned long)c.uart,(unsigned long)c.gap,(unsigned long)c.overrun,(unsigned long)c.watchdog);
    if (result<0 || (size_t)result>=capacity) return 0;
    return (size_t)result;
}
#endif
