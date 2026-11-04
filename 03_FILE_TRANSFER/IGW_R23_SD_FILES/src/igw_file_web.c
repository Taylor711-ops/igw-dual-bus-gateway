#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "igwf_sha256.h"
#include "file_page.h"
#define LIMIT 8388608u
#define JOBS 16u
struct job {char id[33],input[512],output[512],log[512],sha[65],operation[10];char bus[8],phase[16];unsigned unit,size,progress,total;int ok,used,done;pid_t worker;};
static struct job jobs[JOBS];static unsigned job_count;
static char runtime[400],backend[512],rs_backend[512],control_token[33];
static const char *authority="192.168.31.222:8081";
static volatile sig_atomic_t stopping;
static int blocked;
static int inherited_listener=-1,inherited_connection=-1;
static pid_t active_worker;
static void stop(int sig){(void)sig;stopping=1;}
static uint64_t clock_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int write_all(int fd,const void *data,size_t n){const uint8_t *p=data;while(n){ssize_t k=write(fd,p,n);if(k<0&&errno==EINTR&&!stopping)continue;if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;}
static int random_hex(char out[33]){uint8_t b[16];int f=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(f<0)return -1;ssize_t n=read(f,b,16);close(f);if(n!=16)return -1;for(unsigned i=0;i<16;i++)snprintf(out+i*2,3,"%02x",b[i]);return 0;}
static void hex_digest(const uint8_t *p,size_t n,char out[65]){uint8_t d[32];igwf_digest(p,n,d);for(unsigned i=0;i<32;i++)snprintf(out+i*2,3,"%02x",d[i]);}
static int response(int fd,unsigned code,const char *type,const void *body,size_t size,int download){
 char h[700];const char *reason=code==202?"Accepted":code==200?"OK":code==400?"Bad Request":code==403?"Forbidden":code==404?"Not Found":code==409?"Conflict":code==413?"Payload Too Large":code==507?"Insufficient Storage":code==503?"Service Unavailable":"Internal Server Error";
 int n=snprintf(h,sizeof h,"HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'\r\n%s\r\n",code,reason,type,size,download?"Content-Disposition: attachment; filename=igw-device-file.bin\r\n":"");
 if(n<0||(size_t)n>=sizeof h||write_all(fd,h,(size_t)n))return -1;
 return write_all(fd,body,size);
}
static int error_reply(int fd,unsigned code,const char *why){char b[180];int n=snprintf(b,sizeof b,"{\"ok\":false,\"error\":\"%s\"}",why);return response(fd,code,"application/json",b,(size_t)n,0);}
static int read_file(const char *path,uint8_t out[LIMIT],unsigned *n){int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return -1;struct stat st;if(fstat(f,&st)||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>LIMIT){close(f);return -1;}*n=(unsigned)st.st_size;unsigned done=0;while(done<*n){ssize_t k=read(f,out+done,*n-done);if(k<0&&errno==EINTR)continue;if(k<=0){close(f);return -1;}done+=(unsigned)k;}close(f);return 0;}
static int save_input(const char *path,const uint8_t *body,unsigned n){int f=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return -1;int ret=write_all(f,body,n);if(!ret)ret=fsync(f);if(close(f))ret=-1;return ret;}
static int run_backend(struct job *j,const char *op,const char *size_or_path,const char *extra){
 char unit[8];snprintf(unit,sizeof unit,"%u",j->unit);
 int is_rs=!strcmp(j->bus,"rs485");const char *exe=is_rs?rs_backend:backend;
 char *can_args[]={(char *)exe,"--can","can0","--unit",unit,(char *)op,(char *)size_or_path,(char *)extra,NULL};
 char *rs_args[]={(char *)exe,"--unit",unit,"--baud","115200",(char *)op,(char *)size_or_path,(char *)extra,NULL};
 char **args=is_rs?rs_args:can_args;if(!extra)args[7]=NULL;
 int log=open(j->log,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);if(log<0)return -1;
 pid_t pid=fork();if(pid<0){close(log);return -1;}
 if(!pid){if(dup2(log,STDOUT_FILENO)<0||dup2(log,STDERR_FILENO)<0)_exit(126);close(log);execv(exe,args);_exit(127);}
 close(log);uint64_t end=clock_ms()+3700000;int status=0,terminated=0;
 for(;;){pid_t p=waitpid(pid,&status,WNOHANG);if(p==pid)break;if(p<0&&errno!=EINTR){blocked=1;return -1;}
  if(stopping&&!terminated){kill(pid,SIGTERM);terminated=2;end=clock_ms()+20000;}
  else if(clock_ms()>end&&terminated<2){kill(pid,SIGTERM);terminated=2;end=clock_ms()+20000;}
  else if(terminated==2&&clock_ms()>end){kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}blocked=1;return -1;}
  struct timespec pause={0,20000000};nanosleep(&pause,NULL);
 }
 if(!WIFEXITED(status)||WEXITSTATUS(status)||terminated){blocked=1;return -1;}return 0;
}
/* Require the expected command result from a successfully exited backend.
 * Output bytes are independently hashed below rather than trusting a PASS line. */
static int backend_result(struct job *j,unsigned *bytes,char hash[65],unsigned *count){
 FILE *f=fopen(j->log,"r");if(!f)return -1;char line[512];*count=0;
 while(fgets(line,sizeof line,f)){
  unsigned unit=0,n=0;unsigned long long duration=0;char h[65]={0};int fields=0;
  if(!strcmp(j->bus,"can")){
   unsigned packets=0,tx=0,lr=0,pr=0,ignored=0;
   fields=sscanf(line,"RESULT=PASS_CAN_BLOCK_FILE unit=%u bytes=%u duration_ms=%llu packets=%u wire_tx=%u link_retries=%u packet_retries=%u ignored=%u sha256=%64s",&unit,&n,&duration,&packets,&tx,&lr,&pr,&ignored,h)==9;
  }else{
   unsigned baud=0,requests=0,retries=0,ignored=0;
   fields=sscanf(line,"RESULT=PASS_RS485_FILE unit=%u bytes=%u baud=%u duration_ms=%llu requests=%u retries=%u ignored=%u sha256=%64s",&unit,&n,&baud,&duration,&requests,&retries,&ignored,h)==8&&baud==115200;
  }
  if(fields){
   if(unit!=j->unit||n>LIMIT||strlen(h)!=64){fclose(f);return -1;}
   for(unsigned i=0;i<64;i++)if(!((h[i]>='0'&&h[i]<='9')||(h[i]>='a'&&h[i]<='f'))){fclose(f);return -1;}
   (*count)++;*bytes=n;memcpy(hash,h,65);
  }
 }
 fclose(f);return *count?0:-1;
}
struct request {char method[8],target[256],host[100],origin[120],token[50];size_t content_length;int length_seen,host_seen,origin_seen,token_seen;};
static int copy_field(char *out,size_t cap,const char *value){size_t n=strlen(value);while(n&&(value[n-1]==' '||value[n-1]=='\t'))n--;if(n>=cap)return -1;memcpy(out,value,n);out[n]=0;return 0;}
static int parse_request(int in,struct request *r,uint8_t body[LIMIT]){
 uint64_t deadline=clock_ms()+15000;char head[8192];size_t n=0;memset(r,0,sizeof *r);
 while(n+1<sizeof head){if(stopping||clock_ms()>deadline)return -1;ssize_t k=read(in,head+n,1);if(k<0&&errno==EINTR&&!stopping)continue;if(k!=1)return -1;if(!head[n])return -1;n++;if(n>=4&&!memcmp(head+n-4,"\r\n\r\n",4))break;}
 if(n<4||memcmp(head+n-4,"\r\n\r\n",4))return -1;
 head[n]=0;
 char *line_end=strstr(head,"\r\n");if(!line_end)return -1;*line_end=0;char version[12],tail;
 if(sscanf(head,"%7s %255s %11s %c",r->method,r->target,version,&tail)!=3||strcmp(version,"HTTP/1.1"))return -1;
 for(char *p=line_end+2;*p;){char *e=strstr(p,"\r\n");if(!e)return -1;if(e==p)break;*e=0;char *colon=strchr(p,':');if(!colon||p[0]==' '||p[0]=='\t')return -1;*colon=0;char *v=colon+1;while(*v==' '||*v=='\t')v++;
  if(!strcasecmp(p,"Host")){if(r->host_seen++||copy_field(r->host,sizeof r->host,v))return -1;}
  else if(!strcasecmp(p,"Origin")){if(r->origin_seen++||copy_field(r->origin,sizeof r->origin,v))return -1;}
  else if(!strcasecmp(p,"X-IGW-Token")){if(r->token_seen++||copy_field(r->token,sizeof r->token,v))return -1;}
  else if(!strcasecmp(p,"Content-Length")){if(r->length_seen||!*v)return -1;r->length_seen=1;size_t length=0;for(;*v;v++){if(*v<'0'||*v>'9'||length>LIMIT)return -2;length=length*10+(unsigned)(*v-'0');}if(length>LIMIT)return -2;r->content_length=length;}
  else if(!strcasecmp(p,"Transfer-Encoding")||!strcasecmp(p,"Expect"))return -1;
  p=e+2;
 }
 if(strcmp(r->host,authority))return -3;
 if(strcmp(r->method,"GET")&&strcmp(r->method,"POST"))return -1;
 if(!strcmp(r->method,"GET")&&r->content_length)return -1;
 size_t done=0;while(done<r->content_length){if(stopping||clock_ms()>deadline)return -1;ssize_t k=read(in,body+done,r->content_length-done);if(k<0&&errno==EINTR&&!stopping)continue;if(k<=0)return -1;done+=(size_t)k;}
 return 0;
}
static int authorized(const struct request *r){char expected[150];snprintf(expected,sizeof expected,"http://%s",authority);return !strcmp(r->token,control_token)&&(!r->origin[0]||!strcmp(r->origin,expected));}
static int hex_id(const char *id){if(strlen(id)!=32)return 0;for(unsigned i=0;i<32;i++)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return 0;return 1;}
static int operation(const char *target,char op[10],unsigned *unit,unsigned *size,char bus[8]){
 const char *p=NULL;
 if(!strncmp(target,"/api/file?op=put&unit=",22)){strcpy(op,"put");p=target+22;}
 else if(!strncmp(target,"/api/file?op=get&unit=",22)){strcpy(op,"get");p=target+22;}
 else if(!strncmp(target,"/api/file?op=generate&unit=",27)){strcpy(op,"generate");p=target+27;}
 else return -1;
 if((*p!='1'&&*p!='2')||strncmp(p+1,"&size=",6))return -1;
 *unit=(unsigned)(*p-'0');p+=7;if(*p<'0'||*p>'9')return -1;unsigned n=0;
 for(;*p>='0'&&*p<='9';p++){if(n>LIMIT/10)return -1;n=n*10+(unsigned)(*p-'0');if(n>LIMIT)return -1;}
 if(!strcmp(p,"&bus=can"))strcpy(bus,"can");else if(!strcmp(p,"&bus=rs485"))strcpy(bus,"rs485");else return -1;
 *size=n;return 0;
}
/* The HTTP parent never handles a bus. Exactly one owned worker invokes
 * an unchanged, validated CLI. Successful publication requires worker exit 0
 * and an atomically written result after independent output verification. */
static void progress_job(struct job *j){
 if(j->done)return;
 FILE *f=fopen(j->log,"r");if(!f)return;char line[512];
 while(fgets(line,sizeof line,f)){
  char phase[16];unsigned n=0,total=0;
  if(sscanf(line,"WEB_STAGE=%15s",phase)==1){strcpy(j->phase,phase);j->progress=0;j->total=j->size;}
  if(sscanf(line,"PROGRESS bytes=%u/%u",&n,&total)==2&&n<=total&&total<=LIMIT){j->progress=n;j->total=total;}
 }
 fclose(f);
}
static int valid_hash(const char *h){if(strlen(h)!=64)return 0;for(unsigned i=0;i<64;i++)if(!((h[i]>='0'&&h[i]<='9')||(h[i]>='a'&&h[i]<='f')))return 0;return 1;}
static void harvest(void){
 for(unsigned i=0;i<job_count;i++){
  struct job *j=jobs+i;if(!j->worker||j->done)continue;progress_job(j);int status=0;pid_t p=waitpid(j->worker,&status,WNOHANG);if(!p)continue;if(p<0&&errno==EINTR)continue;
  if(active_worker==j->worker){active_worker=0;}j->done=1;
  char path[560],h[65]={0},extra;unsigned size=0;snprintf(path,sizeof path,"%s/%s.result",runtime,j->id);FILE *f=fopen(path,"r");int fields=f?fscanf(f,"%u %64s %c",&size,h,&extra):0;if(f)fclose(f);
  if(p==j->worker&&WIFEXITED(status)&&!WEXITSTATUS(status)&&fields==2&&size<=LIMIT&&valid_hash(h)){
   j->ok=1;j->size=size;j->progress=size;j->total=size;strcpy(j->sha,h);strcpy(j->phase,"complete");
   printf("FILE_JOB id=%s bus=%s unit=%u op=%s bytes=%u result=PASS sha256=%s\n",j->id,j->bus,j->unit,j->operation,j->size,j->sha);
  }else{blocked=1;strcpy(j->phase,"failed");printf("FILE_JOB id=%s bus=%s unit=%u op=%s result=FAIL\n",j->id,j->bus,j->unit,j->operation);}
 }
}
static int stage(struct job *j,const char *name){int f=open(j->log,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return -1;char b[64];int n=snprintf(b,sizeof b,"WEB_STAGE=%s\n",name);int ret=write_all(f,b,(size_t)n);close(f);return ret;}
static int execute_job(struct job *j){
 int result=0;const char *op=j->operation;unsigned size=j->size;static uint8_t data[LIMIT];
 if(!strcmp(op,"put")){
  result=stage(j,"put");if(!result)result=run_backend(j,"put",j->input,NULL);
  if(!result){result=stage(j,"readback");}if(!result)result=run_backend(j,"get",j->output,NULL);
 }else if(!strcmp(op,"get")){result=stage(j,"get");if(!result)result=run_backend(j,"get",j->output,NULL);}
 else{char length[16];snprintf(length,sizeof length,"%u",size);result=stage(j,"generate");if(!result)result=run_backend(j,"generate",length,j->output);}
 if(!result&&!stopping){unsigned bytes=0,count=0,n=0;char wire_sha[65]={0},output_sha[65];
  if(stage(j,"verify")||backend_result(j,&bytes,wire_sha,&count)||count!=(!strcmp(op,"put")?2u:1u)||read_file(j->output,data,&n))return -1;
  hex_digest(data,n,output_sha);
  if(bytes!=n||strcmp(wire_sha,output_sha)||(!strcmp(op,"put")&&(size!=n||strcmp(j->sha,output_sha)))||(!strcmp(op,"generate")&&size!=n))return -1;
  char path[560],tmp[570],line[100];snprintf(path,sizeof path,"%s/%s.result",runtime,j->id);snprintf(tmp,sizeof tmp,"%s.tmp",path);int length=snprintf(line,sizeof line,"%u %s\n",n,output_sha);
  if(save_input(tmp,(const uint8_t *)line,(unsigned)length)||rename(tmp,path)){return -1;}return 0;
 }
 return -1;
}
static void handle(int in,int out){
 harvest();struct request req;static uint8_t body[LIMIT];int parsed=parse_request(in,&req,body);
 if(parsed){error_reply(out,parsed==-2?413:parsed==-3?403:400,"invalid_request");return;}
 if(!strcmp(req.method,"GET")&&!strcmp(req.target,"/")){response(out,200,"text/html; charset=utf-8",FILE_PAGE,sizeof FILE_PAGE-1,0);return;}
 if(!strcmp(req.method,"GET")&&!strcmp(req.target,"/api/session")){char json[320];int n=snprintf(json,sizeof json,"{\"revision\":\"R23_DUAL_FILE_WEB\",\"token\":\"%s\",\"max_bytes\":%u,\"remaining_jobs\":%u,\"blocked\":%s,\"busy\":%s}",control_token,LIMIT,JOBS-job_count,blocked?"true":"false",active_worker?"true":"false");response(out,200,"application/json",json,(size_t)n,0);return;}
 if(!authorized(&req)){error_reply(out,403,"invalid_token_or_origin");return;}
 if(!strcmp(req.method,"GET")&&!strcmp(req.target,"/api/jobs")){
  char json[16384];size_t n=(size_t)snprintf(json,sizeof json,"{\"ok\":true,\"blocked\":%s,\"busy\":%s,\"jobs\":[",blocked?"true":"false",active_worker?"true":"false");
  for(unsigned i=0;i<job_count;i++){struct job *j=jobs+i;int k=snprintf(json+n,sizeof json-n,"%s{\"job_id\":\"%s\",\"bus\":\"%s\",\"unit\":%u,\"operation\":\"%s\",\"bytes\":%u,\"ok\":%s,\"done\":%s,\"phase\":\"%s\",\"progress_bytes\":%u,\"total_bytes\":%u,\"sha256\":\"%s\"}",i?",":"",j->id,j->bus,j->unit,j->operation,j->size,j->ok?"true":"false",j->done?"true":"false",j->phase,j->progress,j->total,j->ok?j->sha:"");if(k<0||(size_t)k>=sizeof json-n){error_reply(out,500,"response_limit");return;}n+=(size_t)k;}
  memcpy(json+n,"]}",2);n+=2;response(out,200,"application/json",json,n,0);return;
 }
 if(!strcmp(req.method,"GET")&&!strncmp(req.target,"/api/download?id=",17)){
  const char *id=req.target+17;if(!hex_id(id)){error_reply(out,400,"invalid_job_id");return;}
  for(unsigned i=0;i<job_count;i++)if(jobs[i].ok&&!strcmp(id,jobs[i].id)){unsigned n;if(read_file(jobs[i].output,body,&n)){error_reply(out,500,"file_read_failed");return;}response(out,200,"application/octet-stream",body,n,1);return;}
  error_reply(out,404,"verified_file_not_found");return;
 }
 if(!strcmp(req.method,"POST")&&!strcmp(req.target,"/api/cancel")){
  if(req.content_length||!req.length_seen){error_reply(out,400,"invalid_body_length");return;}
  if(active_worker){kill(active_worker,SIGTERM);blocked=1;}
  const char *json="{\"ok\":true}";response(out,200,"application/json",json,strlen(json),0);return;
 }
 if(strcmp(req.method,"POST")||strncmp(req.target,"/api/file?",10)){error_reply(out,404,"not_found");return;}
 char op[10],bus[8];unsigned unit=0,size=0;
 if(operation(req.target,op,&unit,&size,bus)||!req.length_seen){error_reply(out,400,"invalid_operation");return;}
 if((!strcmp(op,"put")&&size!=req.content_length)||(strcmp(op,"put")&&req.content_length)||(!strcmp(op,"get")&&size)){error_reply(out,400,"invalid_body_length");return;}
 if(blocked){error_reply(out,503,"previous_failure_requires_operator_review");return;}
 if(active_worker){error_reply(out,409,"another_transfer_is_running");return;}
 if(job_count>=JOBS){error_reply(out,507,"session_job_limit_save_evidence_and_restart");return;}
 struct job *j=jobs+job_count;memset(j,0,sizeof *j);j->unit=unit;j->size=size;strcpy(j->operation,op);strcpy(j->bus,bus);strcpy(j->phase,"starting");j->total=size;
 if(random_hex(j->id)){error_reply(out,500,"random_failed");return;}
 snprintf(j->input,sizeof j->input,"%s/%s.input",runtime,j->id);snprintf(j->output,sizeof j->output,"%s/%s.output",runtime,j->id);snprintf(j->log,sizeof j->log,"%s/%s.log",runtime,j->id);j->used=1;job_count++;
 if(!strcmp(op,"put")){hex_digest(body,size,j->sha);if(save_input(j->input,body,size)){j->done=1;blocked=1;error_reply(out,500,"input_save_failed");return;}}
 fflush(NULL);pid_t pid=fork();if(pid<0){j->done=1;blocked=1;error_reply(out,500,"worker_start_failed");return;}
 if(!pid){if(inherited_listener>=0)close(inherited_listener);if(inherited_connection>=0)close(inherited_connection);_exit(execute_job(j)?3:0);}
 j->worker=pid;active_worker=pid;
 char json[180];int n=snprintf(json,sizeof json,"{\"ok\":true,\"accepted\":true,\"job_id\":\"%s\"}",j->id);response(out,202,"application/json",json,(size_t)n,0);
}

static int initialize_runtime(void){
 strcpy(runtime,"/tmp/igw_r23_files.XXXXXX");
 return mkdtemp(runtime)&&!random_hex(control_token)?0:-1;
}
static void usage(void){puts("igw_file_web --can-backend /absolute/igw_can_block --rs-backend /absolute/igw_rs485_file\nR23 dedicated dual-bus file service: 192.168.31.222:8081, peer 192.168.31.100. Slave SD_FILE_R23. Stop existing bus owners first.");}
int main(int argc,char **argv){
 if(argc!=5||strcmp(argv[1],"--can-backend")||strcmp(argv[3],"--rs-backend")){usage();return 2;}
 if(geteuid()!=0){fprintf(stderr,"Run as root\n");return 2;}
 char *resolved=realpath(argv[2],NULL);if(!resolved||strlen(resolved)>=sizeof backend){free(resolved);perror("backend");return 2;}strcpy(backend,resolved);free(resolved);
 struct stat st;if(stat(backend,&st)||!S_ISREG(st.st_mode)||access(backend,X_OK)){perror("backend executable");return 2;}
 resolved=realpath(argv[4],NULL);if(!resolved||strlen(resolved)>=sizeof rs_backend){free(resolved);return 2;}strcpy(rs_backend,resolved);free(resolved);
 if(stat(rs_backend,&st)||!S_ISREG(st.st_mode)||access(rs_backend,X_OK)){perror("RS backend");return 2;}
 int lock=open("/tmp/igw_file_web_r19.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){fprintf(stderr,"File web already running\n");return 2;}
 if(initialize_runtime()){perror("runtime");return 2;}
 struct sigaction sa={0};sa.sa_handler=stop;sigemptyset(&sa.sa_mask);sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);signal(SIGPIPE,SIG_IGN);
 int listener=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);int yes=1;if(listener<0||setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes)){perror("socket");return 2;}
 struct sockaddr_in addr;memset(&addr,0,sizeof addr);addr.sin_family=AF_INET;addr.sin_port=htons(8081);inet_pton(AF_INET,"192.168.31.222",&addr.sin_addr);
 if(bind(listener,(struct sockaddr *)&addr,sizeof addr)||listen(listener,8)){perror("HTTP bind/listen");return 2;}
 setvbuf(stdout,NULL,_IOLBF,0);printf("R23 DUAL FILE WEB READY http://%s peer=192.168.31.100; CAN+RS485; one operation at a time; max=8388608 bytes; slave=SD_FILE_R23\nEVIDENCE_DIRECTORY=%s\n",authority,runtime);
 struct in_addr allowed;inet_pton(AF_INET,"192.168.31.100",&allowed);
 inherited_listener=listener;
 while(!stopping){harvest();struct pollfd p={listener,POLLIN,0};int n=poll(&p,1,500);if(n<0&&errno==EINTR)continue;if(n<0)break;if(!n)continue;
  struct sockaddr_in peer;socklen_t length=sizeof peer;int fd=accept4(listener,(struct sockaddr *)&peer,&length,SOCK_CLOEXEC);if(fd<0)continue;
  struct timeval t={5,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t);t.tv_sec=10;setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t);
  inherited_connection=fd;if(peer.sin_addr.s_addr==allowed.s_addr)handle(fd,fd);else error_reply(fd,403,"peer_not_allowed");close(fd);inherited_connection=-1;
 }
 close(listener);if(active_worker){kill(active_worker,SIGTERM);while(active_worker){harvest();struct timespec pause={0,20000000};nanosleep(&pause,NULL);}}close(lock);printf("R23_STOP jobs=%u blocked=%u; evidence retained at %s\n",job_count,(unsigned)blocked,runtime);return blocked?3:0;
}
