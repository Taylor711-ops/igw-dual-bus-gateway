#ifndef IGW_NPU_BRIDGE_H
#define IGW_NPU_BRIDGE_H
#include "npu_model.h"
#define NPU_CONTRACT "ps_snapshot_window_r12_transfer"
#define NPU_RESULTS 32
static unsigned npu_port;
static uint64_t npu_generation;
struct npu_row {unsigned status;uint64_t local_us,bus_us,at;}; /* 0 ok, 1 timeout, 2 cooldown */
struct npu_result {uint64_t at,first,last;uint32_t id;double features[16];struct npu_row rows[5][2];char reply[2048];};
/* Every access to npu_state and npu_generation holds state.mutex. UDP and model
 * checks run outside the mutex. The original bus worker owns all bus requests. */
static struct {char status[64];uint64_t updated,total,rejected;unsigned fill,next,count;struct npu_result results[NPU_RESULTS];} npu_state;
static uint64_t npu_now_us(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))abort();return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static int npu_row_from(const struct snapshot *s,struct npu_row *r){
 r->at=s->sampled;r->local_us=s->local_us;r->bus_us=s->rtt;
 if(!s->sampled||!s->local_us||s->local_us>5000000)return -1;
 if(s->valid){r->status=0;if(!s->rtt||s->rtt>5000000)return -1;}
 else if(!strcmp(s->error,"gateway_timeout"))r->status=1;
 else if(!strcmp(s->error,"gateway_cooldown"))r->status=2;
 else return -1; /* Network errors, extra senders, bad CRC, etc. are unknown. */
 return 0;
}
static double npu_mean(const double a[5],unsigned n){double x=0;for(unsigned i=0;i<n;i++)x+=a[i];return n?x/n:0;}
static double npu_p95(double a[5],unsigned n){
 if(!n)return 0;
 for(unsigned i=1;i<n;i++){double v=a[i];unsigned j=i;while(j&&a[j-1]>v){a[j]=a[j-1];j--;}a[j]=v;}
 double p=(n-1)*0.95;unsigned lo=(unsigned)p;return lo+1<n?a[lo]+(a[lo+1]-a[lo])*(p-lo):a[lo];
}
static void npu_features(struct npu_row rows[5][2],double out[16]){
 for(unsigned b=0;b<2;b++){
  double success[5],bus[5],failed[5];unsigned ok=0,fail=0,counts[3]={0};
  for(unsigned i=0;i<5;i++){const struct npu_row *r=&rows[i][b];counts[r->status]++;if(r->status==0){success[ok]=r->local_us/1000.0;bus[ok++]=r->bus_us/1000.0;}else failed[fail++]=r->local_us/1000.0;}
  double *f=out+b*8;for(unsigned i=0;i<3;i++)f[i]=counts[i]/5.0;
  f[3]=npu_mean(success,ok);f[4]=npu_p95(success,ok);f[5]=npu_mean(bus,ok);f[6]=npu_p95(bus,ok);f[7]=npu_mean(failed,fail);
 }
}
static void npu_status(const char *s,unsigned fill,bool reject){pthread_mutex_lock(&state.mutex);snprintf(npu_state.status,sizeof npu_state.status,"%.63s",s);npu_state.fill=fill;npu_state.updated=now_ms();if(reject)npu_state.rejected++;pthread_mutex_unlock(&state.mutex);}
static int npu_datagram(int fd,const char *request,char reply[2048]){
 if(send(fd,request,strlen(request),0)!=(ssize_t)strlen(request))return -1;
 uint64_t until=now_ms()+1200;
 while(!atomic_load(&stopping)&&now_ms()<until){
  struct pollfd p={fd,POLLIN,0};int n=poll(&p,1,100);if(n<0&&errno==EINTR)continue;if(n<0)return -1;if(!n)continue;
  ssize_t size=recv(fd,reply,2047,MSG_TRUNC);if(size<1||size>=2047||memchr(reply,0,(size_t)size))return -1;reply[size]=0;return 0;
 }
 return -1;
}
static int npu_identity(const struct jd *d,uint32_t id,const char *server){
 uint64_t v;
 if(jfield(d,0,"v",&v)||v!=1||!jeq(d,jget(d,0,"type"),"npu_reply")||!jeq(d,jget(d,0,"session"),session)||
    jfield(d,0,"id",&v)||v!=id||!jeq(d,jget(d,0,"model_id"),NPU_MODEL_ID)||!jeq(d,jget(d,0,"backend"),"Alnpu"))return -1;
 if(server&&!jeq(d,jget(d,0,"server_id"),server))return -1;
 return 0;
}
static int npu_array(const struct jd *d,const char *key,unsigned *out,unsigned n){
 int a=jget(d,0,key);if(a<0||d->t[a].type!='[')return -1;
 unsigned i=(unsigned)a+1;for(unsigned k=0;k<n;k++){uint64_t v;if(i>=d->t[a].next||jnum(d,(int)i,&v)||v>255)return -1;out[k]=(unsigned)v;i=d->t[i].next;}
 return i==d->t[a].next?0:-1;
}
static int npu_check_reply(const struct jd *d,const double f[16]){
 unsigned x[16],cpu[3],clip,wire_x[16],wire_cpu[3],y[3],pred=0,cpu_pred=0,bad=0,maxdiff=0,endpoints=0;uint64_t v;
 nm_reference(f,x,cpu,&clip);
 if(npu_array(d,"input_u8",wire_x,16)||memcmp(x,wire_x,sizeof x)||npu_array(d,"cpu_logits",wire_cpu,3)||memcmp(cpu,wire_cpu,sizeof cpu)||npu_array(d,"logits",y,3))return -1;
 for(unsigned i=0;i<3;i++){unsigned diff=y[i]>cpu[i]?y[i]-cpu[i]:cpu[i]-y[i];if(diff>nm_budget[i])bad++;if(diff>maxdiff)maxdiff=diff;if(y[i]>y[pred])pred=i;if(cpu[i]>cpu[cpu_pred])cpu_pred=i;if(y[i]==0||y[i]==255)endpoints++;}
 unsigned second=0;for(unsigned i=0;i<3;i++)if(i!=pred&&y[i]>second)second=y[i];
 #define NPU_EQ(key,expected) (jfield(d,0,key,&v)==0&&v==(uint64_t)(expected))
 if(!NPU_EQ("class",pred)||!NPU_EQ("cpu_class",cpu_pred)||!NPU_EQ("clipped_dims",clip)||!NPU_EQ("max_diff",maxdiff)||!NPU_EQ("outside_budget",bad)||!NPU_EQ("logit_endpoints",endpoints)||!NPU_EQ("margin_lsb",y[pred]-second)||
    !jeq(d,jget(d,0,"label"),nm_labels[pred])||jfield(d,0,"enqueue_us",&v)||v>10000000||!v||jfield(d,0,"inference_count",&v)||!v)return -1;
 bool pass=bad==0&&pred==cpu_pred;
 if(!jeq(d,jget(d,0,"status"),pass?"ok":"review"))return -1;
 #undef NPU_EQ
 return pass?0:1;
}
/* A transition is defined from observed request availability, independently
 * of the model label. Timeout/cooldown both mean unavailable. The raw model
 * reply remains unmodified and is still archived for evaluation. */
static bool npu_window_transition(const struct npu_row rows[5][2]){
 for(unsigned b=0;b<2;b++)for(unsigned k=1;k<5;k++)
  if((rows[k][b].status==0)!=(rows[0][b].status==0))return true;
 return false;
}
static void npu_json_locked(FILE *f){
 fprintf(f,",\"software_revision\":\"R16_CAN_UPLINK\",\"npu\":{\"enabled\":%s,\"feature_contract\":\"%s\",\"classification_validation\":\"PENDING_BOARD_DOMAIN_TEST\",\"presentation_policy\":\"transition_abstention_v1\",\"model_id\":\"%s\",\"status\":\"%s\",\"window_filled\":%u,\"window_rounds\":5,\"total\":%"PRIu64",\"rejected\":%"PRIu64",\"age_ms\":",npu_port?"true":"false",NPU_CONTRACT,NPU_MODEL_ID,npu_state.status[0]?npu_state.status:npu_port?"waiting_for_samples":"disabled",npu_state.fill,npu_state.total,npu_state.rejected);
 if(npu_state.updated)fprintf(f,"%"PRIu64,now_ms()-npu_state.updated);else fputs("null",f);
 fputs(",\"history\":[",f);
 for(unsigned k=0;k<npu_state.count;k++){
  unsigned i=(npu_state.next+NPU_RESULTS-npu_state.count+k)%NPU_RESULTS;const struct npu_result *r=&npu_state.results[i];
  fprintf(f,"%s{\"window_phase\":\"%s\",\"id\":%u,\"at_ms\":%"PRIu64",\"first_generation\":%"PRIu64",\"last_generation\":%"PRIu64",\"features\":[",k?",":"",npu_window_transition(r->rows)?"transition":"stable",r->id,r->at-started,r->first,r->last);
  for(unsigned n=0;n<16;n++)fprintf(f,"%s%.17g",n?",":"",r->features[n]);
  fputs("],\"samples\":[",f);
  for(unsigned j=0;j<5;j++){if(j)fputc(',',f);fputc('[',f);for(unsigned b=0;b<2;b++)fprintf(f,"%s{\"status\":\"%s\",\"local_us\":%"PRIu64",\"bus_us\":%"PRIu64",\"at_ms\":%"PRIu64"}",b?",":"",r->rows[j][b].status==0?"ok":r->rows[j][b].status==1?"timeout":"cooldown",r->rows[j][b].local_us,r->rows[j][b].bus_us,r->rows[j][b].at-started);fputc(']',f);}
  fprintf(f,"],\"reply\":%s}",r->reply);
 }
 fputs("],\"hardware_irq\":{",f);
 FILE *irq=fopen("/proc/interrupts","r");uint64_t hard=0,soft=0;bool have_hard=false,have_soft=false;
 if(irq){char line[1024];while(fgets(line,sizeof line,irq)){
  bool is_hard=strstr(line,"HARD_NPU_IRQ")!=NULL,is_soft=strstr(line,"SOFT_NPU_IRQ")!=NULL;
  if(!is_hard&&!is_soft)continue;
  char *p=strchr(line,':');if(!p)continue;p++;uint64_t sum=0;bool found=false;
  while(*p){while(*p==' '||*p=='\t')p++;if(*p<'0'||*p>'9')break;char *end;unsigned long long v=strtoull(p,&end,10);if(end==p)break;sum+=v;p=end;found=true;}
  if(is_hard){hard=sum;have_hard=found;}else {soft=sum;have_soft=found;}
 }fclose(irq);}
 fprintf(f,"\"available\":%s,\"HARD\":%"PRIu64",\"SOFT\":%"PRIu64"}}",have_hard&&have_soft?"true":"false",hard,soft);
}
static bool npu_strhex(const char *s){for(;*s;s++)if(!(*s>='0'&&*s<='9')&&!(*s>='a'&&*s<='f'))return false;return true;}
static bool npu_true(const struct jd *d,const char *key){int i=jget(d,0,key);return i>=0&&d->t[i].type=='b'&&d->t[i].end-d->t[i].start==4&&!memcmp(d->s+d->t[i].start,"true",4);}
static void *npu_worker(void *unused){
 (void)unused;int fd=socket(AF_INET,SOCK_DGRAM,0);struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons((uint16_t)npu_port);
 if(fd<0||connect(fd,(struct sockaddr *)&addr,sizeof addr)){if(fd>=0)close(fd);npu_status("npu_socket_unavailable",0,true);return NULL;}
 uint64_t generation=0,first=0,last_count=0;unsigned fill=0;uint32_t id=0;char server[17]="";struct npu_row rows[5][2];
 while(!atomic_load(&stopping)){
  struct snapshot snap[2];uint64_t next;
  pthread_mutex_lock(&state.mutex);next=npu_generation;snap[0]=state.snap[0];snap[1]=state.snap[1];pthread_mutex_unlock(&state.mutex);
  if(next==generation){struct timespec pause={0,50000000};nanosleep(&pause,NULL);continue;}
  bool gap=next!=generation+1;generation=next;if(gap)fill=0;
  if(npu_row_from(&snap[0],&rows[fill][0])||npu_row_from(&snap[1],&rows[fill][1])){fill=0;npu_status("unknown_sample_window_reset",fill,true);continue;}
  if(fill && (rows[fill][0].at<rows[fill-1][0].at || rows[fill][0].at-rows[fill-1][0].at>2*period_ms+2000)){
   rows[0][0]=rows[fill][0];rows[0][1]=rows[fill][1];fill=0;npu_status("long_sample_gap_window_reset",0,true);
  }
  if(!fill)first=next;
  fill++;npu_status(gap?"sampling_gap_window_reset":"collecting",fill,false);if(fill<5)continue;
  struct npu_result result={0};result.id=++id;result.first=first;result.last=next;memcpy(result.rows,rows,sizeof rows);npu_features(rows,result.features);fill=0;
  char request[1024],reply[2048];struct jd doc;
  if(!server[0]){
   snprintf(request,sizeof request,"IGWNPU1 HELLO %s",session);
   uint64_t layers;
   if(npu_datagram(fd,request,reply)||jparse(&doc,reply,strlen(reply))||npu_identity(&doc,0,NULL)||!jeq(&doc,jget(&doc,0,"status"),"ready")||
      jstr(&doc,jget(&doc,0,"server_id"),server,sizeof server)||strlen(server)!=16||!npu_strhex(server)||
      jfield(&doc,0,"hardware_layers",&layers)||layers!=2||!npu_true(&doc,"self_test")){
    server[0]=0;npu_status("npu_hello_failed",0,true);continue;
   }
   last_count=0;
  }
  int used=snprintf(request,sizeof request,"IGWNPU1 INFER %s %u %s",session,id,server);
  for(unsigned i=0;i<16;i++){int n=snprintf(request+used,sizeof request-(size_t)used," %.17g",result.features[i]);if(n<0||(size_t)n>=sizeof request-(size_t)used){used=-1;break;}used+=n;}
  if(used<0||npu_datagram(fd,request,result.reply)||jparse(&doc,result.reply,strlen(result.reply))||npu_identity(&doc,id,server)){
   server[0]=0;npu_status("npu_reply_unavailable",0,true);continue;
  }
  int checked=npu_check_reply(&doc,result.features);uint64_t count=0;
  if(checked<0||jfield(&doc,0,"inference_count",&count)||count<=last_count){server[0]=0;npu_status("npu_reply_validation_failed",0,true);continue;}
  last_count=count;result.at=now_ms();
  pthread_mutex_lock(&state.mutex);npu_state.results[npu_state.next]=result;npu_state.next=(npu_state.next+1)%NPU_RESULTS;if(npu_state.count<NPU_RESULTS)npu_state.count++;npu_state.total++;npu_state.updated=result.at;npu_state.fill=0;snprintf(npu_state.status,sizeof npu_state.status,"%s",checked?"numeric_review":"numeric_pass_accuracy_pending");pthread_mutex_unlock(&state.mutex);
 }
 close(fd);return NULL;
}
#endif
