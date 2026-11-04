#ifndef CBF_PROTOCOL_H
#define CBF_PROTOCOL_H
#include "rsf_protocol.h"
/* Classic-CAN bounded-credit transport for one CRC-protected RSF packet.
 * Four 5-byte fragments per credit group; no unbounded hardware queue burst.
 * Full 32-bit message ID in control; low 16 bits + fragment index in data.
 * Complete file token/offset and SHA-256 remain in the upper protocol. */
#define CBF_CTL 0x680u
#define CBF_REPLY 0x6a0u
#define CBF_DATA 0x700u
#define CBF_RDATA 0x720u
#define CBF_WINDOW 4u
#define CBF_PAYLOAD 5u
#define CBF_PACKET (RSF_HEAD+RSF_BLOCK+2u)
enum {CBF_BEGIN=0xa0,CBF_ACK=0xa1,CBF_PULL=0xa2,CBF_META=0xa3,CBF_END=0xa4,CBF_ERROR=0xaf};
struct cbf_frame {uint32_t id;uint8_t data[8];};
struct cbf_store {unsigned unit,length,next,reply_length,cached,active,tail_crc;uint32_t message;uint64_t seen;uint8_t request[CBF_PACKET],reply[256];struct cbf_frame queued[CBF_WINDOW+1];unsigned q_head,q_count;};
static inline unsigned cbf_fragments(unsigned bytes){return (bytes+CBF_PAYLOAD-1)/CBF_PAYLOAD;}
static inline void cbf_control(uint8_t out[8],unsigned type,uint32_t id,unsigned index,unsigned status){memset(out,0,8);out[0]=(uint8_t)type;rsf_p32(out+1,id);out[5]=(uint8_t)index;out[6]=(uint8_t)(index>>8);out[7]=(uint8_t)status;}
static inline void cbf_data(uint8_t out[8],uint32_t id,unsigned index,const uint8_t *p,unsigned size){
 memset(out,0,8);out[0]=(uint8_t)id;out[1]=(uint8_t)(id>>8);out[2]=(uint8_t)index;unsigned start=index*CBF_PAYLOAD,n=size-start;if(n>CBF_PAYLOAD)n=CBF_PAYLOAD;memcpy(out+3,p+start,n);
}
static inline void cbf_enqueue(struct cbf_store *s,unsigned data,const uint8_t p[8]){
 if(s->q_count>=CBF_WINDOW+1)return;
 struct cbf_frame *f=s->queued+s->q_count++;f->id=(data?CBF_RDATA:CBF_REPLY)+s->unit;memcpy(f->data,p,8);
}
static inline void cbf_status(struct cbf_store *s,unsigned type,unsigned index,unsigned status){uint8_t p[8];cbf_control(p,type,s->message,index,status);cbf_enqueue(s,0,p);}
/* The caller drains queued frames before accepting another control request.
 * A transport retry never re-executes an already cached application packet. */
static inline int cbf_receive(struct cbf_store *s,struct rsf_store *file,unsigned data,const uint8_t p[8],uint64_t now){
 if(s->q_head<s->q_count){return 0;}s->q_head=s->q_count=0;
 if(!data){
  uint32_t id=rsf_u32(p+1);if(!id)return 1;
  if(p[0]==CBF_BEGIN){
   unsigned n=p[5],crc=rsf_u16(p+6);if(n<RSF_HEAD+2||n>CBF_PACKET)return 1;
   if(id==s->message){
    if(n!=s->length||crc!=s->tail_crc){cbf_status(s,CBF_ERROR,0,RSF_CONFLICT);return 1;}
    if(!s->cached&&now-s->seen>=2000){s->next=0;s->active=1;}
   }else{s->message=id;s->length=n;s->tail_crc=crc;s->next=0;s->cached=0;s->active=1;s->reply_length=0;}
   s->seen=now;cbf_status(s,CBF_ACK,s->next,CBF_WINDOW);return 1;
  }
  if(p[0]!=CBF_PULL||id!=s->message||!s->cached||p[7])return 1;
  unsigned index=rsf_u16(p+5);
  if(index==65535u){cbf_status(s,CBF_META,s->reply_length,0);return 1;}
  unsigned count=cbf_fragments(s->reply_length);if(index>=count){cbf_status(s,CBF_ERROR,index,RSF_RANGE);return 1;}
  unsigned end=index+CBF_WINDOW;if(end>count)end=count;
  for(unsigned i=index;i<end;i++){uint8_t wire[8];cbf_data(wire,s->message,i,s->reply,s->reply_length);cbf_enqueue(s,1,wire);}
  cbf_status(s,CBF_END,end,0);return 1;
 }
 if(!s->active||rsf_u16(p)!=(s->message&65535u))return 1;
 unsigned index=p[2],count=cbf_fragments(s->length);if(index>=count)return 1;
 unsigned start=index*CBF_PAYLOAD,n=s->length-start;if(n>CBF_PAYLOAD)n=CBF_PAYLOAD;
 for(unsigned i=n;i<CBF_PAYLOAD;i++)if(p[3+i])return 1;
 if(index>s->next){cbf_status(s,CBF_ACK,s->next,CBF_WINDOW);return 1;}
 if(index<s->next){
  if(memcmp(s->request+start,p+3,n)){s->active=0;cbf_status(s,CBF_ERROR,index,RSF_CONFLICT);return 1;}
 }else{memcpy(s->request+start,p+3,n);s->next++;}
 s->seen=now;
 if(s->next==count&&!s->cached){
  struct rsf_packet q;
  if(rsf_u16(s->request+s->length-2)!=s->tail_crc||!rsf_parse(s->request,s->length,0,&q)||q.unit!=s->unit){s->active=0;cbf_status(s,CBF_ERROR,index,RSF_BAD);return 1;}
  s->reply_length=(unsigned)rsf_handle(file,s->request,s->length,s->reply,now);
  if(!s->reply_length){s->active=0;cbf_status(s,CBF_ERROR,index,RSF_BAD);return 1;}s->cached=1;
 }
 if((index+1)%CBF_WINDOW==0||index+1==count)cbf_status(s,CBF_ACK,s->next,CBF_WINDOW);
 return 1;
}
static inline const struct cbf_frame *cbf_peek(struct cbf_store *s){return s->q_head<s->q_count?s->queued+s->q_head:NULL;}
static inline void cbf_sent(struct cbf_store *s){if(s->q_head<s->q_count)s->q_head++;}
#endif
