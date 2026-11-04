#ifndef IGW_CAN_RW_PROTOCOL_H
#define IGW_CAN_RW_PROTOCOL_H
#include <stdint.h>
#include <string.h>
#include "igw_rw_protocol.h"
/* Private node1 protocol, classical CAN, NOT CANopen. LE sequence; BE value. */
#define IGW_CAN_RW_REQUEST_ID 0x211u
#define IGW_CAN_RW_REPLY_ID 0x191u
#define IGW_CAN_RW_REVISION 0x0901u
#define IGW_CAN_WRITE_CACHE_SIZE 128u
#define IGW_CAN_WRITE_RETAIN_US 60000000ULL
struct igw_can_write_entry {
    uint8_t used, request[8], reply[8];
    uint64_t until;
};
struct igw_can_write_cache {struct igw_can_write_entry entries[IGW_CAN_WRITE_CACHE_SIZE];};
static inline uint32_t igw_can_seq(const uint8_t p[8]) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
/* Errors E1 malformed, E2 read address, E3 read-only write, E4 seq conflict,
 * E5 no unexpired write-cache slot. All errors perform no register write. */
static inline void igw_can_rw_reply(struct igw_can_write_cache *cache,
        uint16_t regs[32],const uint8_t p[8],uint8_t out[8],uint64_t now)
{
    int free_slot=-1;
    memcpy(out,p,8);out[4]=0xe1;out[6]=out[7]=0;
    for(unsigned i=0;i<IGW_CAN_WRITE_CACHE_SIZE;i++) {
        struct igw_can_write_entry *e=&cache->entries[i];
        if(e->used && now>=e->until)e->used=0;
        if(!e->used) {if(free_slot<0)free_slot=(int)i;continue;}
        if(igw_can_seq(e->request)==igw_can_seq(p)) {
            if(!memcmp(e->request,p,8))memcpy(out,e->reply,8);
            else out[4]=0xe4;
            return;
        }
    }
    if(p[4]==1) {
        if(p[6] || p[7])return;
        if(p[5]>=32) {out[4]=0xe2;return;}
        out[4]=0x81;out[6]=(uint8_t)(regs[p[5]]>>8);out[7]=(uint8_t)regs[p[5]];
    } else if(p[4]==2) {
        if(p[5]<IGW_RW_FIRST || p[5]>IGW_RW_LAST) {out[4]=0xe3;return;}
        if(free_slot<0) {out[4]=0xe5;return;}
        uint16_t value=(uint16_t)(((uint16_t)p[6]<<8)|p[7]);
        igw_rw_store(regs,p[5],value);
        out[4]=0x82;out[6]=p[6];out[7]=p[7];
        struct igw_can_write_entry *e=&cache->entries[free_slot];
        memcpy(e->request,p,8);memcpy(e->reply,out,8);
        e->until=now+IGW_CAN_WRITE_RETAIN_US;e->used=1;
    }
}
#endif
