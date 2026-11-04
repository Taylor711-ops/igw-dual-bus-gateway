#ifndef RSF_PROTOCOL_H
#define RSF_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "igw_protocol.h"
#include "igwf_sha256.h"
/* R20 custom RTU extension, not standard Modbus file records. All multibyte
 * fields little endian; CRC16 is the established hardware RTU CRC. */
#define RSF_MAX (8u*1024u*1024u)
#define RSF_BLOCK 192u
#define RSF_HEAD 20u
#define RSF_IDLE_MS 8000u
enum {RSF_SPEED=1,RSF_PUT=2,RSF_GET=3,RSF_GENERATE=4,RSF_WRITE=5,
      RSF_READ=6,RSF_COMMIT=7,RSF_CLOSE=8,RSF_ABORT=9};
enum {RSF_OK=0,RSF_BAD=1,RSF_BUSY=2,RSF_SESSION=3,RSF_RANGE=4,
      RSF_CONFLICT=5,RSF_INCOMPLETE=6,RSF_DIGEST=7,RSF_NO_FILE=8,RSF_STORAGE=9};
enum {RSF_EMPTY=0,RSF_WRITING=1,RSF_READING=2,RSF_COMMITTED=3,RSF_FAILED=4};
struct rsf_packet {unsigned unit,op,status;uint64_t token;uint32_t offset;unsigned count,length;const uint8_t *payload;};
static inline uint32_t rsf_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline void rsf_p32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static inline unsigned rsf_u16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static inline size_t rsf_frame(uint8_t out[256],unsigned unit,unsigned reply,unsigned op,unsigned status,uint64_t token,uint32_t offset,unsigned count,const uint8_t *p,unsigned n){
 if(!unit||unit>16||!token||n>RSF_BLOCK||count>RSF_BLOCK)return 0;
 memset(out,0,RSF_HEAD);out[0]=(uint8_t)unit;out[1]=(uint8_t)(reply?0x43:0x42);out[2]=(uint8_t)op;out[3]=(uint8_t)status;
 rsf_p32(out+4,(uint32_t)token);rsf_p32(out+8,(uint32_t)(token>>32));rsf_p32(out+12,offset);
 out[16]=(uint8_t)count;out[17]=(uint8_t)(count>>8);out[18]=(uint8_t)n;out[19]=(uint8_t)(n>>8);
 if(n){memcpy(out+RSF_HEAD,p,n);}return igw_append_crc(out,RSF_HEAD+n);
}
static inline int rsf_parse(const uint8_t *p,size_t n,unsigned reply,struct rsf_packet *v){
 if(n<RSF_HEAD+2||!igw_valid_adu(p,n)||p[1]!=(reply?0x43:0x42)||!p[0]||p[0]>16)return 0;
 v->unit=p[0];v->op=p[2];v->status=p[3];v->token=rsf_u32(p+4)|(uint64_t)rsf_u32(p+8)<<32;
 v->offset=rsf_u32(p+12);v->count=rsf_u16(p+16);v->length=rsf_u16(p+18);v->payload=p+RSF_HEAD;
 return v->token&&v->count<=RSF_BLOCK&&v->length<=RSF_BLOCK&&n==RSF_HEAD+2+v->length;
}
static inline uint8_t rsf_generated(unsigned unit,uint64_t token,uint32_t i){return (uint8_t)(i*73u+(i>>8)*19u+unit*31u+(uint32_t)token);}
#endif
