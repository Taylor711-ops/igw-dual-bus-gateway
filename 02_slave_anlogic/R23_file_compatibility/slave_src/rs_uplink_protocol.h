#ifndef IGW_RS_UPLINK_PROTOCOL_H
#define IGW_RS_UPLINK_PROTOCOL_H
#include <stdint.h>
#include <string.h>
#include "uplink_protocol.h"
/* R17: generated latest state, CAN-coordinated ownership, RS485 carries value.
 * RTS(epoch,sequence,magic) carries NO value. GRANT(nonce,epoch).
 * RS ADU: unit,0x41,nonce32LE,epoch32LE,sequence16LE,value16BE,CRC16LE.
 * ACK over CAN: nonce32LE,sequence16LE,value16BE. Not standard Modbus FC.
 */
#define RSU_RTS 0x400u
#define RSU_GRANT 0x440u
#define RSU_ACK 0x480u
#define RSU_DONE 0x4c0u
#define RSU_RELEASED 0x500u
#define RSU_CANCEL 0x540u
#define RSU_DONE_MAGIC 0x1744u
#define RSU_RELEASED_MAGIC 0x1745u
#define RSU_CANCEL_MAGIC 0x1743u
#define RSU_RTS_MAGIC 0x1752u
#define RSU_GRANT_MAGIC 0x1747u
#define RSU_FC 0x41u
#define RSU_BYTES 16u
#define RSU_PERIOD_MS 2000u
#define RSU_RETRY_MS 350u
#define RSU_START_MS 25u
#define RSU_TX_LIMIT_MS 80u
#define RSU_LEASE_MS 200u
#define RSU_TRIES 3u
static inline uint16_t rsu_crc(const uint8_t *p,unsigned n){uint16_t c=0xffff;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(uint16_t)((c>>1)^((c&1)?0xa001:0));}return c;}
static inline void rsu_frame(uint8_t p[RSU_BYTES],unsigned unit,uint32_t nonce,uint32_t epoch,uint16_t seq,uint16_t value){
 memset(p,0,RSU_BYTES);p[0]=(uint8_t)unit;p[1]=RSU_FC;ul_put32(p+2,nonce);ul_put32(p+6,epoch);ul_put16(p+10,seq);p[12]=(uint8_t)(value>>8);p[13]=(uint8_t)value;ul_put16(p+14,rsu_crc(p,14));
}
struct rsu_client {
 unsigned unit,pending,tries,granted,tx_scheduled,release_pending,cancelled;uint32_t epoch,nonce,retired;uint16_t seq,value;
 uint64_t due,grant_ms,accepted,unknown,retries,expired,release_due;
};
static inline void rsu_init(struct rsu_client *c,unsigned unit){memset(c,0,sizeof *c);c->unit=unit;}
/* Called independently of host requests. Candidate is committed on CAN submit. */
static inline int rsu_tick(struct rsu_client *c,uint64_t now,uint32_t epoch,uint16_t value,struct ul_frame *out){
 if(c->release_pending||c->granted)return 0;
 if(c->epoch!=epoch){c->epoch=epoch;c->seq=0;c->pending=c->tries=c->granted=c->tx_scheduled=0;c->nonce=c->retired=0;c->due=now;}
 if(!epoch||now<c->due)return 0;
 if(!c->pending){
  if(c->seq==65535u)return 0; /* caller rekeys CAN session at rollover */
  c->seq++;c->value=value;c->pending=1;c->tries=0;c->nonce=0;
 }
 if(c->tries==RSU_TRIES){c->unknown++;c->pending=c->granted=0;c->due=now+RSU_PERIOD_MS;return 0;}
 memset(out,0,sizeof *out);out->id=RSU_RTS+c->unit;ul_put32(out->data,c->epoch);ul_put16(out->data+4,c->seq);ul_put16(out->data+6,RSU_RTS_MAGIC);
 if(c->tries)c->retries++;
 c->tries++;c->granted=0;c->tx_scheduled=0;c->due=now+RSU_RETRY_MS;return 1;
}
static inline void rsu_receive(struct rsu_client *c,const struct ul_frame *f,uint64_t now){
 uint32_t nonce=ul_u32(f->data);
 if(f->id==RSU_GRANT+c->unit){
  int fresh=!c->retired||(int32_t)(nonce-c->retired)>0;
  if(c->pending&&c->tries&&ul_u32(f->data+4)==c->epoch&&nonce&&fresh&&!c->release_pending&&!c->granted){
   c->nonce=nonce;c->grant_ms=now;c->granted=1;c->tx_scheduled=0;
  }
  return;
 }
 if(ul_u16(f->data+4)!=c->seq)return;
 if(f->id==RSU_RELEASED+c->unit&&c->release_pending&&nonce==c->nonce&&ul_u16(f->data+6)==RSU_RELEASED_MAGIC){
  c->retired=nonce;c->release_pending=c->granted=c->cancelled=0;
  if(c->pending)c->due=now;
  return;
 }
 if(f->id==RSU_CANCEL+c->unit&&nonce&&(nonce==c->nonce||!c->retired||(int32_t)(nonce-c->retired)>0)&&ul_u16(f->data+6)==RSU_CANCEL_MAGIC){
  c->nonce=nonce;c->retired=nonce;c->granted=0;c->cancelled=1;c->release_pending=1;c->release_due=now;return;
 }
 if(!c->pending)return;
 if(f->id==RSU_ACK+c->unit&&c->nonce&&nonce==c->nonce&&ul_value(f->data)==c->value){
  c->accepted++;c->pending=0;c->due=now+RSU_PERIOD_MS;
 }
}
static inline void rsu_done(struct rsu_client *c,uint64_t now){
 c->release_pending=1;c->granted=0;c->retired=c->nonce;c->release_due=now;
}
static inline int rsu_release_tick(struct rsu_client *c,uint64_t now,struct ul_frame *f){
 if(!c->release_pending||now<c->release_due)return 0;
 memset(f,0,sizeof *f);f->id=RSU_DONE+c->unit;ul_put32(f->data,c->nonce);ul_put16(f->data+4,c->seq);ul_put16(f->data+6,RSU_DONE_MAGIC);
 c->release_due=now+250;return 1;
}
/* No grant => no RS transmission. Each grant permits at most one frame. */
static inline int rsu_prepare(struct rsu_client *c,uint64_t now,uint8_t out[RSU_BYTES]){
 if(!c->pending||!c->granted||c->tx_scheduled)return 0;
 if(now<c->grant_ms||now-c->grant_ms>=RSU_START_MS){c->expired++;rsu_done(c,now);return 0;}
 rsu_frame(out,c->unit,c->nonce,c->epoch,c->seq,c->value);c->tx_scheduled=1;return 1;
}
#endif
