#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "rsf_protocol.h"
#include "cbf_protocol.h"
#include "managed_lock.h"
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U<<0)
#endif
static volatile sig_atomic_t stopped;
static int fd=-1;
static unsigned unit=1,wire_tx,packets,packet_retries;
static uint64_t until;
static const char *can_name="can0";
#ifdef CBF_TEST_TRANSPORT
static int test_read=-1,test_write=-1;
#endif
static uint64_t host_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+(unsigned)t.tv_nsec/1000;}
static int host_stopped(void){return stopped||host_us()>=until;}
static void stop_signal(int sig){(void)sig;stopped=1;}
static unsigned number(const char *s,unsigned max){uint64_t n=0;if(!s||!*s)exit(2);for(;*s;s++){if(*s<'0'||*s>'9')exit(2);n=n*10+(unsigned)(*s-'0');if(n>max)exit(2);}return (unsigned)n;}
static int connect_can(void){
 int s=socket(PF_CAN,SOCK_RAW|SOCK_CLOEXEC,CAN_RAW);if(s<0)return -1;
 struct can_filter filters[2];memset(filters,0,sizeof filters);filters[0].can_id=CBF_REPLY+unit;filters[1].can_id=CBF_RDATA+unit;
 for(unsigned i=0;i<2;i++)filters[i].can_mask=CAN_SFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG;
 if(setsockopt(s,SOL_CAN_RAW,CAN_RAW_FILTER,filters,sizeof filters)){close(s);return -1;}
 struct sockaddr_can a;memset(&a,0,sizeof a);a.can_family=AF_CAN;a.can_ifindex=(int)if_nametoindex(can_name);
 if(!a.can_ifindex||bind(s,(struct sockaddr *)&a,sizeof a)){close(s);return -1;}return s;
}
#include "cbf_host.h"
static int wire_send(unsigned data,const uint8_t p[8]){
 struct can_frame f;memset(&f,0,sizeof f);f.can_id=(data?CBF_DATA:CBF_CTL)+unit;f.can_dlc=8;memcpy(f.data,p,8);
 uint64_t end=host_us()+100000;
 while(!host_stopped()&&host_us()<end){ssize_t n;
#ifdef CBF_TEST_TRANSPORT
  if(test_write>=0)n=write(test_write,&f,sizeof f);else
#endif
  n=send(fd,&f,sizeof f,MSG_DONTWAIT);
  if(n==(ssize_t)sizeof f){wire_tx++;return 0;}
  if(n<0&&(errno==EAGAIN||errno==ENOBUFS||errno==EINTR)){struct timespec t={0,1000000};nanosleep(&t,NULL);continue;}
  perror("CAN frame send");return -1;
 }return -1;
}
static int wire_receive(unsigned *data,uint8_t p[8],uint64_t end){
 if(end>until)end=until;
 while(!host_stopped()&&host_us()<end){uint64_t now=host_us();if(now>=end)break;struct pollfd poller={fd,POLLIN,0};int ready=poll(&poller,1,(int)((end-now+999)/1000));
  if(ready<0&&errno==EINTR){continue;}if(ready<0){return -1;}if(!ready){break;}
  if(!(poller.revents&POLLIN)){fputs("CAN_INTERFACE_ERROR\n",stderr);return -1;}
  struct can_frame f;ssize_t n;
#ifdef CBF_TEST_TRANSPORT
  if(test_read>=0)n=read(fd,&f,sizeof f);else
#endif
  n=recv(fd,&f,sizeof f,MSG_DONTWAIT|MSG_TRUNC);
  if(n<0&&(errno==EINTR||errno==EAGAIN))continue;
  if(n!=(ssize_t)sizeof f||f.can_dlc!=8||(f.can_id!=CBF_REPLY+unit&&f.can_id!=CBF_RDATA+unit)){wire_ignored++;continue;}
  *data=f.can_id==CBF_RDATA+unit;memcpy(p,f.data,8);return 1;
 }return 0;
}
static int exchange(unsigned op,uint64_t token,uint32_t offset,unsigned count,const uint8_t *p,unsigned n,uint8_t out[256],struct rsf_packet *a){
 uint8_t packet[256];size_t size=rsf_frame(packet,unit,0,op,0,token,offset,count,p,n);if(!size)return -1;
 for(unsigned attempt=0;attempt<3&&!host_stopped();attempt++){
  if(attempt)packet_retries++;
  packets++;
  if(cbh_send(packet,(unsigned)size)){fprintf(stderr,"CAN_BLOCK_SEND_TIMEOUT op=%u offset=%u\n",op,offset);return -1;}
  size_t length=0;if(cbh_read(out,&length)){fputs("CAN_BLOCK_REPLY_TIMEOUT\n",stderr);continue;}
  if(!rsf_parse(out,length,1,a)||a->unit!=unit||a->op!=op||a->token!=token){wire_ignored++;continue;}
  if(a->status){fprintf(stderr,"REMOTE_ERROR op=%u code=%u\n",op,a->status);return -1;}
  if(a->count!=count||(op!=RSF_GET&&a->offset!=offset)||(op==RSF_READ&&a->length!=count)||((op==RSF_GET||op==RSF_GENERATE||op==RSF_COMMIT)&&a->length!=32)||((op==RSF_PUT||op==RSF_WRITE||op==RSF_CLOSE)&&a->length)){wire_ignored++;continue;}
  return 0;
 }
 return -1;
}
static int cancel_pending(void){
#ifdef CBF_TEST_TRANSPORT
 if(test_read>=0)return 0;
#endif
 if(fd>=0){close(fd);fd=-1;}int control=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(control<0)return -1;
 struct ifreq r;memset(&r,0,sizeof r);memcpy(r.ifr_name,can_name,strlen(can_name)+1);
 if(ioctl(control,SIOCGIFFLAGS,&r)){close(control);return -1;}short flags=r.ifr_flags;r.ifr_flags=(short)(flags&~IFF_UP);
 if(ioctl(control,SIOCSIFFLAGS,&r)){close(control);return -1;}r.ifr_flags=(short)(flags|IFF_UP);int ret=ioctl(control,SIOCSIFFLAGS,&r);close(control);return ret;
}
/* Same verified publication contract as R18/R20. Existing output is never replaced. */
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
static void usage(void){puts("R21 classic-CAN block file / shared R23 CAN+RS485 SD store, max 8388608 bytes/node.\nigw_can_block [--can can0] [--unit 1|2] put INPUT\nigw_can_block [options] get NEW_OUTPUT\nigw_can_block [options] generate BYTES NEW_OUTPUT\nDedicated classic CAN 500000, no other main-board gateway/uplink/file owners.\n192-byte application blocks; credit4 transport. SHA256 before publication.\nSlave SD_FILE_R23 required; SD generations; no file encryption or resume.");}
int main(int argc,char **argv){
 int pos=1;while(pos<argc&&!strncmp(argv[pos],"--",2)){const char *key=argv[pos++];if(!strcmp(key,"--help")){usage();return 0;}if(pos>=argc)return 2;const char *value=argv[pos++];
  if(!strcmp(key,"--can"))can_name=value;else if(!strcmp(key,"--unit")){unit=number(value,2);if(!unit)return 2;}
#ifdef CBF_TEST_TRANSPORT
  else if(!strcmp(key,"--test-read-fd"))test_read=(int)number(value,100000);else if(!strcmp(key,"--test-write-fd"))test_write=(int)number(value,100000);
#endif
  else return 2;
 }
 if(strlen(can_name)>=IFNAMSIZ||pos>=argc){usage();return 2;}const char *op=argv[pos++];unsigned mode;uint32_t size=0;
 if(!strcmp(op,"put"))mode=RSF_PUT;else if(!strcmp(op,"get"))mode=RSF_GET;else if(!strcmp(op,"generate")){mode=RSF_GENERATE;if(pos>=argc)return 2;size=number(argv[pos++],RSF_MAX);}else{usage();return 2;}
 if(pos+1!=argc){usage();return 2;}const char *path=argv[pos];static uint8_t data[RSF_MAX];uint8_t own[32],remote[32],answer[256];struct rsf_packet a;
 if(mode==RSF_PUT&&input(path,data,&size)){perror("input");return 2;}
 if(mode!=RSF_PUT){struct stat st;if(!lstat(path,&st)){fputs("Output already exists\n",stderr);return 2;}if(errno!=ENOENT){perror("output");return 2;}}
 struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_handler=stop_signal;sigemptyset(&sa.sa_mask);sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);signal(SIGPIPE,SIG_IGN);
 uint64_t token;int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(random<0||read(random,&token,8)!=8||read(random,&message_id,4)!=4){perror("random");return 2;}close(random);if(!token)token=1;
 int lock=open("/tmp/igw_can_file_r18.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){fputs("Another CAN file client owns the lock\n",stderr);return 2;}
 int rs_owned=0;
#ifdef CBF_TEST_TRANSPORT
 if(test_read<0)
#endif
 {
  if(geteuid()){fputs("Run as root\n",stderr);return 2;}
  int managed=igw_managed_rs_lock();if(managed<0){perror("Invalid managed RS485 owner");return 2;}
  if(!managed){if(mkdir(IGW_RS_LOCK,0700)){perror("RS485 owner lock; shared file store requires exclusive use");return 2;}rs_owned=1;}
 }
#ifdef CBF_TEST_TRANSPORT
 fd=test_read>=0?test_read:connect_can();
#else
 fd=connect_can();
#endif
 if(fd<0){perror("CAN connect");if(rs_owned)rmdir("/tmp/igw_rs485_once.lock");return 2;}
 setvbuf(stdout,NULL,_IOLBF,0);uint64_t began=host_us();until=began+UINT64_C(3600000000);int ret=3,cancel_failed=0;
 printf("R23_BEGIN unit=%u direction=%s max_bytes=%u block_bytes=%u credit=%u; shared SD CAN/RS485 file\n",unit,mode==RSF_PUT?"host_to_device":"device_to_host",RSF_MAX,RSF_BLOCK,CBF_WINDOW);
 if(mode==RSF_PUT)igwf_digest(data,size,own);
 if(exchange(mode,token,mode==RSF_GET?0:size,0,mode==RSF_PUT?own:NULL,mode==RSF_PUT?32:0,answer,&a))goto finish;
 if(mode!=RSF_PUT){size=a.offset;if(size>RSF_MAX){fputs("Remote file size out of range\n",stderr);goto finish;}memcpy(remote,a.payload,32);}
 for(uint32_t offset=0;offset<size;){unsigned n=size-offset;if(n>RSF_BLOCK)n=RSF_BLOCK;
  if(exchange(mode==RSF_PUT?RSF_WRITE:RSF_READ,token,offset,n,mode==RSF_PUT?data+offset:NULL,mode==RSF_PUT?n:0,answer,&a))goto finish;
  if(mode!=RSF_PUT){memcpy(data+offset,a.payload,n);if(mode==RSF_GENERATE)for(unsigned i=0;i<n;i++)if(data[offset+i]!=rsf_generated(unit,token,offset+i)){fputs("Generated data mismatch\n",stderr);goto finish;}}
  offset+=n;if(offset%61440u==0||offset==size)printf("PROGRESS bytes=%u/%u\n",offset,size);
 }
 if(mode==RSF_PUT){if(exchange(RSF_COMMIT,token,0,0,NULL,0,answer,&a))goto finish;memcpy(remote,a.payload,32);}else igwf_digest(data,size,own);
 if(memcmp(own,remote,32)){fputs("FILE_SHA256_MISMATCH\n",stderr);goto finish;}
 if(exchange(RSF_CLOSE,token,0,0,NULL,0,answer,&a))goto finish;
 if(mode!=RSF_PUT&&publish(path,data,size)){perror("publish verified output");goto finish;}ret=0;
finish:
 if(ret){cancel_failed=cancel_pending();fprintf(stderr,"CAN_PENDING_TX_CANCEL=%s\n",cancel_failed?"FAILED":"DONE");
#ifdef CBF_TEST_TRANSPORT
  if(test_read<0)
#endif
  {puts("RECOVERY_WAIT 10 seconds for remote file lease; no new frames submitted");uint64_t end=host_us()+10000000;while(host_us()<end){struct timespec t={0,100000000};nanosleep(&t,NULL);}}
  fputs("RESULT=FAIL_CAN_BLOCK_FILE execution=unknown; no unverified local output published; review before restart\n",stderr);
 }else{
  uint64_t duration=(host_us()-began)/1000;printf("RESULT=PASS_CAN_BLOCK_FILE unit=%u bytes=%u duration_ms=%llu packets=%u wire_tx=%u link_retries=%u packet_retries=%u ignored=%u sha256=",unit,size,(unsigned long long)duration,packets,wire_tx,link_retries,packet_retries,wire_ignored);for(unsigned i=0;i<32;i++)printf("%02x",own[i]);printf(" effective_Bps=%.1f\n",duration?size*1000.0/duration:0.0);
 }
 if(fd>=0){close(fd);}if(rs_owned&&!cancel_failed){rmdir("/tmp/igw_rs485_once.lock");}close(lock);return ret;
}
