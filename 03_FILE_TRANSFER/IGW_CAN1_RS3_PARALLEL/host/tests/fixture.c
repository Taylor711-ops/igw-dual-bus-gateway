#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <linux/can.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#define IGW_NODE_COUNT 2
#define IGW_FIRST_NODE 1
#include "../slave_r23/slave_src/rsf_wire.h"
#include "ramdisk.inc"
#include "../slave_r23/slave_src/sd_store.inc"
#include "../slave_r23/slave_src/rsf_sd_protocol.inc"
#include "cbf_protocol.h"
static uint64_t sd_now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;}

static struct rsf_store files[2];
static struct cbf_store transport[2];
static int exact(int fd,void *buf,size_t n,int writing){uint8_t *p=buf;while(n){ssize_t k=writing?write(fd,p,n):read(fd,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;}
int main(int argc,char **argv){
 if(argc!=7)return 2;
 test_disk_start(argv[6]);if(sd_storage_init())return 2;
 int input=atoi(argv[1]),output=atoi(argv[2]),rs_in=atoi(argv[3]),rs_out=atoi(argv[4]);const char *fault=argv[5];unsigned dropped=0,corrupted=0;
 for(unsigned i=0;i<2;i++){files[i].unit=i+1;files[i].bank=SdBanks+i;rsf_restore(files+i);transport[i].unit=i+1;}
 for(;;){struct pollfd p[2]={{input,POLLIN,0},{rs_in,POLLIN,0}};if(poll(p,2,1000)<0){if(errno==EINTR)continue;return 2;}
  struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);uint64_t now=(uint64_t)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;
  if(p[1].revents&POLLIN){uint8_t request[258],reply[258]={0};if(exact(rs_in,request,sizeof request,0))break;unsigned n=rsf_u16(request);struct rsf_packet q;if(n>256||!rsf_parse(request+2,n,0,&q))return 2;
   size_t len=q.op==RSF_SPEED?rsf_frame(reply+2,q.unit,1,q.op,0,q.token,q.offset,0,NULL,0):rsf_handle(files+q.unit-1,request+2,n,reply+2,now);reply[0]=(uint8_t)len;reply[1]=(uint8_t)(len>>8);if(exact(rs_out,reply,sizeof reply,1))break;
  }
  if(p[0].revents&POLLIN){struct can_frame f;if(exact(input,&f,sizeof f,0))break;unsigned data=f.can_id>CBF_DATA&&f.can_id<=CBF_DATA+2;unsigned unit=f.can_id-(data?CBF_DATA:CBF_CTL);if(unit<1||unit>2||f.can_dlc!=8)return 2;
   if(!strcmp(fault,"drop_request")&&data&&f.data[2]==1&&!dropped++)continue;
   struct cbf_store *s=transport+unit-1;cbf_receive(s,files+unit-1,data,f.data,now);
   if(!strcmp(fault,"corrupt_file")&&s->cached&&s->request[2]==RSF_COMMIT&&!corrupted++){char path[40];sd_path(path,unit,SdBanks[unit-1].slot,"BIN");FIL file;UINT wrote;uint8_t bad=0xff;assert(f_open(&file,path,FA_WRITE)==FR_OK);assert(f_write(&file,&bad,1,&wrote)==FR_OK);assert(f_close(&file)==FR_OK);}
   const struct cbf_frame *q;
   while((q=cbf_peek(s))){
    struct can_frame reply;memset(&reply,0,sizeof reply);reply.can_id=q->id;reply.can_dlc=8;memcpy(reply.data,q->data,8);cbf_sent(s);
    if(!strcmp(fault,"drop_ack")&&q->id==CBF_REPLY+unit&&q->data[0]==CBF_ACK&&!dropped++)continue;
    if(!strcmp(fault,"drop_meta")&&q->id==CBF_REPLY+unit&&q->data[0]==CBF_META&&!dropped++)continue;
    if(!strcmp(fault,"drop_reply")&&q->id==CBF_RDATA+unit&&q->data[2]==1&&!dropped++)continue;
    if(!strcmp(fault,"stale")&&!dropped++){struct can_frame stale=reply;stale.data[1]^=1;if(exact(output,&stale,sizeof stale,1))return 2;}
    if(exact(output,&reply,sizeof reply,1))return 0;
   }
  }
  if((p[0].revents&POLLHUP)||(p[1].revents&POLLHUP))break;
 }
 for(unsigned i=0;i<2;i++){sd_close(SdBanks+i);}test_disk_finish();return 0;
}
