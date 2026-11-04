#ifndef IGW_NODES_H
#define IGW_NODES_H
#include "igw_can_rw_protocol.h"
/* One firmware can serve a contiguous set of virtual nodes. For physical
 * multidrop use COUNT=1 and a distinct FIRST_NODE on each physical slave.
 * Addresses and register banks are independent; buses share a bank only when
 * their node address is equal. No broadcasts and no persistent writes. */
#ifndef IGW_FIRST_NODE
#define IGW_FIRST_NODE 1
#endif
#ifndef IGW_NODE_COUNT
#define IGW_NODE_COUNT 2
#endif
#if IGW_FIRST_NODE < 1 || IGW_NODE_COUNT < 1 || IGW_FIRST_NODE+IGW_NODE_COUNT-1 > 16
#error R13 nodes must be in 1..16
#endif
struct igw_nodes {
    uint16_t regs[IGW_NODE_COUNT][32];
    struct igw_can_write_cache cache[IGW_NODE_COUNT];
};
static inline int igw_node_index(unsigned unit) {
    return unit>=IGW_FIRST_NODE && unit<IGW_FIRST_NODE+IGW_NODE_COUNT ?
        (int)(unit-IGW_FIRST_NODE) : -1;
}
static inline void igw_nodes_init(struct igw_nodes *n) {
    memset(n,0,sizeof *n);
    for(unsigned i=0;i<IGW_NODE_COUNT;i++) {
        uint16_t *r=n->regs[i];r[0]=0x1234;r[1]=IGW_FIRST_NODE+i;
        r[4]=IGW_RW_REVISION;r[6]=0xffff;r[8]=IGW_CAN_RW_REVISION;
        r[9]=0x0d01;r[10]=IGW_NODE_COUNT;
    }
}
static inline int igw_nodes_can(struct igw_nodes *n,unsigned request_id,
        const uint8_t p[8],uint8_t out[8],uint64_t now) {
    if(request_id<0x211 || request_id>0x220)return 0;
    unsigned unit=request_id-0x210;int i=igw_node_index(unit);
    if(i<0)return 0;
    n->regs[i][3]=(uint16_t)(now/1000000);
    igw_can_rw_reply(&n->cache[i],n->regs[i],p,out,now);
    return (int)(0x190+unit);
}
static inline size_t igw_nodes_rtu(struct igw_nodes *n,const uint8_t *p,
        size_t size,uint8_t out[256],uint64_t now) {
    if(!igw_valid_adu(p,size))return 0;
    int i=igw_node_index(p[0]);if(i<0)return 0;
    n->regs[i][2]=(uint16_t)(n->regs[i][2]+1);
    n->regs[i][3]=(uint16_t)(now/1000000);
    return igw_slave_rw_reply(p,size,p[0],n->regs[i],out);
}
#endif
