#ifndef IGW_UPLINK_PROTOCOL_H
#define IGW_UPLINK_PROTOCOL_H
#include <stdint.h>
#include <string.h>
/* R16: latest-state uploads to a separate volatile host register 16 per unit.
 * Classic standard CAN, DLC=8. HELLO/WELCOME establish a host-owned epoch.
 * DATA/ACK = epoch32LE, sequence16LE, value16BE; ACK is an exact echo.
 * This is state synchronization, not an exactly-once actuator command channel.
 */
#define UL_HELLO 0x300u
#define UL_WELCOME 0x340u
#define UL_DATA 0x380u
#define UL_ACK 0x3c0u
#define UL_MAGIC 0x31554749u
#define UL_PERIOD_MS 1000u
#define UL_RETRY_MS 250u
#define UL_MAX_TRIES 3u
struct ul_frame {unsigned id;uint8_t data[8];};
static inline uint32_t ul_u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline uint16_t ul_u16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));}
static inline void ul_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static inline void ul_put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static inline uint16_t ul_value(const uint8_t *p){return (uint16_t)(((unsigned)p[6]<<8)|p[7]);}
struct ul_server_node {
    uint32_t cookie,epoch;uint16_t seq,value;unsigned valid,awaiting;
    uint64_t accepted,duplicates,stale,invalid,sessions,seen_ms;
};
static inline uint32_t ul_epoch(uint32_t *counter){++*counter;if(!*counter)++*counter;return *counter;}
/* Return 1 for a response, 0 for silence. modified=1 means a new host write.
 * Caller publishes the committed shadow before transmitting any DATA ACK.
 */
static inline int ul_server_receive(struct ul_server_node *n,unsigned unit,
        uint32_t *counter,const struct ul_frame *in,struct ul_frame *out,
        uint64_t now,unsigned *modified){
    *modified=0;memset(out,0,sizeof *out);
    if(in->id==UL_HELLO+unit){
        uint32_t cookie=ul_u32(in->data);
        if(!cookie||ul_u32(in->data+4)!=UL_MAGIC){n->invalid++;return 0;}
        if(!n->awaiting||cookie!=n->cookie){
            n->cookie=cookie;n->epoch=ul_epoch(counter);n->seq=0;
            n->awaiting=1;n->sessions++;
        }
        out->id=UL_WELCOME+unit;ul_put32(out->data,cookie);ul_put32(out->data+4,n->epoch);return 1;
    }
    if(in->id!=UL_DATA+unit){n->invalid++;return 0;}
    uint32_t epoch=ul_u32(in->data);uint16_t seq=ul_u16(in->data+4),value=ul_value(in->data);
    if(!n->epoch||epoch!=n->epoch){n->stale++;out->id=UL_WELCOME+unit;return 1;} /* zero cookie/epoch requests reconnect */
    if(!seq){n->invalid++;return 0;}
    if(seq==n->seq){
        if(!n->valid||n->value!=value){n->invalid++;return 0;}
        n->duplicates++;
    }else if(seq==(unsigned)n->seq+1u){
        n->seq=seq;n->value=value;n->valid=1;n->awaiting=0;
        n->accepted++;n->seen_ms=now;*modified=1;
    }else if(seq<n->seq){n->stale++;return 0;}
    else {n->invalid++;out->id=UL_WELCOME+unit;return 1;}
    out->id=UL_ACK+unit;memcpy(out->data,in->data,8);return 1;
}
enum ul_phase {UL_CONNECT=1,UL_WAIT_ACK=2,UL_IDLE=3};
struct ul_client {
    unsigned unit,phase,tries,pending;uint32_t cookie,epoch;
    uint16_t seq,value;uint64_t due,acked,unknown,retries,reconnects;
};
static inline void ul_connect(struct ul_client *c,uint64_t now){
    if(!++c->cookie)++c->cookie;
    c->epoch=0;c->seq=0;c->phase=UL_CONNECT;c->tries=0;c->due=now;c->reconnects++;
}
static inline void ul_client_init(struct ul_client *c,unsigned unit,uint64_t now){
    memset(c,0,sizeof *c);c->unit=unit;c->cookie=unit;ul_connect(c,now);
}
static inline void ul_data(const struct ul_client *c,struct ul_frame *out){
    memset(out,0,sizeof *out);out->id=UL_DATA+c->unit;ul_put32(out->data,c->epoch);
    ul_put16(out->data+4,c->seq);out->data[6]=(uint8_t)(c->value>>8);out->data[7]=(uint8_t)c->value;
}
/* Work on a candidate copy. Commit candidate when a proposed frame was
 * submitted successfully, or when no frame was proposed; TX busy must not
 * consume a retry. No busy waits or delays are used by this state machine.
 */
static inline int ul_client_tick(struct ul_client *c,uint64_t now,uint16_t value,struct ul_frame *out){
    if(now<c->due)return 0;
    if(c->phase==UL_IDLE){
        c->value=value;c->pending=1;
        if(c->seq==65535u){ul_connect(c,now);}
        else {c->seq++;c->phase=UL_WAIT_ACK;c->tries=0;}
    }
    if(c->phase==UL_CONNECT){
        if(c->tries==UL_MAX_TRIES){c->tries=0;c->due=now+UL_PERIOD_MS;return 0;}
        memset(out,0,sizeof *out);out->id=UL_HELLO+c->unit;
        ul_put32(out->data,c->cookie);ul_put32(out->data+4,UL_MAGIC);
        c->tries++;c->due=now+UL_RETRY_MS;return 1;
    }
    if(c->phase==UL_WAIT_ACK){
        if(c->tries==UL_MAX_TRIES){
            c->unknown++;c->pending=0;c->phase=UL_IDLE;c->tries=0;c->due=now+UL_PERIOD_MS;return 0;
        }
        if(c->tries)c->retries++;
        ul_data(c,out);c->tries++;c->due=now+UL_RETRY_MS;return 1;
    }
    return 0;
}
static inline void ul_client_receive(struct ul_client *c,const struct ul_frame *in,uint64_t now,uint16_t value){
    if(in->id==UL_WELCOME+c->unit){
        uint32_t cookie=ul_u32(in->data),epoch=ul_u32(in->data+4);
        if(!cookie&&!epoch){if(c->phase==UL_WAIT_ACK)ul_connect(c,now);return;}
        if(c->phase!=UL_CONNECT||cookie!=c->cookie||!epoch)return;
        c->epoch=epoch;c->seq=1;c->phase=UL_WAIT_ACK;c->tries=0;c->due=now;
        if(!c->pending)c->value=value;
        c->pending=1;return;
    }
    if(in->id==UL_ACK+c->unit&&c->phase==UL_WAIT_ACK){
        struct ul_frame expected;ul_data(c,&expected);
        if(memcmp(in->data,expected.data,8))return;
        c->acked++;c->pending=0;c->phase=UL_IDLE;c->due=now+UL_PERIOD_MS;
    }
}
#endif
