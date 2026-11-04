/* Host harness injects snapshots only; uses production worker and validators.
 * No FPGA, bus hardware, or ArmNN inference is emulated as a hardware pass. */
#define main web_main_for_test
#include "../src/igw_web.c"
#undef main
int main(int argc,char **argv){
 if(argc!=2)return 2;
 npu_port=(unsigned)atoi(argv[1]);started=now_ms();strcpy(session,"0123456789abcdef0123456789abcdef");
 setvbuf(stdout,NULL,_IOLBF,0);pthread_t thread;if(pthread_create(&thread,NULL,npu_worker,NULL))return 2;
 char line[500];while(fgets(line,sizeof line,stdin)){
  if(!strncmp(line,"sample ",7)){
   unsigned a,b;unsigned long long al,ab,bl,bb;
   if(sscanf(line+7,"%u %llu %llu %u %llu %llu",&a,&al,&ab,&b,&bl,&bb)!=6)return 3;
   struct snapshot s[2]={0};unsigned statuses[2]={a,b};
   for(unsigned i=0;i<2;i++){s[i].valid=statuses[i]==0;s[i].sampled=now_ms();s[i].local_us=i?bl:al;s[i].rtt=i?bb:ab;if(!s[i].valid)snprintf(s[i].error,sizeof s[i].error,"%s",statuses[i]==1?"gateway_timeout":statuses[i]==2?"gateway_cooldown":"udp_timeout_unknown");}
   pthread_mutex_lock(&state.mutex);state.snap[0]=s[0];state.snap[1]=s[1];npu_generation++;pthread_mutex_unlock(&state.mutex);
   struct timespec t={0,80000000};nanosleep(&t,NULL);puts("sampled");
  }else if(!strncmp(line,"state",5)){size_t n;char *s=state_json(&n,true);if(!s)return 4;puts(s);free(s);}
  else if(!strncmp(line,"stop",4))break;else return 5;
 }
 atomic_store(&stopping,true);pthread_join(thread,NULL);return 0;
}
