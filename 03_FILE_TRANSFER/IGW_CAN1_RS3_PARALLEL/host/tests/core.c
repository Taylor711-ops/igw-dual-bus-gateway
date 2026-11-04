#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#define IGW_NODE_COUNT 2
#define IGW_FIRST_NODE 1
#include "../slave_r23/slave_src/rsf_wire.h"
#include "ramdisk.inc"
#include "../slave_r23/slave_src/sd_store.inc"
#include "../slave_r23/slave_src/rsf_sd_protocol.inc"
static uint64_t tick=100;
static uint64_t sd_now_ms(void){return tick;}
static struct rsf_store States[2];
static void boot(void){
 assert(sd_storage_init()==0);memset(States,0,sizeof States);
 for(unsigned i=0;i<2;i++){States[i].unit=i+1;States[i].bank=SdBanks+i;rsf_restore(States+i);}
}
static struct rsf_packet call(unsigned unit,unsigned op,uint64_t token,uint32_t off,unsigned count,const uint8_t *data,unsigned n){
 uint8_t in[256];static uint8_t out[256];size_t len=rsf_frame(in,unit,0,op,0,token,off,count,data,n);assert(len);len=rsf_handle(States+unit-1,in,len,out,++tick);struct rsf_packet reply;assert(rsf_parse(out,len,1,&reply));return reply;
}
static void put(unsigned unit,uint64_t token,const uint8_t *data,unsigned size){
 uint8_t hash[32];igwf_digest(data,size,hash);assert(!call(unit,RSF_PUT,token,size,0,hash,32).status);
 for(unsigned off=0;off<size;){unsigned n=size-off;if(n>RSF_BLOCK)n=RSF_BLOCK;assert(!call(unit,RSF_WRITE,token,off,n,data+off,n).status);if(!off)assert(!call(unit,RSF_WRITE,token,off,n,data+off,n).status);off+=n;}
 assert(!call(unit,RSF_COMMIT,token,0,0,NULL,0).status);uint64_t generation=SdBanks[unit-1].generation;assert(!call(unit,RSF_COMMIT,token,0,0,NULL,0).status);assert(generation==SdBanks[unit-1].generation);assert(!call(unit,RSF_CLOSE,token,0,0,NULL,0).status);
}
static void get(unsigned unit,uint64_t token,const uint8_t *data,unsigned size){
 struct rsf_packet a=call(unit,RSF_GET,token,0,0,NULL,0);assert(!a.status&&a.offset==size);uint8_t digest[32];igwf_digest(data,size,digest);assert(!memcmp(a.payload,digest,32));
 for(unsigned off=0;off<size;){unsigned n=size-off;if(n>RSF_BLOCK)n=RSF_BLOCK;a=call(unit,RSF_READ,token,off,n,NULL,0);assert(!a.status&&!memcmp(a.payload,data+off,n));off+=n;}
 assert(!call(unit,RSF_CLOSE,token,0,0,NULL,0).status);
}
int main(void){
 test_disk_start(NULL);boot();assert(sizeof States<4096);assert(!States[0].valid&&!States[1].valid);
 uint8_t old[4096],newdata[4096],hash[32];for(unsigned i=0;i<4096;i++){old[i]=(uint8_t)(i*137+(i>>7)*21);newdata[i]=old[i]^165;}put(1,1,old,sizeof old);put(2,2,newdata,sizeof newdata);boot();get(1,3,old,sizeof old);get(2,4,newdata,sizeof newdata);
 puts("PASS real FatFs: persistent reload, two nodes, byte equality and commit dedup");
 igwf_digest(newdata,sizeof newdata,hash);assert(!call(1,RSF_PUT,5,4096,0,hash,32).status);assert(!call(1,RSF_WRITE,5,0,192,newdata,192).status);tick+=RSF_IDLE_MS+1;rsf_expire(States,tick);get(1,6,old,sizeof old);boot();get(1,7,old,sizeof old);
 puts("PASS incomplete write expiry preserves previous committed file across mount/reload");
 assert(!call(1,RSF_PUT,8,4096,0,hash,32).status);assert(!call(1,RSF_WRITE,8,0,192,newdata,192).status);assert(call(1,RSF_COMMIT,8,0,0,NULL,0).status==RSF_INCOMPLETE);assert(!call(1,RSF_ABORT,8,0,0,NULL,0).status);get(1,9,old,sizeof old);
 memset(hash,0,sizeof hash);assert(!call(1,RSF_PUT,10,0,0,hash,32).status);assert(call(1,RSF_COMMIT,10,0,0,NULL,0).status==RSF_DIGEST);get(1,11,old,sizeof old);
 puts("PASS incomplete commit/abort and wrong SHA never replace old version");
 /* Fail the first sector write at sync. */
 igwf_digest(newdata,17,hash);assert(!call(1,RSF_PUT,12,17,0,hash,32).status);assert(!call(1,RSF_WRITE,12,0,17,newdata,17).status);TestFailWrite=0;assert(call(1,RSF_COMMIT,12,0,0,NULL,0).status==RSF_STORAGE);TestFailWrite=-1;boot();get(1,13,old,sizeof old);
 puts("PASS disk write error at commit preserves verified old generation");
 /* Commit a new version, then damage only the newer data and remount. */
 put(1,14,newdata,sizeof newdata);char path[40];sd_path(path,1,SdBanks[0].slot,"BIN");FIL f;UINT wrote;uint8_t changed=0;assert(f_open(&f,path,FA_WRITE)==FR_OK);assert(f_write(&f,&changed,1,&wrote)==FR_OK&&wrote==1);assert(f_close(&f)==FR_OK);boot();get(1,15,old,sizeof old);
 puts("PASS boot verifies SD data SHA and falls back from corrupt newest generation");
 put(1,20,newdata,sizeof newdata);sd_path(path,1,SdBanks[0].slot,"MET");assert(f_open(&f,path,FA_WRITE)==FR_OK);assert(f_write(&f,&changed,1,&wrote)==FR_OK&&wrote==1);assert(f_close(&f)==FR_OK);boot();get(1,21,old,sizeof old);
 puts("PASS incomplete/corrupt newest commit record falls back to prior generation");
 /* >4MiB fixture uses one4KiB workspace, no large slave file array. */
 struct rsf_packet a=call(2,RSF_GENERATE,16,RSF_MAX,0,NULL,0);assert(!a.status&&a.offset==RSF_MAX);uint8_t expected[32];memcpy(expected,a.payload,32);assert(!call(2,RSF_CLOSE,16,0,0,NULL,0).status);boot();a=call(2,RSF_GET,17,0,0,NULL,0);assert(!a.status&&a.offset==RSF_MAX&&!memcmp(expected,a.payload,32));
 for(uint32_t offset=0;offset<RSF_MAX;){unsigned n=RSF_MAX-offset;if(n>RSF_BLOCK)n=RSF_BLOCK;a=call(2,RSF_READ,17,offset,n,NULL,0);assert(!a.status);for(unsigned i=0;i<n;i++)assert(a.payload[i]==rsf_generated(2,16,offset+i));offset+=n;}
 assert(!call(2,RSF_CLOSE,17,0,0,NULL,0).status);printf("PASS real FatFs8MiB generated/persisted/reloaded/read; slave workspace=%zu protocol=%zu\n",sizeof SdBuffer+sizeof SdBanks+sizeof SdFs,sizeof States);
 /* Refuse a nonempty foreign/unclaimed directory rather than overwrite it. */
 assert(f_unlink("1:/IGW23/OWNER.ID")==FR_OK);assert(sd_storage_init()!=0&&!SdReady&&!strcmp(SdReason,"directory_owner"));FILINFO existing;assert(f_stat("1:/IGW23/U01A.BIN",&existing)==FR_OK&&existing.fsize==4096);
 puts("PASS existing nonempty unowned directory is refused without deleting data");
 /* Missing/unformatted volume refuses file requests, never RAM fallback. */
 SdReady=0;assert(call(1,RSF_GET,18,0,0,NULL,0).status==RSF_STORAGE);
 test_disk_finish();puts("RESULT=PASS_R23_REAL_FATFS_CORE_NATIVE; not a physical SD/power-cut test");return 0;
}
