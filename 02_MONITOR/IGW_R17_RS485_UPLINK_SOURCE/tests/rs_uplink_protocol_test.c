#include <assert.h>
#include <stdio.h>
#include "../src/rs_uplink_protocol.h"
static struct ul_frame grant(unsigned unit,uint32_t nonce,uint32_t epoch){struct ul_frame f={.id=RSU_GRANT+unit};ul_put32(f.data,nonce);ul_put32(f.data+4,epoch);return f;}
static struct ul_frame control(unsigned id,uint32_t nonce,uint16_t seq,uint16_t tail){struct ul_frame f={.id=id};ul_put32(f.data,nonce);ul_put16(f.data+4,seq);ul_put16(f.data+6,tail);return f;}
int main(void){
 struct rsu_client c;struct ul_frame out,g;uint8_t bytes[RSU_BYTES];rsu_init(&c,1);
 assert(rsu_tick(&c,0,123,987,&out));assert(out.id==RSU_RTS+1);assert(ul_u16(out.data+6)==RSU_RTS_MAGIC);
 g=grant(1,10,124);rsu_receive(&c,&g,1);assert(!c.granted); /* old session grant */
 g=grant(1,10,123);rsu_receive(&c,&g,2);assert(c.granted);
 assert(rsu_prepare(&c,10,bytes));assert(bytes[0]==1&&bytes[1]==RSU_FC&&rsu_crc(bytes,RSU_BYTES)==0);assert(bytes[12]==3&&bytes[13]==219);
 assert(!rsu_prepare(&c,11,bytes));rsu_done(&c,30);assert(rsu_release_tick(&c,30,&out));assert(out.id==RSU_DONE+1);
 rsu_receive(&c,&g,31);assert(!c.granted); /* duplicate grant after TX */
 out=control(RSU_ACK+1,11,1,0);out.data[6]=3;out.data[7]=219;rsu_receive(&c,&out,32);assert(c.accepted==0);
 ul_put32(out.data,10);rsu_receive(&c,&out,33);assert(c.accepted==1);
 out=control(RSU_RELEASED+1,10,1,RSU_RELEASED_MAGIC);rsu_receive(&c,&out,34);assert(!c.release_pending);
 assert(rsu_tick(&c,2033,123,456,&out));assert(c.seq==2);
 g=grant(1,9,123);rsu_receive(&c,&g,2034);assert(!c.granted); /* any older retired nonce, not just equality */
 g=grant(1,11,123);rsu_receive(&c,&g,2035);assert(c.granted);
 assert(!rsu_prepare(&c,2060,bytes)&&c.expired==1&&c.release_pending);
 out=control(RSU_RELEASED+1,11,2,RSU_RELEASED_MAGIC);rsu_receive(&c,&out,2061);assert(!c.release_pending);
 out=control(RSU_CANCEL+1,12,2,RSU_CANCEL_MAGIC);rsu_receive(&c,&out,2062);assert(c.release_pending);
 g=grant(1,12,123);rsu_receive(&c,&g,2063);assert(!c.granted);
 out=control(RSU_RELEASED+1,12,2,RSU_RELEASED_MAGIC);rsu_receive(&c,&out,2064);
 g=grant(1,11,123);rsu_receive(&c,&g,2065);assert(!c.granted); /* delayed older grant after later cancellation */
 assert(rsu_tick(&c,2066,321,111,&out)&&c.seq==1);g=grant(1,100,123);rsu_receive(&c,&g,2067);assert(!c.granted);
 rsu_init(&c,1);assert(rsu_tick(&c,0,123,5,&out));assert(rsu_tick(&c,350,123,6,&out));assert(rsu_tick(&c,700,123,7,&out));assert(!rsu_tick(&c,1050,123,8,&out)&&c.unknown==1&&!c.pending);
 puts("PASS RS protocol: epoch-bound grants, retired-nonce ordering, no ungranted/repeated/late TX, exact CRC frame, explicit release, bounded requests");return 0;
}
