#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "igw_driver.h"
#include "rsf_protocol.h"
#include "managed_lock.h"
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U<<0)
#endif
static volatile sig_atomic_t stopped;
static igw_device dev;
static volatile uint32_t *mmio;
static unsigned unit=1,baud=9600,wanted_baud=9600,requests,retries,ignored;
static uint64_t token,until;
static int lock_owned,device_safe;
#ifdef RSF_TEST_TRANSPORT
static int test_read=-1,test_write=-1;
#endif
static uint64_t now_us(void *p){(void)p;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+(unsigned)t.tv_nsec/1000;}
static void stop_signal(int n){(void)n;stopped=1;}
static uint32_t rd(void *p,unsigned o){(void)p;__sync_synchronize();uint32_t v=mmio[o/4];__sync_synchronize();return v;}
static void wr(void *p,unsigned o,uint32_t v){(void)p;__sync_synchronize();mmio[o/4]=v;__sync_synchronize();(void)mmio[IGW_CONTROL/4];}
static void pause_us(unsigned n){struct timespec t={n/1000000,(long)(n%1000000)*1000};while(nanosleep(&t,&t)&&errno==EINTR&&!stopped){}}
static unsigned number(const char *s,unsigned max){if(!s||!*s){fputs("Invalid number\n",stderr);exit(2);}uint64_t n=0;for(;*s;s++){if(*s<'0'||*s>'9'||n>(max-(unsigned)(*s-'0'))/10u){fputs("Invalid number\n",stderr);exit(2);}n=n*10+(unsigned)(*s-'0');if(n>max){fputs("Number out of range\n",stderr);exit(2);}}return (unsigned)n;}
static int configure(unsigned rate){
#ifdef RSF_TEST_TRANSPORT
 if(test_read>=0){baud=rate;return 0;}
#endif
 if(igw_rd(&dev,IGW_MAGIC)!=0x32574749u||igw_rd(&dev,IGW_VERSION)!=0x00020000u){fputs("RS485_ABI_MISMATCH; no writes permitted\n",stderr);return -1;}
 device_safe=1;int ret=igw_configure(&dev,rate);if(ret){fprintf(stderr,"RS485_CONFIG_FAIL=%d\n",ret);return -1;}baud=rate;return 0;
}
#ifdef RSF_TEST_TRANSPORT
static int exact(int f,void *buf,size_t n,int writing){uint8_t *p=buf;while(n){ssize_t k=writing?write(f,p,n):read(f,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;}
#endif
static int send_frame(const uint8_t *p,size_t n){
#ifdef RSF_TEST_TRANSPORT
 if(test_read>=0){uint8_t frame[258]={0};frame[0]=(uint8_t)n;frame[1]=(uint8_t)(n>>8);memcpy(frame+2,p,n);return exact(test_write,frame,sizeof frame,1);}
#endif
 return igw_transmit(&dev,p,n,(uint64_t)n*11000000/baud+500000)==IGW_OK?0:-1;
}
static int receive_frame(uint8_t *p,size_t *n){
#ifdef RSF_TEST_TRANSPORT
 if(test_read>=0){struct pollfd poller={test_read,POLLIN,0};int ready=poll(&poller,1,0);if(ready<=0)return 0;if(!(poller.revents&POLLIN))return -1;uint8_t frame[258];if(exact(test_read,frame,sizeof frame,0))return -1;*n=rsf_u16(frame);if(*n>256)return -1;memcpy(p,frame+2,*n);return 1;}
#endif
 uint64_t tick;return igw_take_frame(&dev,p,n,&tick);
}
static int exchange(unsigned op,uint32_t offset,unsigned count,const uint8_t *body,unsigned size,uint8_t answer[256],struct rsf_packet *a){
 uint8_t request[256];size_t length=rsf_frame(request,unit,0,op,0,token,offset,count,body,size);if(!length)return -1;
 for(unsigned attempt=0;attempt<3&&!stopped&&now_us(NULL)<until;attempt++){
  if(attempt)retries++;
  if(send_frame(request,length)){fputs("RS485_TX_FAILED\n",stderr);return -1;}requests++;
  /* Covers the largest response at the active baud, CPU scheduling, and T3.5. */
  uint64_t budget=(op==RSF_COMMIT||op==RSF_GENERATE)?UINT64_C(30000000):UINT64_C(600000)+(uint64_t)256*11000000/baud;
  uint64_t end=now_us(NULL)+budget;if(end>until)end=until;
  while(!stopped&&now_us(NULL)<end){size_t n=0;int got=receive_frame(answer,&n);if(got<0){ignored++;pause_us(100);continue;}if(!got){pause_us(100);continue;}
   if(!rsf_parse(answer,n,1,a)||a->unit!=unit||a->token!=token||a->op!=op){ignored++;continue;}
   if(a->status){fprintf(stderr,"REMOTE_ERROR op=%u code=%u\n",op,a->status);return -1;}
   if(a->count!=count||((op!=RSF_GET)&&a->offset!=offset)||((op==RSF_READ)&&a->length!=count)||((op==RSF_GET||op==RSF_GENERATE||op==RSF_COMMIT)&&a->length!=32)||((op==RSF_PUT||op==RSF_WRITE||op==RSF_CLOSE||op==RSF_SPEED)&&a->length)){ignored++;continue;}
   return 0;
  }
 }
 fprintf(stderr,"TRANSFER_TIMEOUT_OR_STOP op=%u offset=%u\n",op,offset);return -1;
}
static int input(const char *path,uint8_t *data,uint32_t *size){
 int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return -1;struct stat st;if(fstat(f,&st)||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>RSF_MAX){close(f);errno=EFBIG;return -1;}
 *size=(uint32_t)st.st_size;uint32_t done=0;while(done<*size){ssize_t n=read(f,data+done,*size-done);if(n<0&&errno==EINTR)continue;if(n<=0){close(f);return -1;}done+=(uint32_t)n;}uint8_t extra;ssize_t n=read(f,&extra,1);close(f);return n==0?0:-1;
}
static int publish(const char *path,const uint8_t *data,uint32_t size){
 if(strlen(path)>4000){errno=ENAMETOOLONG;return -1;}char tmp[4050];snprintf(tmp,sizeof tmp,"%s.part.XXXXXX",path);int f=mkstemp(tmp);if(f<0)return -1;uint32_t done=0;int ret=-1;
 while(done<size){ssize_t n=write(f,data+done,size-done);if(n<0&&errno==EINTR)continue;if(n<=0)goto out;done+=(uint32_t)n;}
 if(fsync(f)){goto out;}if(close(f)){f=-1;goto out;}f=-1;
 if(syscall(SYS_renameat2,AT_FDCWD,tmp,AT_FDCWD,path,RENAME_NOREPLACE)){goto out;}ret=0;
out:
 {int saved=errno;if(f>=0)close(f);if(ret)unlink(tmp);errno=saved;}return ret;
}
/* Wait for automatic slave baseline return after a failure. No new frames are
 * submitted, including ABORT, because execution and baud may be unknown. */
static int release_bus(int failed){
#ifdef RSF_TEST_TRANSPORT
 if(test_read>=0){puts("FINAL_TX_AUTHORIZATION=0x00000000 TEST_ONLY=1");return 0;}
#endif
 if(!device_safe)return 0;
 igw_wr(&dev,IGW_CONTROL,0);uint64_t end=now_us(NULL)+1500000;
 while((igw_rd(&dev,IGW_STATUS)&IGW_TX_BUSY)&&now_us(NULL)<end)pause_us(100);
 if(igw_rd(&dev,IGW_STATUS)&IGW_TX_BUSY){fputs("RS485_NOT_QUIESCENT lock retained; operator review required\n",stderr);return -1;}
 if(failed){puts("RECOVERY_WAIT slave baseline timeout; wait 10 seconds");uint64_t deadline=now_us(NULL)+10000000;while(now_us(NULL)<deadline){struct timespec t={0,100000000};nanosleep(&t,NULL);}}
 if(configure(9600))return -1;
 printf("FINAL_TX_AUTHORIZATION=0x%08x BASELINE_BAUD=9600\n",igw_rd(&dev,IGW_CONTROL));return igw_rd(&dev,IGW_CONTROL)?-1:0;
}
static void usage(void){puts("RS485 file tool; nodes 1/2 require R23; node 3 requires Zynq file firmware.\nigw_rs485_file [--unit 1|2|3] [--baud 9600] put INPUT\nigw_rs485_file [options] get NEW_OUTPUT\nigw_rs485_file [options] generate BYTES NEW_OUTPUT\nMax 8388608 bytes per node. All nodes share a 9600 baud bus; large files are slow.\nDedicated bus; stop all gateways and file clients before use. Files are plaintext.");}
int main(int argc,char **argv){
 int pos=1;while(pos<argc&&!strncmp(argv[pos],"--",2)){const char *key=argv[pos++];if(!strcmp(key,"--help")){usage();return 0;}if(pos>=argc){usage();return 2;}const char *v=argv[pos++];if(!strcmp(key,"--unit")){unit=number(v,3);if(!unit)return 2;}else if(!strcmp(key,"--baud")){wanted_baud=number(v,115200);if(wanted_baud!=9600){fputs("RS485_SHARED_BUS_REQUIRES_9600\n",stderr);return 2;}}
#ifdef RSF_TEST_TRANSPORT
 else if(!strcmp(key,"--test-read-fd"))test_read=(int)number(v,100000);else if(!strcmp(key,"--test-write-fd"))test_write=(int)number(v,100000);
#endif
 else{usage();return 2;}}
 if(pos>=argc){usage();return 2;}const char *op=argv[pos++];unsigned mode;uint32_t size=0;
 if(!strcmp(op,"put"))mode=RSF_PUT;else if(!strcmp(op,"get"))mode=RSF_GET;else if(!strcmp(op,"generate")){mode=RSF_GENERATE;if(pos>=argc)return 2;size=number(argv[pos++],RSF_MAX);}else{usage();return 2;}
 if(pos+1!=argc){usage();return 2;}const char *path=argv[pos];static uint8_t data[RSF_MAX];uint8_t own[32],remote[32],answer[256];struct rsf_packet a;
 if(mode==RSF_PUT&&input(path,data,&size)){perror("input");return 2;}
 if(mode!=RSF_PUT){struct stat st;if(!lstat(path,&st)){fputs("Output already exists\n",stderr);return 2;}if(errno!=ENOENT){perror("output");return 2;}}
 struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_handler=stop_signal;sigemptyset(&sa.sa_mask);sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);signal(SIGPIPE,SIG_IGN);
 int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(random<0||read(random,&token,8)!=8){perror("random");return 2;}close(random);if(!token)token=1;
 int lock=open("/tmp/igw_rs485_file_r20.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){fputs("Another RS file client owns the lock\n",stderr);return 2;}
#ifdef RSF_TEST_TRANSPORT
 if(test_read<0)
#endif
 {
  if(geteuid()){fputs("Run as root\n",stderr);return 2;}
  int managed=igw_managed_rs_lock();if(managed<0){perror("Invalid managed RS485 owner");return 2;}
  if(!managed){if(mkdir(IGW_RS_LOCK,0700)){perror("RS485 owner lock (do not remove a live lock)");return 2;}lock_owned=1;}
  int f=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(f<0){perror("devmem");if(lock_owned)rmdir(IGW_RS_LOCK);return 2;}
  void *p=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,f,0x80400000);close(f);if(p==MAP_FAILED){perror("mmap");if(lock_owned)rmdir(IGW_RS_LOCK);return 2;}mmio=p;dev.read=rd;dev.write=wr;dev.now_us=now_us;
 }
 setvbuf(stdout,NULL,_IOLBF,0);uint64_t began=now_us(NULL);until=began+UINT64_C(43200000000);int ret=3,quiescent=0;
 /* A previous crashed high-speed client may leave a short lease. Deliberate
  * baseline wait avoids probing at competing baud rates after a crash. */
 if(configure(9600))goto finish;
 printf("R23_BEGIN unit=%u direction=%s baud=%u max_bytes=%u block_bytes=%u session=%016llx\n",unit,mode==RSF_PUT?"host_to_device":"device_to_host",wanted_baud,RSF_MAX,RSF_BLOCK,(unsigned long long)token);
 if(wanted_baud!=9600){if(exchange(RSF_SPEED,wanted_baud,0,NULL,0,answer,&a))goto finish;pause_us(180000);if(stopped||configure(wanted_baud))goto finish;puts("SPEED_NEGOTIATED=115200");}
 if(mode==RSF_PUT)igwf_digest(data,size,own);
 if(exchange(mode,mode==RSF_GET?0:size,0,mode==RSF_PUT?own:NULL,mode==RSF_PUT?32:0,answer,&a))goto finish;
 if(mode!=RSF_PUT){size=a.offset;if(size>RSF_MAX){fputs("Remote size out of range\n",stderr);goto finish;}memcpy(remote,a.payload,32);}
 for(uint32_t offset=0;offset<size;){unsigned n=size-offset;if(n>RSF_BLOCK)n=RSF_BLOCK;
  if(exchange(mode==RSF_PUT?RSF_WRITE:RSF_READ,offset,n,mode==RSF_PUT?data+offset:NULL,mode==RSF_PUT?n:0,answer,&a))goto finish;
  if(mode!=RSF_PUT){memcpy(data+offset,a.payload,n);if(mode==RSF_GENERATE)for(unsigned i=0;i<n;i++)if(data[offset+i]!=rsf_generated(unit,token,offset+i)){fputs("Generated data mismatch\n",stderr);goto finish;}}
  offset+=n;if(offset%12288u==0||offset==size)printf("PROGRESS bytes=%u/%u\n",offset,size);
 }
 if(mode==RSF_PUT){if(exchange(RSF_COMMIT,0,0,NULL,0,answer,&a))goto finish;memcpy(remote,a.payload,32);}else igwf_digest(data,size,own);
 if(memcmp(own,remote,32)){fputs("FILE_SHA256_MISMATCH\n",stderr);goto finish;}
 if(exchange(RSF_CLOSE,0,0,NULL,0,answer,&a))goto finish;
 if(wanted_baud!=9600){if(exchange(RSF_SPEED,9600,0,NULL,0,answer,&a))goto finish;pause_us(180000);if(stopped||configure(9600))goto finish;}
 if(mode!=RSF_PUT&&publish(path,data,size)){perror("publish verified output");goto finish;}
 ret=0;
finish:
 quiescent=release_bus(ret!=0);if(quiescent)ret=3;
 if(!ret){uint64_t duration=(now_us(NULL)-began)/1000;printf("RESULT=PASS_RS485_FILE unit=%u bytes=%u baud=%u duration_ms=%llu requests=%u retries=%u ignored=%u sha256=",unit,size,wanted_baud,(unsigned long long)duration,requests,retries,ignored);for(unsigned i=0;i<32;i++)printf("%02x",own[i]);printf(" effective_Bps=%.1f\n",duration?size*1000.0/duration:0.0);}
 else fputs("RESULT=FAIL_RS485_FILE execution=unknown; no unverified local output published; inspect log before restart\n",stderr);
 if(mmio){munmap((void *)mmio,4096);}if(lock_owned&&!quiescent){rmdir("/tmp/igw_rs485_once.lock");}close(lock);return ret;
}
