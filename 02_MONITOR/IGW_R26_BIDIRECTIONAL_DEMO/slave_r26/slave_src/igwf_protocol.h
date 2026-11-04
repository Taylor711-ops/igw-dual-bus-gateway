#ifndef IGWF_PROTOCOL_H
#define IGWF_PROTOCOL_H
#include "igwf_sha256.h"
#define IGWF_MAX 65536u
#define IGWF_REQ 0x600u
#define IGWF_REPLY 0x620u
#define IGWF_IDLE_MS 2000u
enum {IGWF_PUT=1,IGWF_GET=2,IGWF_GENERATE=3,IGWF_WRITE=4,IGWF_COMMIT=5,IGWF_HASH=6,IGWF_EXPECT=7,IGWF_READ=8,IGWF_CLOSE=9,IGWF_ABORT=10,IGWF_ERROR=127};
enum {IGWF_E_FORMAT=1,IGWF_E_BUSY=2,IGWF_E_SESSION=3,IGWF_E_OFFSET=4,IGWF_E_STATE=5,IGWF_E_HASH=6,IGWF_E_EMPTY=7,IGWF_E_TIMEOUT=8};
enum {IGWF_NONE,IGWF_WRITING,IGWF_READING,IGWF_COMMITTED,IGWF_CLOSED,IGWF_FAILED};
struct igwf_store {
 uint8_t data[IGWF_MAX],expected[32],hash[32];
 uint32_t token,length,next,mask;uint64_t seen;unsigned state,begin_op,unit;int valid;
};
static inline uint32_t igwf_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline void igwf_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static inline unsigned igwf_arg(const uint8_t *p){return (unsigned)p[5]|(unsigned)p[6]<<8;}
static inline uint32_t igwf_length(const uint8_t *p){return igwf_arg(p)|(uint32_t)p[7]<<16;}
static inline void igwf_set_length(uint8_t *p,uint32_t n){p[5]=(uint8_t)n;p[6]=(uint8_t)(n>>8);p[7]=(uint8_t)(n>>16);}
static inline void igwf_message(uint8_t p[8],unsigned op,uint32_t token,unsigned arg,unsigned value){p[0]=(uint8_t)op;igwf_put32(p+1,token);p[5]=(uint8_t)arg;p[6]=(uint8_t)(arg>>8);p[7]=(uint8_t)value;}
static inline uint8_t igwf_generated(unsigned unit,uint32_t token,uint32_t i){return (uint8_t)(i*73u+(i>>8)*19u+unit*31u+(token&255u));}
static inline void igwf_init(struct igwf_store *s,unsigned unit){memset(s,0,sizeof *s);s->unit=unit;}
static inline int igwf_error(uint8_t out[8],const uint8_t in[8],unsigned error){igwf_message(out,IGWF_ERROR,igwf_u32(in+1),in[0],error);return 1;}
static inline int igwf_handle(struct igwf_store *s,const uint8_t in[8],uint8_t out[8],uint64_t now){
 unsigned op=in[0],arg=igwf_arg(in);uint32_t token=igwf_u32(in+1),len=igwf_length(in);
 if(!token)return igwf_error(out,in,IGWF_E_FORMAT);
 if((s->state==IGWF_WRITING||s->state==IGWF_READING)&&now-s->seen>IGWF_IDLE_MS){if(s->state==IGWF_WRITING)s->valid=0;s->state=IGWF_FAILED;}
 if(op==IGWF_PUT||op==IGWF_GET||op==IGWF_GENERATE){
  if((op==IGWF_GET&&len)||(op!=IGWF_GET&&len>IGWF_MAX))return igwf_error(out,in,IGWF_E_FORMAT);
  if(token==s->token){
   if(op!=s->begin_op||(op!=IGWF_GET&&len!=s->length))return igwf_error(out,in,IGWF_E_SESSION);
   if(s->state==IGWF_FAILED)return igwf_error(out,in,IGWF_E_TIMEOUT);
   if(s->state==IGWF_CLOSED)return igwf_error(out,in,IGWF_E_STATE);
   s->seen=now;igwf_message(out,op|128,token,0,0);igwf_set_length(out,s->length);return 1;
  }
  if(s->state==IGWF_WRITING||s->state==IGWF_READING)return igwf_error(out,in,IGWF_E_BUSY);
  if(op==IGWF_GET&&!s->valid)return igwf_error(out,in,IGWF_E_EMPTY);
  s->token=token;s->begin_op=op;s->seen=now;s->next=0;s->mask=0;
  if(op==IGWF_PUT){s->length=len;s->valid=0;s->state=IGWF_WRITING;memset(s->expected,0,32);}
  else{
   if(op==IGWF_GENERATE){s->length=len;for(uint32_t i=0;i<len;i++)s->data[i]=igwf_generated(s->unit,token,i);igwf_digest(s->data,len,s->hash);s->valid=1;}
   s->state=IGWF_READING;
  }
  igwf_message(out,op|128,token,0,0);igwf_set_length(out,s->length);return 1;
 }
 if(token!=s->token)return igwf_error(out,in,IGWF_E_SESSION);
 if(s->state==IGWF_FAILED)return igwf_error(out,in,IGWF_E_TIMEOUT);
 igwf_message(out,op|128,token,arg,in[7]);
 if(op==IGWF_EXPECT){
  if(s->state!=IGWF_WRITING||arg>=32||s->next)return igwf_error(out,in,IGWF_E_STATE);
  if((s->mask&(1u<<arg))&&s->expected[arg]!=in[7])return igwf_error(out,in,IGWF_E_HASH);
  s->expected[arg]=in[7];s->mask|=1u<<arg;
 }else if(op==IGWF_WRITE){
  if(s->state!=IGWF_WRITING||s->mask!=UINT32_MAX)return igwf_error(out,in,IGWF_E_STATE);
  if(arg>=s->length)return igwf_error(out,in,IGWF_E_OFFSET);
  if(arg==s->next){s->data[s->next++]=in[7];}
  else if(!s->next||arg!=s->next-1||s->data[arg]!=in[7])return igwf_error(out,in,IGWF_E_OFFSET);
 }else if(op==IGWF_COMMIT){
  if(arg||in[7])return igwf_error(out,in,IGWF_E_FORMAT);
  if(s->state!=IGWF_COMMITTED){
   if(s->state!=IGWF_WRITING||s->next!=s->length||s->mask!=UINT32_MAX)return igwf_error(out,in,IGWF_E_STATE);
   igwf_digest(s->data,s->length,s->hash);
   if(memcmp(s->hash,s->expected,32)){s->state=IGWF_FAILED;s->valid=0;return igwf_error(out,in,IGWF_E_HASH);}
   s->state=IGWF_COMMITTED;s->valid=1;
  }
 }else if(op==IGWF_HASH){
  if(!s->valid||arg>=32||in[7]||(s->state!=IGWF_COMMITTED&&s->state!=IGWF_READING))return igwf_error(out,in,IGWF_E_STATE);
  out[7]=s->hash[arg];
 }else if(op==IGWF_READ){
  if(s->state!=IGWF_READING||!s->valid||in[7])return igwf_error(out,in,IGWF_E_STATE);
  if(arg>=s->length)return igwf_error(out,in,IGWF_E_OFFSET);
  out[7]=s->data[arg];
 }else if(op==IGWF_CLOSE){
  if(arg||in[7]||(s->state!=IGWF_READING&&s->state!=IGWF_CLOSED))return igwf_error(out,in,IGWF_E_STATE);
  s->state=IGWF_CLOSED;
 }else if(op==IGWF_ABORT){
  if(arg||in[7])return igwf_error(out,in,IGWF_E_FORMAT);
  if(s->state==IGWF_WRITING)s->valid=0;
  s->state=IGWF_FAILED;
 }else return igwf_error(out,in,IGWF_E_FORMAT);
 s->seen=now;return 1;
}
#endif
