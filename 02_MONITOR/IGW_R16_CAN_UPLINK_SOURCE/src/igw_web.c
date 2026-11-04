#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "json_min.h"
#include "web_assets.h"
#define JOBS 256
#define HISTORY 120
#define TRACE 32768
static atomic_bool stopping;
static uint64_t started;
static char session[33],token[33],host_header[80],origin[100];
static struct in_addr allowed_peer;
static unsigned udp_deadline_ms=700,period_ms=3000,nodes=1;
static uint64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))abort();return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void stop_handler(int sig){(void)sig;atomic_store(&stopping,true);}
struct counts {uint64_t attempts,ok,timeout,tx_fail,bad_reply,crc,uart,gap,overrun,watchdog;};
struct snapshot {bool valid;uint16_t values[32];uint64_t sampled,rtt,local_us;struct counts c;char error[64];};
struct command {char id[33];unsigned bus,op,address,value,unit;}; /* bus CAN0 RS1 BOTH2; op READ0 WRITE1 CROSS2 */
struct job {struct command cmd;unsigned status;uint64_t until,begin,end; /* 0 queued 1 running 2 pass 3 review */
    bool before_valid,after_valid,count_valid,write_started,write_acked,unchanged;
    unsigned before,readback,count_before,count_after,can_written,rs_written;
    char error[64],trace[TRACE];size_t trace_used;};
struct point {uint64_t at,can_us,rs_us;bool can_valid,rs_valid;};
static struct {
 pthread_mutex_t mutex;struct snapshot snap[2],device[16][2];struct job jobs[JOBS];int active;
 struct point history[HISTORY];unsigned history_count,history_next;unsigned http_active;
} state={.mutex=PTHREAD_MUTEX_INITIALIZER,.active=-1};
struct bus {int fd;unsigned which,unit;uint32_t seq;bool baseline;uint64_t previous[20];struct counts c;char error[64];uint64_t last_rtt,last_local_us;};
static struct bus buses[2];
#include "npu_bridge.h"
static const char *bus_name(unsigned b){return b==0?"CAN":b==1?"RS485":"BOTH";}
static const char *op_name(unsigned op){return op==0?"READ":op==1?"WRITE":"CROSS";}
static void reason(char out[64],const char *s){snprintf(out,64,"%.63s",s);}
static int numeq(const struct jd *d,const char *key,uint64_t expect){uint64_t v;return jfield(d,0,key,&v)==0 && v==expect;}
static int hex_decode(const struct jd *d,const char *key,uint8_t *out,size_t capacity){
 char s[600];if(jstr(d,jget(d,0,key),s,sizeof s))return -1;size_t n=strlen(s);if(n%2 || n/2>capacity)return -1;
 for(size_t i=0;i<n/2;i++){unsigned x=0;for(unsigned j=0;j<2;j++){char c=s[2*i+j];unsigned v=c>='0'&&c<='9'?(unsigned)(c-'0'):c>='a'&&c<='f'?(unsigned)(c-'a'+10):c>='A'&&c<='F'?(unsigned)(c-'A'+10):99;if(v>15)return -1;x=x*16+v;}out[i]=(uint8_t)x;}return (int)(n/2);
}
static uint16_t crc16(const uint8_t *p,size_t n){uint16_t c=65535;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned b=0;b<8;b++)c=(uint16_t)((c>>1)^((c&1)?0xa001:0));}return c;}
static bool same_cmd(const struct command *a,const struct command *b){return !strcmp(a->id,b->id)&&a->bus==b->bus&&a->op==b->op&&a->address==b->address&&a->value==b->value&&a->unit==b->unit;}
static void add_trace(struct job *j,unsigned bus,uint32_t seq,const char *raw){
 if(!j)return;
 size_t left=sizeof j->trace-j->trace_used;
 int n=snprintf(j->trace+j->trace_used,left,"%s{\"bus\":\"%s\",\"seq\":%"PRIu32",\"reply\":%s}",j->trace_used?",":"",bus_name(bus),seq,raw&&*raw?raw:"null");
 if(n<0 || (size_t)n>=left){reason(j->error,"evidence_capacity");return;}j->trace_used+=(size_t)n;
}
/* Only the worker owns these UDP sockets. No write retransmission. */
static int exchange(struct bus *b,bool write,unsigned address,unsigned arg,uint16_t *values,struct job *j){
 uint64_t local_begin=npu_now_us();
 char command[128],raw[3072];b->error[0]=0;
 if(atomic_load(&stopping)){reason(b->error,"service_stopping");return -1;}
 b->seq++;
 int n=b->which==0?snprintf(command,sizeof command,"IGW2 CAN %s %"PRIu32" %u %u",write?"WRITE":"READ",b->seq,b->unit,address):snprintf(command,sizeof command,"IGW2 RS485 %s %"PRIu32" %u %u %u",write?"WRITE":"READ",b->seq,b->unit,address,arg);
 if(b->which==0 && write)n+=snprintf(command+n,sizeof command-(size_t)n," %u",arg);
 if(j && write)j->write_started=true;
 if(send(b->fd,command,(size_t)n,0)!=n){reason(b->error,"udp_send_failed");goto failure;}
 uint64_t end=now_ms()+udp_deadline_ms;struct jd d;
 for(;;){
  if(atomic_load(&stopping)){reason(b->error,"service_stopping");goto failure;}
  uint64_t now=now_ms();if(now>=end){reason(b->error,"udp_timeout_unknown");add_trace(j,b->which,b->seq,NULL);goto failure;}
  struct pollfd p={b->fd,POLLIN,0};int ret=poll(&p,1,(int)(end-now));
  if(ret<0&&errno==EINTR)continue;
  if(ret<=0){reason(b->error,"udp_timeout_unknown");add_trace(j,b->which,b->seq,NULL);goto failure;}
  ssize_t size=recv(b->fd,raw,sizeof raw-1,MSG_TRUNC);
  if(size<0){if(errno==EINTR||errno==EAGAIN)continue;reason(b->error,"udp_receive_failed");goto failure;}
  if(size>=(ssize_t)sizeof raw-1 || memchr(raw,0,(size_t)size))continue;
  raw[size]=0;
  uint64_t seq;if(jparse(&d,raw,(size_t)size)||jfield(&d,0,"seq",&seq)||seq!=b->seq)continue;
  break;
 }
 add_trace(j,b->which,b->seq,raw);
 const char *errors_can[]={"timeout","tx_fail","mismatch","ignored","busy","resets","reset_fail","invalid","udp_fail","duplicate"};
 const char *errors_rs[]={"timeout","tx_fail","bad_reply","exception","busy","duplicate","ignored","udp_fail"};
 const char **errors=b->which?errors_rs:errors_can;unsigned ecount=b->which?8:10;
 int st=jget(&d,0,"stats");uint64_t c[20]={0};bool counters_ok=true;
 if(jfield(&d,st,"attempts",&c[0])||jfield(&d,st,"ok",&c[1]))counters_ok=false;
 for(unsigned i=0;i<ecount;i++)if(jfield(&d,st,errors[i],&c[2+i]))counters_ok=false;
 if(!counters_ok){reason(b->error,"invalid_gateway_counters");goto failure;}
 b->c.attempts=c[0];b->c.ok=c[1];b->c.timeout=c[2];b->c.tx_fail=c[3];b->c.bad_reply=c[4];
 if(!jeq(&d,jget(&d,0,"status"),"ok")){
  char status[40];if(jstr(&d,jget(&d,0,"status"),status,sizeof status))strcpy(status,"invalid_status");
  snprintf(b->error,sizeof b->error,"gateway_%.39s",status);goto failure;
 }
 if(b->baseline){
  if(c[0]!=b->previous[0]+1 || c[1]!=b->previous[1]+1){reason(b->error,"extra_sender_or_counter_reset");goto failure;}
  for(unsigned i=0;i<ecount;i++)if(c[2+i]!=b->previous[2+i]){reason(b->error,"new_gateway_error");goto failure;}
 }
 if(!numeq(&d,"v",2)||!jeq(&d,jget(&d,0,"type"),b->which?"rs485_reply":"can_rw_reply")||
    !jeq(&d,jget(&d,0,"op"),write?"WRITE":"READ")||!numeq(&d,"unit",b->unit)||!numeq(&d,"address",address)||
    !numeq(&d,"count",write||!b->which?1:arg)||!numeq(&d,"value",write?arg:0)||
    !jeq(&d,jget(&d,0,"execution"),write?"acknowledged":"read")){
  reason(b->error,"response_identity");goto failure;
 }
 uint8_t data[256];int length=hex_decode(&d,"data",data,sizeof data);if(length<0){reason(b->error,"invalid_hex");goto failure;}
 unsigned count=write?0:b->which?arg:1;uint16_t decoded[8]={0};
 if(!b->which){
  uint8_t request[8];int reqlen=hex_decode(&d,"request_data",request,sizeof request);
  uint8_t expected[8];for(unsigned i=0;i<4;i++)expected[i]=(uint8_t)(b->seq>>(8*i));
  expected[4]=write?2:1;expected[5]=(uint8_t)address;expected[6]=write?(uint8_t)(arg>>8):0;expected[7]=write?(uint8_t)arg:0;
  if(reqlen!=8||memcmp(request,expected,8)||!numeq(&d,"request_can_id",0x210+b->unit)||!numeq(&d,"reply_can_id",0x190+b->unit)||
     length!=8||memcmp(data,expected,4)||data[4]!=(expected[4]|0x80)||data[5]!=address){reason(b->error,"can_raw_identity");goto failure;}
  unsigned v=((unsigned)data[6]<<8)|data[7];if(write && v!=arg){reason(b->error,"can_write_echo");goto failure;}decoded[0]=(uint16_t)v;
 }else{
  if(crc16(data,(size_t)length)||length<4||data[0]!=b->unit||data[1]!=(write?6:3)){reason(b->error,"modbus_crc_or_function");goto failure;}
  if(write){if(length!=8||data[2]!=(address>>8)||data[3]!=(address&255)||data[4]!=(arg>>8)||data[5]!=(arg&255)){reason(b->error,"fc06_echo");goto failure;}}
  else {if(length!=(int)(5+2*arg)||data[2]!=2*arg){reason(b->error,"fc03_length");goto failure;}for(unsigned i=0;i<count;i++)decoded[i]=(uint16_t)(((unsigned)data[3+2*i]<<8)|data[4+2*i]);}
  int pl=jget(&d,0,"pl");uint64_t pc[7];const char *keys[]={"good","tx","crc","uart","gap","overrun","watchdog"};
  for(unsigned i=0;i<7;i++)if(jfield(&d,pl,keys[i],&pc[i])){reason(b->error,"invalid_pl_counters");goto failure;}
  b->c.crc=pc[2];b->c.uart=pc[3];b->c.gap=pc[4];b->c.overrun=pc[5];b->c.watchdog=pc[6];
  if(b->baseline){if(pc[0]!=(uint32_t)(b->previous[12]+1)||pc[1]!=(uint32_t)(b->previous[13]+1)){reason(b->error,"pl_counter_reset_or_extra_frame");goto failure;}
   for(unsigned i=2;i<7;i++)if(pc[i]!=b->previous[12+i]){reason(b->error,"new_pl_error");goto failure;}}
  for(unsigned i=0;i<7;i++)c[12+i]=pc[i];
 }
 int regs=jget(&d,0,"registers");if(regs<0||d.t[regs].type!='['){reason(b->error,"invalid_register_array");goto failure;}
 unsigned cursor=(unsigned)regs+1;
 for(unsigned i=0;i<count;i++){uint64_t v;if(cursor>=d.t[regs].next||jnum(&d,(int)cursor,&v)||v!=decoded[i]){reason(b->error,"register_raw_mismatch");goto failure;}cursor=d.t[cursor].next;}
 if(cursor!=d.t[regs].next){reason(b->error,"extra_register_values");goto failure;}
 if(jfield(&d,0,b->which?"rs485_rtt_us":"can_rtt_us",&b->last_rtt)||!b->last_rtt){reason(b->error,"invalid_rtt");goto failure;}
 memcpy(b->previous,c,sizeof c);b->baseline=true;
 if(values)for(unsigned i=0;i<count;i++)values[i]=decoded[i];
 if(j&&write)j->write_acked=true;
 if(j&&j->error[0]){reason(b->error,j->error);goto failure;}
 b->last_local_us=npu_now_us()-local_begin;return 0;
failure:
 b->last_local_us=npu_now_us()-local_begin;b->baseline=false;return -1;
}
static int read_one(unsigned bus,unsigned address,uint16_t *value,struct job *j){return exchange(&buses[bus],false,address,1,value,j);}
static int read_meta(unsigned bus,uint16_t meta[4],struct job *j){
 if(bus)return exchange(&buses[bus],false,4,4,meta,j);
 for(unsigned i=0;i<4;i++)if(read_one(bus,4+i,meta+i,j))return -1;
 return 0;
}
static int checked_meta(const uint16_t before[4],const uint16_t after[4],unsigned address,unsigned value,unsigned increment){
 return before[0]==0x0801 && after[0]==0x0801 && after[1]==(uint16_t)(before[1]+increment) && after[2]==address && after[3]==value?0:-1;
}
static void run_job(struct job *j){
 buses[0].unit=buses[1].unit=j->cmd.unit;
 uint16_t before[4],after[4],old,actual,rev;unsigned bus=j->cmd.bus,address=j->cmd.address;j->begin=now_ms();
 if(j->cmd.op==0){if(read_one(bus,address,&actual,j))goto transport;j->readback=actual;j->after_valid=true;goto pass;}
 unsigned identity_bus=bus==1?1:0;
 if(read_one(identity_bus,8,&rev,j)){reason(j->error,buses[identity_bus].error);goto review;}
 if(rev!=0x0901){reason(j->error,"slave_r9_revision_required");goto review;}
 if(j->cmd.op==2)bus=1;
 if(read_meta(bus,before,j))goto transport;
 if(before[0]!=0x0801){reason(j->error,"register_revision_required");goto review;}
 if(read_one(bus,address,&old,j))goto transport;
 j->before=old;j->before_valid=true;j->count_before=before[1];j->count_valid=true;
 if(j->cmd.op==1){
  if(old==j->cmd.value){j->unchanged=true;j->readback=old;j->after_valid=true;j->count_after=before[1];goto pass;}
  if(exchange(&buses[bus],true,address,j->cmd.value,NULL,j)||read_one(bus,address,&actual,j)||read_meta(bus,after,j))goto transport;
  j->readback=actual;j->after_valid=true;j->count_after=after[1];
  if(actual!=j->cmd.value||checked_meta(before,after,address,j->cmd.value,1)){reason(j->error,"independent_readback_or_count");goto review;}
 }else{
  uint16_t via_can;if(read_one(0,address,&via_can,j))goto can_error;
  if(via_can!=old){reason(j->error,"initial_cross_view_mismatch");goto review;}
  j->can_written=old^0x5a5a;j->rs_written=j->can_written^0xa5a5;
  if(exchange(&buses[0],true,address,j->can_written,NULL,j))goto can_error;
  if(read_one(1,address,&actual,j)||read_meta(1,after,j))goto transport;
  if(actual!=j->can_written||checked_meta(before,after,address,j->can_written,1)){reason(j->error,"can_write_rs485_readback");goto review;}
  if(exchange(&buses[1],true,address,j->rs_written,NULL,j))goto transport;
  if(read_one(0,address,&actual,j))goto can_error;
  if(read_meta(1,after,j))goto transport;
  j->readback=actual;j->after_valid=true;j->count_after=after[1];
  if(actual!=j->rs_written||checked_meta(before,after,address,j->rs_written,2)){reason(j->error,"rs485_write_can_readback");goto review;}
 }
pass:j->status=2;j->end=now_ms();return;
can_error:reason(j->error,buses[0].error);goto review;
transport:reason(j->error,buses[bus].error);
review:j->status=3;j->end=now_ms();
}
static void sample_bus(unsigned which,unsigned unit,struct snapshot *s){
 buses[which].unit=unit;
 memset(s,0,sizeof *s);struct bus *b=&buses[which];
 if(!which){for(unsigned i=0;i<32;i++){if(read_one(0,i,&s->values[i],NULL))goto bad;if(b->last_rtt>s->rtt)s->rtt=b->last_rtt;if(b->last_local_us>s->local_us)s->local_us=b->last_local_us;}}
 else {for(unsigned i=0;i<32;i+=8){if(exchange(b,false,i,8,s->values+i,NULL))goto bad;if(b->last_rtt>s->rtt)s->rtt=b->last_rtt;if(b->last_local_us>s->local_us)s->local_us=b->last_local_us;}}
 if(s->values[0]!=0x1234||s->values[1]!=unit||s->values[4]!=0x0801||s->values[8]!=0x0901){reason(s->error,"slave_identity_mismatch");goto done;}
 s->valid=true;goto done;
bad:reason(s->error,b->error);if(b->last_local_us>s->local_us)s->local_us=b->last_local_us;
done:s->sampled=now_ms();s->c=b->c;
}
struct sample_call {unsigned which,unit;struct snapshot *snapshot;};
static void *sample_async(void *pointer){struct sample_call *call=pointer;sample_bus(call->which,call->unit,call->snapshot);return NULL;}
static void *worker(void *unused){
 (void)unused;uint64_t next=0;
 while(!atomic_load(&stopping)){
  int active;struct job *local=NULL;
  pthread_mutex_lock(&state.mutex);active=state.active;
  if(active>=0){state.jobs[active].status=1;local=malloc(sizeof *local);if(local)*local=state.jobs[active];else{state.jobs[active].status=3;reason(state.jobs[active].error,"memory_unavailable");state.active=-1;}}
  pthread_mutex_unlock(&state.mutex);
  if(local){run_job(local);pthread_mutex_lock(&state.mutex);state.jobs[active]=*local;state.active=-1;pthread_mutex_unlock(&state.mutex);
   printf("WEB_JOB {\"id\":\"%s\",\"bus\":\"%s\",\"unit\":%u,\"op\":\"%s\",\"status\":\"%s\",\"error\":\"%s\",\"trace\":[%s]}\n",local->cmd.id,bus_name(local->cmd.bus),local->cmd.unit,op_name(local->cmd.op),local->status==2?"PASS":"REVIEW",local->error,local->trace);free(local);continue;}
  uint64_t now=now_ms();
  if(now>=next){
   for(unsigned unit=1;unit<=nodes&&!atomic_load(&stopping);unit++) {
   struct snapshot snap[2];struct sample_call call={0,unit,&snap[0]};pthread_t can_sampler;
   if(!pthread_create(&can_sampler,NULL,sample_async,&call)){sample_bus(1,unit,&snap[1]);pthread_join(can_sampler,NULL);}
   else {sample_bus(0,unit,&snap[0]);sample_bus(1,unit,&snap[1]);}
   if(atomic_load(&stopping))break;
   pthread_mutex_lock(&state.mutex);
   state.device[unit-1][0]=snap[0];state.device[unit-1][1]=snap[1];
   if(unit==1){state.snap[0]=snap[0];state.snap[1]=snap[1];npu_generation++;struct point *h=&state.history[state.history_next];
   *h=(struct point){now_ms()-started,snap[0].rtt,snap[1].rtt,snap[0].valid,snap[1].valid};
   state.history_next=(state.history_next+1)%HISTORY;if(state.history_count<HISTORY)state.history_count++;}
   pthread_mutex_unlock(&state.mutex);
   }
   next=now_ms()+period_ms;
  }else {struct timespec t={0,50000000};nanosleep(&t,NULL);}
 }
 return NULL;
}
static void print_job(FILE *f,const struct job *j,bool trace){
 fprintf(f,"{\"id\":\"%s\",\"bus\":\"%s\",\"op\":\"%s\",\"unit\":%u,\"address\":%u,\"value\":%u,\"status\":\"%s\",\"error\":\"%s\",\"execution\":\"%s\",\"begin_ms\":%"PRIu64",\"end_ms\":%"PRIu64",",
 j->cmd.id,bus_name(j->cmd.bus),op_name(j->cmd.op),j->cmd.unit,j->cmd.address,j->cmd.value,j->status==0?"QUEUED":j->status==1?"RUNNING":j->status==2?"PASS":"REVIEW",j->error,
 j->status<2?"pending":j->status==2?(j->cmd.op==0?"read":j->unchanged?"unchanged":"verified"):j->write_started?"unknown_or_unverified":"not_written",j->begin?j->begin-started:0,j->end?j->end-started:0);
 fprintf(f,"\"before\":");if(j->before_valid)fprintf(f,"%u",j->before);else fputs("null",f);
 fputs(",\"readback\":",f);if(j->after_valid)fprintf(f,"%u",j->readback);else fputs("null",f);
 fputs(",\"write_count_before\":",f);if(j->count_valid)fprintf(f,"%u",j->count_before);else fputs("null",f);
 fputs(",\"write_count_after\":",f);if(j->count_valid&&j->after_valid)fprintf(f,"%u",j->count_after);else fputs("null",f);
 fprintf(f,",\"can_written\":%u,\"rs485_written\":%u",j->can_written,j->rs_written);
 if(trace)fprintf(f,",\"trace\":[%s]",j->trace);
 fputc('}',f);
}
static uint64_t sys_counter(const char *name,bool *available){
 char path[160];snprintf(path,sizeof path,"/sys/class/net/can0/statistics/%s",name);FILE *f=fopen(path,"r");uint64_t n=0;
 if(!f){*available=false;return 0;}if(fscanf(f,"%"SCNu64,&n)!=1)*available=false;fclose(f);return n;
}
#include "uplink_snapshot.h"
static char *state_json(size_t *length,bool evidence){
 char *out=NULL;FILE *f=open_memstream(&out,length);if(!f)return NULL;
 fprintf(f,"{\"schema\":\"IGW_WEB_R10\",\"session\":\"%s\",\"uptime_ms\":%"PRIu64",\"sample_interval_ms\":%u",session,now_ms()-started,period_ms);
 if(!evidence)fprintf(f,",\"token\":\"%s\"",token);
 pthread_mutex_lock(&state.mutex);fprintf(f,",\"busy\":%s,\"buses\":{",state.active>=0?"true":"false");
 for(unsigned b=0;b<2;b++){
  struct snapshot *s=&state.snap[b];fprintf(f,"%s\"%s\":{\"valid\":%s,\"age_ms\":",b?",":"",bus_name(b),s->valid?"true":"false");
  if(s->sampled)fprintf(f,"%"PRIu64,now_ms()-s->sampled);else fputs("null",f);
  fprintf(f,",\"rtt_us\":%"PRIu64",\"error\":\"%s\",\"values\":[",s->rtt,s->error);
  for(unsigned i=0;i<32;i++){if(i)fputc(',',f);if(s->valid)fprintf(f,"%u",s->values[i]);else fputs("null",f);}
  fprintf(f,"],\"stats\":{\"attempts\":%"PRIu64",\"ok\":%"PRIu64",\"timeout\":%"PRIu64",\"tx_fail\":%"PRIu64",\"bad_reply\":%"PRIu64",\"crc\":%"PRIu64",\"uart\":%"PRIu64",\"gap\":%"PRIu64",\"overrun\":%"PRIu64",\"watchdog\":%"PRIu64"}}",s->c.attempts,s->c.ok,s->c.timeout,s->c.tx_fail,s->c.bad_reply,s->c.crc,s->c.uart,s->c.gap,s->c.overrun,s->c.watchdog);
 }
 fputs("},\"devices\":[",f);
 for(unsigned unit=1;unit<=nodes;unit++){
  fprintf(f,"%s{\"unit\":%u,\"buses\":{",unit>1?",":"",unit);
  for(unsigned b=0;b<2;b++){
   struct snapshot *s=&state.device[unit-1][b];
   fprintf(f,"%s\"%s\":{\"valid\":%s,\"age_ms\":",b?",":"",bus_name(b),s->valid?"true":"false");
   if(s->sampled)fprintf(f,"%"PRIu64,now_ms()-s->sampled);else fputs("null",f);
   fprintf(f,",\"rtt_us\":%"PRIu64",\"error\":\"%s\",\"values\":[",s->rtt,s->error);
   for(unsigned i=0;i<32;i++){if(i)fputc(',',f);if(s->valid)fprintf(f,"%u",s->values[i]);else fputs("null",f);}
   fprintf(f,"],\"stats_scope\":\"shared_bus\",\"stats\":{\"attempts\":%"PRIu64",\"ok\":%"PRIu64",\"timeout\":%"PRIu64",\"tx_fail\":%"PRIu64",\"bad_reply\":%"PRIu64",\"crc\":%"PRIu64",\"uart\":%"PRIu64",\"gap\":%"PRIu64",\"overrun\":%"PRIu64",\"watchdog\":%"PRIu64"}}",s->c.attempts,s->c.ok,s->c.timeout,s->c.tx_fail,s->c.bad_reply,s->c.crc,s->c.uart,s->c.gap,s->c.overrun,s->c.watchdog);
  }fputs("}}",f);
 }
 fprintf(f,"],\"device_count\":%u,\"npu_scope_unit\":1,\"history_scope_unit\":1,\"history\":[",nodes);
 for(unsigned k=0;k<state.history_count;k++){unsigned i=(state.history_next+HISTORY-state.history_count+k)%HISTORY;struct point *h=&state.history[i];
  fprintf(f,"%s{\"ms\":%"PRIu64",\"CAN\":",k?",":"",h->at);if(h->can_valid)fprintf(f,"%"PRIu64,h->can_us);else fputs("null",f);
  fputs(",\"RS485\":",f);if(h->rs_valid)fprintf(f,"%"PRIu64,h->rs_us);else fputs("null",f);fputc('}',f);
 }
 fputs("],\"jobs\":[",f);bool comma=false;
 for(unsigned i=0;i<JOBS;i++)if(state.jobs[i].cmd.id[0]){if(comma)fputc(',',f);print_job(f,&state.jobs[i],evidence);comma=true;}
 fputc(']',f);npu_json_locked(f);pthread_mutex_unlock(&state.mutex);uplink_json(f);fputs(",\"kernel_can\":{",f);bool available=true;
 uint64_t rx=sys_counter("rx_packets",&available),tx=sys_counter("tx_packets",&available),re=sys_counter("rx_errors",&available),te=sys_counter("tx_errors",&available),drop=sys_counter("rx_dropped",&available);
 fprintf(f,"\"available\":%s,\"rx_packets\":%"PRIu64",\"tx_packets\":%"PRIu64",\"rx_errors\":%"PRIu64",\"tx_errors\":%"PRIu64",\"rx_dropped\":%"PRIu64"}}",available?"true":"false",rx,tx,re,te,drop);fclose(f);return out;
}
/* Local read-only evidence endpoint: plaintext remains in process memory and
 * kernel socket buffers. No JSON spool is written to SD or /tmp. */
static int archive_fd=-1;
static void *archive_exporter(void *unused){
 (void)unused;
 while(!atomic_load(&stopping)){
  struct pollfd p={archive_fd,POLLIN,0};if(poll(&p,1,200)<=0)continue;
  unsigned char request=0;ssize_t received=recv(archive_fd,&request,1,0);
  if(received<0&&(errno==EAGAIN||errno==EINTR))continue;
  if(received!=1||request!='R')break;
  size_t length=0;char *data=state_json(&length,true);unsigned char header[8];
  for(unsigned i=0;i<8;i++)header[i]=(unsigned char)((uint64_t)length>>(56-8*i));
  uint64_t end=now_ms()+2500;bool failed=!data;
  for(unsigned part=0;part<2&&!failed;part++){
   const unsigned char *out=part?(const unsigned char *)data:header;size_t count=part?length:sizeof header,sent=0;
   while(sent<count&&!atomic_load(&stopping)&&now_ms()<end){
    struct pollfd ready={archive_fd,POLLOUT,0};if(poll(&ready,1,100)<=0)continue;
    ssize_t k=send(archive_fd,out+sent,count-sent,MSG_NOSIGNAL);
    if(k<0&&(errno==EAGAIN||errno==EINTR))continue;
    if(k<=0)break;
    sent+=(size_t)k;
   }
   if(sent!=count)failed=true;
  }
  free(data);if(failed)break;
 }
 shutdown(archive_fd,SHUT_RDWR);return NULL;
}
static int archive_validate(int fd){
 struct ucred cred={0};socklen_t n=sizeof cred;int type=0;socklen_t ts=sizeof type;int flags=fcntl(fd,F_GETFL,0);
 if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&cred,&n)||cred.uid!=geteuid()||getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&ts)||type!=SOCK_STREAM||flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK))return -1;
 return 0;
}
static bool is_id(const char *s){if(strlen(s)!=32)return false;for(unsigned i=0;i<32;i++)if(!strchr("0123456789abcdef",s[i]))return false;return true;}
static int parse_command(const char *body,size_t n,struct command *cmd){
 struct jd d;if(jparse(&d,body,n)||d.t[0].type!='{')return -1;unsigned fields=0;
 for(unsigned i=1;i<d.t[0].next;){fields++;i=d.t[d.t[i].next].next;}if(fields!=5&&fields!=6)return -1;
 uint64_t unit=1;int unit_token=jget(&d,0,"unit");
 if(unit_token>=0&&jnum(&d,unit_token,&unit))return -1;
 if(unit<1||unit>nodes||(fields==6&&unit_token<0))return -1;
 cmd->unit=(unsigned)unit;
 char bus[12],op[12];uint64_t address,value;
 if(jstr(&d,jget(&d,0,"id"),cmd->id,sizeof cmd->id)||!is_id(cmd->id)||jstr(&d,jget(&d,0,"bus"),bus,sizeof bus)||jstr(&d,jget(&d,0,"op"),op,sizeof op)||jfield(&d,0,"address",&address)||jfield(&d,0,"value",&value)||address>31||value>65535)return -1;
 cmd->bus=!strcmp(bus,"CAN")?0:!strcmp(bus,"RS485")?1:!strcmp(bus,"BOTH")?2:99;
 cmd->op=!strcmp(op,"READ")?0:!strcmp(op,"WRITE")?1:!strcmp(op,"CROSS")?2:99;
 if(cmd->bus>2||cmd->op>2||(cmd->op==2)!=(cmd->bus==2)||(cmd->op>0&&address<16)||(cmd->op!=1&&value!=0))return -1;
 cmd->address=(unsigned)address;cmd->value=(unsigned)value;return 0;
}
static int send_all(int fd,const void *data,size_t n){
 const char *p=data;uint64_t end=now_ms()+2000;
 while(n){uint64_t now=now_ms();if(now>=end)return -1;struct pollfd f={fd,POLLOUT,0};if(poll(&f,1,(int)(end-now))<=0)return -1;ssize_t sent=send(fd,p,n,MSG_NOSIGNAL);if(sent<0){if(errno==EINTR||errno==EAGAIN)continue;return -1;}p+=sent;n-=(size_t)sent;}return 0;
}
static void response(int fd,int code,const char *type,const char *body,size_t n,bool download){
 char header[1024];int len=snprintf(header,sizeof header,"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'\r\n%s\r\n",code,code==200?"OK":code==202?"Accepted":"Error",type,n,download?"Content-Disposition: attachment; filename=igw_r13_evidence.json\r\n":"");
 if(send_all(fd,header,(size_t)len)==0)(void)send_all(fd,body,n);
}
static void json_error(int fd,int status,const char *error){char s[160];int n=snprintf(s,sizeof s,"{\"error\":\"%s\"}",error);response(fd,status,"application/json",s,(size_t)n,false);}
static void job_response(int fd,int status,const struct job *j){char *text=NULL;size_t n;FILE *f=open_memstream(&text,&n);if(!f){json_error(fd,503,"memory_unavailable");return;}fputs("{\"job\":",f);print_job(f,j,true);fputc('}',f);fclose(f);response(fd,status,"application/json",text,n,false);free(text);}
static void *http_client(void *pointer){
 int fd=*(int *)pointer;free(pointer);char request[8193];size_t have=0,headers=0,wanted=0;uint64_t deadline=now_ms()+2000;
 char method[8]="",path[100]="",version[16]="",host[100]="",request_origin[100]="",csrf[80]="",content_type[80]="";
 bool has_length=false;
 while(!atomic_load(&stopping)){
  if(have>=8192){json_error(fd,413,"request_too_large");goto done;}
  uint64_t now=now_ms();if(now>=deadline){json_error(fd,408,"request_timeout");goto done;}
  struct pollfd p={fd,POLLIN,0};int ready=poll(&p,1,(int)(deadline-now));if(ready<0&&errno==EINTR)continue;if(ready<=0)goto done;
  size_t previous_have=have;ssize_t n=recv(fd,request+have,8192-have,0);if(n<=0)goto done;have+=(size_t)n;request[have]=0;
  if(memchr(request+previous_have,0,(size_t)n)){json_error(fd,400,"nul_byte");goto done;}
  if(!headers){char *end=strstr(request,"\r\n\r\n");if(!end)continue;headers=(size_t)(end-request)+4;
   char *line=strstr(request,"\r\n");if(!line){json_error(fd,400,"request_line");goto done;}*line=0;char extra;
   if(sscanf(request,"%7s %99s %15s %c",method,path,version,&extra)!=3||strcmp(version,"HTTP/1.1")){json_error(fd,400,"request_line");goto done;}
   char *h=line+2;
   while(h<end){char *e=strstr(h,"\r\n");if(!e || e>end){json_error(fd,400,"headers");goto done;}*e=0;char *colon=strchr(h,':');if(!colon){json_error(fd,400,"headers");goto done;}*colon++=0;while(*colon==' '||*colon=='\t')colon++;
    char *target=NULL;size_t cap=0;
    if(!strcasecmp(h,"Host")){target=host;cap=sizeof host;}
    else if(!strcasecmp(h,"Origin")){target=request_origin;cap=sizeof request_origin;}
    else if(!strcasecmp(h,"X-IGW-Token")){target=csrf;cap=sizeof csrf;}
    else if(!strcasecmp(h,"Content-Type")){target=content_type;cap=sizeof content_type;}
    else if(!strcasecmp(h,"Transfer-Encoding")){json_error(fd,400,"transfer_encoding_unsupported");goto done;}
    else if(!strcasecmp(h,"Content-Length")){
     if(has_length||!*colon){json_error(fd,400,"content_length");goto done;}has_length=true;wanted=0;
     for(const char *q=colon;*q;q++){if(*q<'0'||*q>'9'||wanted>4096/10){json_error(fd,413,"content_length");goto done;}wanted=wanted*10+(unsigned)(*q-'0');}
     if(wanted>4096){json_error(fd,413,"body_too_large");goto done;}
    }
    if(target){if(*target||strlen(colon)>=cap){json_error(fd,400,"duplicate_or_long_header");goto done;}strcpy(target,colon);}h=e+2;
   }
   if(strcmp(host,host_header)){json_error(fd,421,"host_mismatch");goto done;}
   if(*request_origin && strcmp(request_origin,origin)){json_error(fd,403,"origin_mismatch");goto done;}
  }
  if(have>=headers+wanted)break;
 }
 if(atomic_load(&stopping))goto done;
 if(have!=headers+wanted){json_error(fd,400,"extra_bytes");goto done;}
 if(!strcmp(method,"GET")){
  if(wanted){json_error(fd,400,"get_body");goto done;}
  if(!strcmp(path,"/")){response(fd,200,"text/html; charset=utf-8",(const char *)web_html,web_html_len,false);goto done;}
  if(!strcmp(path,"/api/state")||!strcmp(path,"/api/evidence")){size_t n;bool evidence=!strcmp(path,"/api/evidence");char *s=state_json(&n,evidence);if(!s){json_error(fd,503,"memory_unavailable");goto done;}response(fd,200,"application/json",s,n,evidence);free(s);goto done;}
  if(!strncmp(path,"/api/job/",9)&&is_id(path+9)){
   struct job *copy=malloc(sizeof *copy);bool found=false;if(!copy){json_error(fd,503,"memory_unavailable");goto done;}
   pthread_mutex_lock(&state.mutex);for(unsigned i=0;i<JOBS;i++)if(!strcmp(state.jobs[i].cmd.id,path+9)){*copy=state.jobs[i];found=true;break;}pthread_mutex_unlock(&state.mutex);
   if(found)job_response(fd,200,copy);else json_error(fd,404,"job_not_found");free(copy);goto done;
  }
  json_error(fd,404,"not_found");goto done;
 }
 if(strcmp(method,"POST")||strcmp(path,"/api/job")){json_error(fd,405,"method_or_path");goto done;}
 if(!has_length||strcmp(content_type,"application/json")||strcmp(csrf,token)){json_error(fd,403,"token_or_content_type");goto done;}
 struct command cmd;if(parse_command(request+headers,wanted,&cmd)){json_error(fd,400,"invalid_command");goto done;}
 struct job *copy=malloc(sizeof *copy);if(!copy){json_error(fd,503,"memory_unavailable");goto done;}
 int slot=-1,code=202;const char *error=NULL;uint64_t now=now_ms();
 pthread_mutex_lock(&state.mutex);
 for(unsigned i=0;i<JOBS;i++)if(state.jobs[i].cmd.id[0]&&!strcmp(state.jobs[i].cmd.id,cmd.id)){
  if(same_cmd(&state.jobs[i].cmd,&cmd)){*copy=state.jobs[i];code=200;}else error="job_id_conflict";slot=(int)i;break;
 }
 if(slot<0){
  if(state.active>=0)error="worker_busy";
  else {for(unsigned i=0;i<JOBS;i++)if(!state.jobs[i].cmd.id[0]||now>=state.jobs[i].until){slot=(int)i;break;}
   if(slot<0)error="job_cache_full";
   else {memset(&state.jobs[slot],0,sizeof state.jobs[slot]);state.jobs[slot].cmd=cmd;state.jobs[slot].until=now+600000;state.active=slot;*copy=state.jobs[slot];}
  }
 }
 pthread_mutex_unlock(&state.mutex);
 if(error)json_error(fd,409,error);else job_response(fd,code,copy);free(copy);
done:
 close(fd);pthread_mutex_lock(&state.mutex);state.http_active--;pthread_mutex_unlock(&state.mutex);return NULL;
}
static int random_hex(char out[33]){unsigned char b[16];int f=open("/dev/urandom",O_RDONLY);if(f<0)return -1;ssize_t n=read(f,b,sizeof b);close(f);if(n!=sizeof b)return -1;for(unsigned i=0;i<16;i++)snprintf(out+2*i,3,"%02x",b[i]);return 0;}
static unsigned option_number(const char *s,unsigned low,unsigned high){char *end;errno=0;unsigned long n=strtoul(s,&end,10);if(errno||!*s||*end||n<low||n>high){fputs("Invalid number\n",stderr);exit(2);}return (unsigned)n;}
int main(int argc,char **argv){
 const char *bind_ip="192.168.31.222",*peer="192.168.31.100";unsigned port=8080,can_port=16000,rs_port=16001;
 for(int i=1;i<argc;i++){if(i+1>=argc)return 2;const char *key=argv[i],*value=argv[++i];
  if(!strcmp(key,"--npu-port"))npu_port=option_number(value,1,65535);else if(!strcmp(key,"--archive-fd"))archive_fd=(int)option_number(value,3,1024);else if(!strcmp(key,"--bind"))bind_ip=value;else if(!strcmp(key,"--peer"))peer=value;
  else if(!strcmp(key,"--port"))port=option_number(value,1,65535);
  else if(!strcmp(key,"--can-port"))can_port=option_number(value,1,65535);
  else if(!strcmp(key,"--rs-port"))rs_port=option_number(value,1,65535);
  else if(!strcmp(key,"--udp-timeout-ms"))udp_deadline_ms=option_number(value,300,2000);
  else if(!strcmp(key,"--nodes"))nodes=option_number(value,1,16);
  else if(!strcmp(key,"--period-ms"))period_ms=option_number(value,3000,30000);else return 2;
 }
 struct sockaddr_in address={0};
 address.sin_family=AF_INET;address.sin_port=htons((uint16_t)port);
 if(inet_pton(AF_INET,bind_ip,&address.sin_addr)!=1||address.sin_addr.s_addr==INADDR_ANY||inet_pton(AF_INET,peer,&allowed_peer)!=1)return 2;
 if(random_hex(session)||random_hex(token))return 2;
 snprintf(host_header,sizeof host_header,"%s:%u",bind_ip,port);snprintf(origin,sizeof origin,"http://%s",host_header);
 started=now_ms();setvbuf(stdout,NULL,_IOLBF,0);
 for(unsigned i=0;i<2;i++){
  struct bus *b=&buses[i];b->which=i;b->unit=1;char seed[9];memcpy(seed,session+8*i,8);seed[8]=0;b->seq=(uint32_t)strtoul(seed,NULL,16);b->fd=socket(AF_INET,SOCK_DGRAM,0);
  struct sockaddr_in endpoint={0};
  endpoint.sin_family=AF_INET;endpoint.sin_port=htons((uint16_t)(i?rs_port:can_port));endpoint.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(b->fd<0||connect(b->fd,(struct sockaddr *)&endpoint,sizeof endpoint))return 2;
 }
 int listener=socket(AF_INET,SOCK_STREAM,0),one=1;if(listener<0)return 2;
 if(setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one)||bind(listener,(struct sockaddr *)&address,sizeof address)||listen(listener,8)){perror("HTTP listen");return 2;}
 int flags=fcntl(listener,F_GETFL,0);if(flags<0||fcntl(listener,F_SETFL,flags|O_NONBLOCK))return 2;
 struct sigaction sa={0};sa.sa_handler=stop_handler;sigemptyset(&sa.sa_mask);sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);signal(SIGPIPE,SIG_IGN);
 pthread_t work,exporter,npu;
 if(archive_fd>=0&&archive_validate(archive_fd)){perror("archive fd");close(listener);return 2;}
 if(pthread_create(&work,NULL,worker,NULL)){if(archive_fd>=0)close(archive_fd);return 2;}
 if(archive_fd>=0&&pthread_create(&exporter,NULL,archive_exporter,NULL)){atomic_store(&stopping,true);pthread_join(work,NULL);close(archive_fd);close(listener);return 2;}
 if(npu_port&&pthread_create(&npu,NULL,npu_worker,NULL)){atomic_store(&stopping,true);pthread_join(work,NULL);if(archive_fd>=0)pthread_join(exporter,NULL);return 2;}
 if(archive_fd>=0)printf("ARCHIVE_EXPORT_FD=%d; read-only; inherited socketpair; excludes control token\n",archive_fd);
 printf("[READY] IGW_WEB_R13 http://%s peer=%s; private addressed backends127.0.0.1:%u/%u; reads only until operator command\n",host_header,peer,can_port,rs_port);
 while(!atomic_load(&stopping)){
  struct pollfd p={listener,POLLIN,0};if(poll(&p,1,200)<=0)continue;
  struct sockaddr_in client;socklen_t length=sizeof client;int fd=accept(listener,(struct sockaddr *)&client,&length);if(fd<0)continue;
  int client_flags=fcntl(fd,F_GETFL,0);if(client_flags<0||fcntl(fd,F_SETFL,client_flags|O_NONBLOCK)){close(fd);continue;}
  if(client.sin_addr.s_addr!=allowed_peer.s_addr){json_error(fd,403,"peer_not_allowed");close(fd);continue;}
  pthread_mutex_lock(&state.mutex);bool full=state.http_active>=8;if(!full)state.http_active++;pthread_mutex_unlock(&state.mutex);
  if(full){json_error(fd,503,"http_busy");close(fd);continue;}
  int *arg=malloc(sizeof *arg);pthread_t thread;
  if(arg){*arg=fd;if(!pthread_create(&thread,NULL,http_client,arg)){pthread_detach(thread);continue;}free(arg);}
  close(fd);pthread_mutex_lock(&state.mutex);state.http_active--;pthread_mutex_unlock(&state.mutex);
 }
 close(listener);pthread_join(work,NULL);if(npu_port)pthread_join(npu,NULL);
 if(archive_fd>=0){pthread_join(exporter,NULL);close(archive_fd);}
 for(;;){pthread_mutex_lock(&state.mutex);unsigned count=state.http_active;pthread_mutex_unlock(&state.mutex);if(!count)break;struct timespec t={0,10000000};nanosleep(&t,NULL);}
 for(unsigned i=0;i<2;i++)close(buses[i].fd);
 puts("[STOP] IGW_WEB_R13 stopped; no automatic write retry");return 0;
}
