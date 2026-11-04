/* Actual production exchange() parser, real datagram transport; fixed replies
 * exercise accounting boundaries independently of RS receiver implementation. */
#define _GNU_SOURCE
#include <assert.h>
#define main igw_web_production_main
#include "../src/igw_web.c"
#undef main
struct fixture {uint64_t good,tx,up,bad,slot,submit,crc;const char *owner;bool scope,malformed;};
static int peer;
static struct fixture normal(uint64_t good,uint64_t tx,uint64_t up){return (struct fixture){.good=good,.tx=tx,.up=up,.owner="aaaaaaaaaaaaaaaa",.scope=true};}
static void init(void){int fd[2];assert(socketpair(AF_UNIX,SOCK_DGRAM,0,fd)==0);memset(&buses[1],0,sizeof buses[1]);memset(&rs_previous,0,sizeof rs_previous);buses[1].fd=fd[0];buses[1].which=1;buses[1].unit=1;peer=fd[1];}
static void finish(void){close(peer);close(buses[1].fd);}
static int run(struct fixture f){
 struct bus *b=&buses[1];char raw[3000],account[700]="";
 if(f.scope)snprintf(account,sizeof account,",\"pl_accounting\":{\"schema\":\"IGW_RS485_PL_ACCOUNTING_R17_1\",\"mode\":\"ordinary_plus_validated_uplink\",\"owner_session\":\"%s\",\"validated_uplink_good\":%"PRIu64",\"bad_uplink_frames\":%"PRIu64",\"slot_timeouts\":%"PRIu64"%s}",f.owner,f.up,f.bad,f.slot,f.malformed?"":",\"control_submit_fail\":0");
 snprintf(raw,sizeof raw,"{\"v\":2,\"type\":\"rs485_reply\",\"status\":\"ok\",\"seq\":%u,\"op\":\"READ\",\"unit\":1,\"address\":16,\"count\":1,\"value\":0,\"execution\":\"read\",\"data\":\"0103021234B533\",\"registers\":[4660],\"rs485_rtt_us\":33000,\"stats\":{\"attempts\":%u,\"ok\":%u,\"timeout\":0,\"tx_fail\":0,\"bad_reply\":0,\"exception\":0,\"busy\":0,\"duplicate\":0,\"ignored\":0,\"udp_fail\":0},\"pl\":{\"good\":%"PRIu64",\"tx\":%"PRIu64",\"crc\":%"PRIu64",\"uart\":0,\"gap\":0,\"overrun\":0,\"watchdog\":0}%s}",b->seq+1,b->seq+1,b->seq+1,f.good,f.tx,f.crc,account);
 assert(send(peer,raw,strlen(raw),0)==(ssize_t)strlen(raw));uint16_t value=0;int result=exchange(b,false,16,1,&value,NULL);
 char request[128];assert(recv(peer,request,sizeof request,0)>0);if(!result)assert(value==4660);return result;
}
static void rejects(struct fixture next,const char *error){init();assert(run(normal(100,20,10))==0);assert(run(next)<0);assert(!strcmp(buses[1].error,error));finish();}
int main(void){
 init();assert(run(normal(100,20,10))==0);assert(run(normal(107,21,16))==0);finish();
 rejects(normal(108,21,16),"pl_counter_reset_or_extra_frame"); /* unaccounted GOOD */
 rejects(normal(107,22,16),"pl_counter_reset_or_extra_frame"); /* extra TX */
 rejects(normal(101,21,9),"pl_uplink_accounting_reset");
 struct fixture n=normal(101,21,10);n.owner="bbbbbbbbbbbbbbbb";rejects(n,"pl_accounting_owner_changed");
 n=normal(101,21,10);n.malformed=true;rejects(n,"invalid_pl_accounting");
 n=normal(101,21,10);n.scope=false;rejects(n,"pl_accounting_owner_changed");
 n=normal(101,21,10);n.crc=1;rejects(n,"new_pl_error");
 n=normal(101,21,10);n.bad=1;rejects(n,"new_uplink_error");
 n=normal(101,21,10);n.slot=1;rejects(n,"new_uplink_error");
 rejects(normal((uint64_t)UINT32_MAX+1,21,10),"invalid_pl_counters");
 init();assert(run(normal(UINT32_MAX-1,UINT32_MAX,10))==0);assert(run(normal(0,0,11))==0);finish();
 init();n=normal(100,20,0);n.scope=false;assert(run(n)==0);n.good++;n.tx++;assert(run(n)==0);finish();
 puts("PASS production exchange accounting: exact legitimate increment, unknown GOOD/TX rejected, owner/schema/reset/error guards, 32-bit wrap, strict legacy mode");return 0;
}
