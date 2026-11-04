#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include "ramdisk0.inc"
#include "../zynq/src/file_engine.c"
static uint64_t tick=100;
uint64_t rs3_clock_ms(void){return tick;}
static struct rsf_packet call(unsigned unit,unsigned op,uint64_t token,uint32_t off,unsigned count,const uint8_t *data,unsigned n){
    uint8_t in[256];static uint8_t out[256];
    size_t len=rsf_frame(in,unit,0,op,0,token,off,count,data,n);
    assert(len);len=rs3_file_request(in,len,out);tick++;
    struct rsf_packet reply;assert(rsf_parse(out,len,1,&reply));return reply;
}
static void put(uint64_t token,const uint8_t *data,unsigned size){
    uint8_t hash[32];igwf_digest(data,size,hash);
    assert(!call(3,RSF_PUT,token,size,0,hash,32).status);
    for(unsigned off=0;off<size;){unsigned n=size-off;if(n>RSF_BLOCK)n=RSF_BLOCK;
        assert(!call(3,RSF_WRITE,token,off,n,data+off,n).status);
        if(!off)assert(!call(3,RSF_WRITE,token,off,n,data+off,n).status);
        off+=n;
    }
    assert(!call(3,RSF_COMMIT,token,0,0,NULL,0).status);
    assert(!call(3,RSF_COMMIT,token,0,0,NULL,0).status);
    assert(!call(3,RSF_CLOSE,token,0,0,NULL,0).status);
}
static void get(uint64_t token,const uint8_t *data,unsigned size){
    struct rsf_packet a=call(3,RSF_GET,token,0,0,NULL,0);uint8_t hash[32];
    assert(!a.status&&a.offset==size);igwf_digest(data,size,hash);assert(!memcmp(a.payload,hash,32));
    for(unsigned off=0;off<size;){unsigned n=size-off;if(n>RSF_BLOCK)n=RSF_BLOCK;
        a=call(3,RSF_READ,token,off,n,NULL,0);assert(!a.status&&!memcmp(a.payload,data+off,n));off+=n;}
    assert(!call(3,RSF_CLOSE,token,0,0,NULL,0).status);
}
int main(void){
    test_disk_start(NULL);assert(!rs3_file_init());
    uint8_t a[4096],b[4096],hash[32];for(unsigned i=0;i<sizeof a;i++){a[i]=(uint8_t)i;b[i]=(uint8_t)(i^165);}
    put(1,a,sizeof a);assert(!rs3_file_init());get(2,a,sizeof a);
    igwf_digest(b,sizeof b,hash);assert(!call(3,RSF_PUT,3,sizeof b,0,hash,32).status);
    assert(!call(3,RSF_WRITE,3,0,192,b,192).status);tick+=RSF_IDLE_MS+1;rs3_file_tick();
    assert(!rs3_file_init());get(4,a,sizeof a);
    put(5,b,sizeof b);assert(!rs3_file_init());get(6,b,sizeof b);
    uint8_t frame[256],reply[256];size_t n=rsf_frame(frame,1,0,RSF_GET,0,2,0,0,NULL,0);
    assert(!rs3_file_request(frame,n,reply));
    frame[0]=3;assert(!rs3_file_request(frame,n,reply)); /* bad CRC */
    uint8_t numeric[8]={3,3,0,16,0,1};igw_append_crc(numeric,6);
    assert(!rs3_file_request(numeric,8,reply)); /* remains PL-owned */
    assert(call(3,RSF_SPEED,7,115200,0,NULL,0).status==RSF_BAD);
    assert(call(3,RSF_SPEED,8,9600,0,NULL,0).status==RSF_OK);
    assert(f_unlink("0:/IGW3/OWNER.ID")==FR_OK);
    assert(rs3_file_init()!=0&&!SdReady&&!strcmp(SdReason,"directory_owner"));
    FILINFO existing;assert(f_stat("0:/IGW3/U03A.BIN",&existing)==FR_OK);
    assert(call(3,RSF_GET,9,0,0,NULL,0).status==RSF_STORAGE);
    test_disk_finish();puts("PASS native unit3: FAT storage, readback, remount, expiry, CRC, bus isolation");return 0;
}
