#ifndef IGW_DRIVER_H
#define IGW_DRIVER_H
#include <stdint.h>
#include <stddef.h>
#include "igw_protocol.h"

enum {
    IGW_MAGIC=0x000, IGW_VERSION=0x004, IGW_CONTROL=0x00c, IGW_FEED=0x010,
    IGW_WD_CYCLES=0x014, IGW_CLOCK=0x018, IGW_STATUS=0x01c,
    IGW_DIV=0x020, IGW_FORMAT=0x024, IGW_T15=0x028, IGW_T35=0x02c,
    IGW_TX_LEN=0x030, IGW_TX_KICK=0x034, IGW_RX_ACK=0x038,
    IGW_RX_LEN=0x040, IGW_RX_TICK_LO=0x044, IGW_RX_TICK_HI=0x048,
    IGW_GOOD=0x050, IGW_CRC_BAD=0x054, IGW_UART_BAD=0x058,
    IGW_OVERRUN=0x05c, IGW_TX_COUNT=0x060, IGW_WD_EXPIRED=0x064,
    IGW_GAP_BAD=0x068, IGW_RX_DATA=0x100, IGW_TX_DATA=0x200
};
enum { IGW_RX_READY=1, IGW_TX_BUSY=2, IGW_TX_PERMIT=4, IGW_NOT_IDLE=0x1a, IGW_QUIET=0x40 };
enum { IGW_OK=0, IGW_BAD_ABI=-1, IGW_BAD_CLOCK=-2, IGW_IDLE_TIMEOUT=-3,
       IGW_BAD_CONFIG=-4, IGW_TX_TIMEOUT=-5, IGW_BAD_LENGTH=-6, IGW_BAD_CRC=-7 };
typedef struct {
    void *opaque;
    uint32_t (*read)(void *, unsigned);
    void (*write)(void *, unsigned, uint32_t);
    uint64_t (*now_us)(void *);
    void (*service)(void *); /* Optional cooperative network polling. */
    uint32_t clock_hz, watchdog_cycles;
    uint64_t feed_interval_us;
} igw_device;
typedef struct { uint32_t good, crc, uart, overrun, tx, watchdog, gap; } igw_counters;
static inline uint32_t igw_rd(igw_device *d, unsigned r) { return d->read(d->opaque, r); }
static inline void igw_wr(igw_device *d, unsigned r, uint32_t v) { d->write(d->opaque, r, v); }
static inline uint64_t igw_now(igw_device *d) { return d->now_us(d->opaque); }
static inline void igw_delay_us(igw_device *d, uint64_t us)
{
    uint64_t start = igw_now(d);
    while (igw_now(d) - start < us) { if (d->service) d->service(d->opaque); }
}
static inline igw_counters igw_get_counters(igw_device *d)
{
    igw_counters c;
    c.good=igw_rd(d,IGW_GOOD); c.crc=igw_rd(d,IGW_CRC_BAD);
    c.uart=igw_rd(d,IGW_UART_BAD); c.overrun=igw_rd(d,IGW_OVERRUN);
    c.tx=igw_rd(d,IGW_TX_COUNT); c.watchdog=igw_rd(d,IGW_WD_EXPIRED);
    c.gap=igw_rd(d,IGW_GAP_BAD); return c;
}
static inline igw_counters igw_counter_delta(igw_counters a, igw_counters b)
{
    igw_counters c;
    c.good=a.good-b.good; c.crc=a.crc-b.crc; c.uart=a.uart-b.uart;
    c.overrun=a.overrun-b.overrun; c.tx=a.tx-b.tx;
    c.watchdog=a.watchdog-b.watchdog; c.gap=a.gap-b.gap; return c;
}
static inline int igw_configure(igw_device *d, unsigned baud)
{
    uint64_t start, wd_us;
    uint32_t div, t15, t35;
    /* No writes at all until the identity and ABI match. */
    if (igw_rd(d,IGW_MAGIC)!=0x32574749u || igw_rd(d,IGW_VERSION)!=0x00020000u)
        return IGW_BAD_ABI;
    d->clock_hz=igw_rd(d,IGW_CLOCK); d->watchdog_cycles=igw_rd(d,IGW_WD_CYCLES);
    if (d->clock_hz<1000000u || d->clock_hz>250000000u || !d->watchdog_cycles ||
        baud<1200u || baud>1000000u) return IGW_BAD_CLOCK;
    wd_us=(uint64_t)d->watchdog_cycles*1000000u/d->clock_hz;
    if (wd_us < 1000u) return IGW_BAD_CLOCK;
    d->feed_interval_us=wd_us/4u;
    if (d->feed_interval_us>50000u) d->feed_interval_us=50000u;
    igw_wr(d,IGW_CONTROL,0); start=igw_now(d);
    while (igw_rd(d,IGW_STATUS)&IGW_NOT_IDLE) {
        if (igw_now(d)-start>=300000u) return IGW_IDLE_TIMEOUT;
        igw_delay_us(d,50);
    }
    div=(d->clock_hz+baud/2u)/baud;
    if (div<16u) return IGW_BAD_CONFIG;
    t15=baud>19200u ? (uint32_t)(((uint64_t)d->clock_hz*75u+99999u)/100000u)
                    : (uint32_t)(((uint64_t)div*33u+1u)/2u);
    t35=baud>19200u ? (uint32_t)(((uint64_t)d->clock_hz*175u+99999u)/100000u)
                    : (uint32_t)(((uint64_t)div*77u+1u)/2u);
    igw_wr(d,IGW_DIV,div); igw_wr(d,IGW_FORMAT,1); /* 8E1 */
    igw_wr(d,IGW_T15,t15); igw_wr(d,IGW_T35,t35);
    if (igw_rd(d,IGW_DIV)!=div || igw_rd(d,IGW_FORMAT)!=1u ||
        igw_rd(d,IGW_T15)!=t15 || igw_rd(d,IGW_T35)!=t35) return IGW_BAD_CONFIG;
    if (igw_rd(d,IGW_STATUS)&IGW_RX_READY) igw_wr(d,IGW_RX_ACK,1);
    return IGW_OK;
}
/* 1 valid frame, 0 empty; negative error acknowledges/discards that mailbox. */
static inline int igw_take_frame(igw_device *d, uint8_t out[256], size_t *length, uint64_t *ticks)
{
    unsigned i,b;
    uint32_t v;
    if (!(igw_rd(d,IGW_STATUS)&IGW_RX_READY)) return 0;
    *length=igw_rd(d,IGW_RX_LEN);
    if (*length<4u || *length>256u) { igw_wr(d,IGW_RX_ACK,1); return IGW_BAD_LENGTH; }
    *ticks=(uint64_t)igw_rd(d,IGW_RX_TICK_LO) | ((uint64_t)igw_rd(d,IGW_RX_TICK_HI)<<32);
    for (i=0;i<*length;i+=4) {
        v=igw_rd(d,IGW_RX_DATA+i);
        for (b=0;b<4u && i+b<*length;b++) out[i+b]=(uint8_t)(v>>(8u*b));
    }
    igw_wr(d,IGW_RX_ACK,1);
    return igw_valid_adu(out,*length) ? 1 : IGW_BAD_CRC;
}
/* Return only after hardware TX_COUNT advances and DE is released.
 * Authorization is revoked after every frame and on every TX timeout.
 * Keepalive is needed only during the bounded transmission itself. */
static inline int igw_transmit(igw_device *d, const uint8_t *p, size_t n, uint64_t timeout_us)
{
    uint64_t start=igw_now(d), last_feed;
    uint32_t before,v,s;
    unsigned i,b;
    if (!p || n<4u || n>256u) return IGW_BAD_LENGTH;
    while (1) {
        s=igw_rd(d,IGW_STATUS);
        if (!(s&IGW_TX_BUSY) && (s&IGW_QUIET)) break;
        if (igw_now(d)-start>=timeout_us) { igw_wr(d,IGW_CONTROL,0); return IGW_TX_TIMEOUT; }
        igw_delay_us(d,50);
    }
    for (i=0;i<n;i+=4) {
        v=0;
        for (b=0;b<4u && i+b<n;b++) v|=(uint32_t)p[i+b]<<(8u*b);
        igw_wr(d,IGW_TX_DATA+i,v);
    }
    igw_wr(d,IGW_TX_LEN,(uint32_t)n); before=igw_rd(d,IGW_TX_COUNT);
    s=igw_rd(d,IGW_STATUS);
    if ((s&IGW_TX_BUSY) || !(s&IGW_QUIET)) {
        igw_wr(d,IGW_CONTROL,0); return IGW_TX_TIMEOUT;
    }
    igw_wr(d,IGW_FEED,0x4b49434bu); igw_wr(d,IGW_CONTROL,1);
    last_feed=igw_now(d); igw_wr(d,IGW_TX_KICK,1);
    while (1) {
        uint64_t now=igw_now(d);
        if (igw_rd(d,IGW_TX_COUNT)!=before && !(igw_rd(d,IGW_STATUS)&IGW_TX_BUSY)) {
            igw_wr(d,IGW_CONTROL,0); return IGW_OK;
        }
        if (now-start>=timeout_us) { igw_wr(d,IGW_CONTROL,0); return IGW_TX_TIMEOUT; }
        if (now-last_feed>=d->feed_interval_us) {
            igw_wr(d,IGW_FEED,0x4b49434bu); last_feed=now;
        }
        igw_delay_us(d,50);
    }
}
#endif
