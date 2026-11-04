#include <assert.h>
#include <stdlib.h>
#include "al_can_hal.h"
#define IGW_HOST_TEST 1
#define main firmware_main
#ifdef TEST_ROLE_MASTER
#include "../slave/main.c"
#else
#include "../slave/main.c"
#endif
#undef main
#include "can_mock.inc"
static uint32_t Registers[0x300/4];
static AL_U64 TxFinish,ResponseAt;
static unsigned RtuFault,ReplyCount,FeedCount;
static void Queue(const uint8_t *p,size_t n){
 memset(&Registers[IGW_RX_DATA/4],0,256);
 for(unsigned i=0;i<n;++i)Registers[(IGW_RX_DATA+i)/4]|=(uint32_t)p[i]<<(8U*(i%4));
 Registers[IGW_RX_LEN/4]=(uint32_t)n;Registers[IGW_RX_TICK_LO/4]=(uint32_t)Tick;
 Registers[IGW_RX_TICK_HI/4]=0;Registers[IGW_STATUS/4]|=IGW_RX_READY;++Registers[IGW_GOOD/4];
}
uint32_t igw_host_read(unsigned offset){
 if(TxFinish && Tick>=TxFinish && RtuFault!=2){
  ++Registers[IGW_TX_COUNT/4];Registers[IGW_STATUS/4]&=~(uint32_t)IGW_TX_BUSY;TxFinish=0;
#ifdef TEST_ROLE_MASTER
  ResponseAt=Tick+2000;
#endif
 }
 if(ResponseAt && Tick>=ResponseAt){
  ResponseAt=0;
  if(RtuFault!=1){uint8_t req[8],reply[256];uint16_t regs[32]={0x1234,1,0,10};regs[2]=(uint16_t)++ReplyCount;
   igw_make_read(req,1,0,4);size_t n=igw_slave_reply(req,8,1,regs,32,reply);if(RtuFault==3)reply[4]^=1;Queue(reply,n);}
 }
 return Registers[offset/4];
}
void igw_host_write(unsigned offset,uint32_t value){
 Registers[offset/4]=value;
 if(offset==IGW_RX_ACK)Registers[IGW_STATUS/4]&=~(uint32_t)IGW_RX_READY;
 if(offset==IGW_FEED)++FeedCount;
 if(offset==IGW_CONTROL){
  if(value)Registers[IGW_STATUS/4]|=IGW_TX_PERMIT;
  else Registers[IGW_STATUS/4]&=~(uint32_t)IGW_TX_PERMIT;
 }
 if(offset==IGW_TX_KICK && value==1){assert(Registers[IGW_STATUS/4]&IGW_TX_PERMIT);Registers[IGW_STATUS/4]|=IGW_TX_BUSY;TxFinish=Tick+10000;}
}
static void Fixture(int can_fault,unsigned rtu_fault){
 ResetFixture(can_fault);Tick=0;RtuFault=rtu_fault;TxFinish=ResponseAt=0;ReplyCount=FeedCount=0;
 memset(Registers,0,sizeof Registers);
 Registers[IGW_MAGIC/4]=0x32574749;Registers[IGW_VERSION/4]=0x00020000;
 Registers[IGW_CLOCK/4]=100000000;Registers[IGW_WD_CYCLES/4]=50000000;Registers[IGW_STATUS/4]=IGW_QUIET;
 RtuMin=RtuMax=RtuSum=0;
}
#ifdef TEST_ROLE_MASTER
struct netif gnetif;const ip_addr_t test_any_address={0};static struct udp_pcb Pcb;
static unsigned NetFault,CanPackets,RtuPackets,StatusPackets,NetCalls,Allocations,Frees,Samples;
void lwip_init(void){}void netif_config(void){}
void ethernetif_input(struct netif *n){assert(n==&gnetif);++NetCalls;}
void sys_check_timeouts(void){}
int ipaddr_aton(const char *s,ip_addr_t *a){assert(!strcmp(s,IGW_PC_IP));a->addr=1;return 1;}
struct udp_pcb *udp_new(void){return &Pcb;}
err_t udp_bind(struct udp_pcb *u,const ip_addr_t *a,u16_t port){assert(u==&Pcb&&a==IP_ADDR_ANY&&port==5001);return ERR_OK;}
void udp_remove(struct udp_pcb *u){assert(u==&Pcb);}
struct pbuf *pbuf_alloc(pbuf_layer l,u16_t n,pbuf_type t){
 assert(l==PBUF_TRANSPORT&&t==PBUF_RAM&&n<1200);if(NetFault==1)return NULL;
 struct pbuf *p=malloc(sizeof *p);assert(p);p->payload=calloc(1,n+1U);assert(p->payload);p->len=n;++Allocations;return p;
}
err_t pbuf_take(struct pbuf *p,const void *b,u16_t n){assert(p&&p->len==n);if(NetFault==2)return ERR_MEM;memcpy(p->payload,b,n);return ERR_OK;}
uint8_t pbuf_free(struct pbuf *p){free(p->payload);free(p);++Frees;return 1;}
err_t udp_sendto(struct udp_pcb *u,struct pbuf *p,const ip_addr_t *a,u16_t port){
 assert(u==&Pcb&&a==&Pc&&port==5000);if(NetFault==3)return ERR_MEM;
 if(strstr(p->payload,"\"kind\":\"can\""))++CanPackets;
 else if(strstr(p->payload,"\"kind\":\"rtu\""))++RtuPackets;
 else ++StatusPackets;
 if(Scenario==0 && RtuFault==0 && NetFault==0 && Samples<12){printf("PKT %s\n",(char *)p->payload);++Samples;}
 return ERR_OK;
}
static void MasterCase(int can_fault,unsigned rtu_fault,unsigned net_fault){
 Fixture(can_fault,rtu_fault);NetFault=net_fault;CanPackets=RtuPackets=StatusPackets=NetCalls=Allocations=Frees=0;
 assert(firmware_main()==AL_OK);assert(NetCalls>100&&Allocations==Frees);
 assert(Stats.attempts==100&&RtuStats.attempts==100);
 if(can_fault==0)assert(Stats.ok==100);else assert(Stats.ok==0);
 if(!rtu_fault)assert(RtuStats.ok==100);else assert(RtuStats.ok==0);
 if(net_fault){assert(Net.errors>200&&CanPackets==0&&RtuPackets==0);}else{
  assert(CanPackets==Stats.ok&&RtuPackets==RtuStats.ok&&StatusPackets>0&&Net.errors==0);}
 assert(Registers[IGW_CONTROL/4]==0); /* Includes stuck TX: authorization revoked. */
}
int main(void){
 MasterCase(0,0,0);MasterCase(1,0,0);MasterCase(7,0,0);MasterCase(0,1,0);MasterCase(0,2,0);MasterCase(0,3,0);
 MasterCase(0,0,1);MasterCase(0,0,2);MasterCase(0,0,3);
 Fixture(0,0);Registers[IGW_MAGIC/4]=0;assert(firmware_main()!=AL_OK&&Stats.attempts==0&&RtuStats.attempts==0);
 Fixture(15,0);assert(firmware_main()!=AL_OK&&Stats.config_error==1&&Stats.tx==0);
 puts("PASS: master simultaneous 100+100, isolated CAN/RS485 faults, UDP failures, init guards (11 scenarios)");return 0;
}
#else
int main(void){
 Fixture(0,0);assert(Setup()==AL_OK);memset(&RtuStats,0,sizeof RtuStats);memset(&RtuTx,0,sizeof RtuTx);memset(&CanReply,0,sizeof CanReply);
 Rtu.read=BoardRead;Rtu.write=BoardWrite;Rtu.now_us=BoardUs;assert(igw_configure(&Rtu,9600)==0);
 for(unsigned seq=1;seq<=100;++seq){
  Frame(&Pending,REQUEST_ID,seq,REQUEST_TAG);HasPending=1;uint8_t req[8];igw_make_read(req,1,0,4);Queue(req,8);
  for(unsigned i=0;i<200;++i){CanTick();RtuTick();}
  assert(Stats.ok==seq&&RtuStats.ok==seq&&LastSent.Id==REPLY_ID&&LastSent.Data[0]==seq);
  assert(Registers[IGW_CONTROL/4]==0);
 }
 /* Stuck RS485 TX does not prevent CAN reply submission. */
 RtuFault=2;uint8_t req[8];igw_make_read(req,1,0,4);Queue(req,8);
 Frame(&Pending,REQUEST_ID,101,REQUEST_TAG);HasPending=1;
 for(unsigned i=0;i<4000;++i){CanTick();RtuTick();}
 assert(Stats.ok==101&&RtuStats.tx_fail==1&&Registers[IGW_CONTROL/4]==0);
 /* A busy CAN reply cannot prevent RS485 reception/transmission. */
 Registers[IGW_STATUS/4]=IGW_QUIET;TxFinish=0;RtuFault=0;Scenario=7;Queue(req,8);
 Frame(&Pending,REQUEST_ID,102,REQUEST_TAG);HasPending=1;
 for(unsigned i=0;i<4000;++i){CanTick();RtuTick();}
 assert(RtuStats.ok==101&&Stats.tx_fail==1);
 /* Exercise FC06 and FC03 through actual generated RtuTick/TX scheduler. */
 Scenario=0;RtuFault=0;TxFinish=0;Registers[IGW_STATUS/4]=IGW_QUIET;
 uint8_t write_request[8]={1,6,0,16,0xab,0xcd,0,0};igw_append_crc(write_request,6);Queue(write_request,8);
 unsigned before_ok=RtuStats.ok;
 for(unsigned i=0;i<300;++i){CanTick();RtuTick();}
 assert(RtuRegisters[16]==0xabcd&&RtuRegisters[5]==1&&RtuStats.ok==before_ok+1);
 assert(Registers[IGW_TX_LEN/4]==8&&Registers[IGW_CONTROL/4]==0);
 for(unsigned i=0;i<8;++i)assert(((Registers[(IGW_TX_DATA+(i/4)*4)/4]>>(8*(i%4)))&255)==write_request[i]);
 uint8_t read_request[8];igw_make_read(read_request,1,16,1);Queue(read_request,8);
 for(unsigned i=0;i<300;++i){CanTick();RtuTick();}
 uint8_t expected_reply[7]={1,3,2,0xab,0xcd,0,0};igw_append_crc(expected_reply,5);
 assert(Registers[IGW_TX_LEN/4]==7&&Registers[IGW_CONTROL/4]==0);
 for(unsigned i=0;i<7;++i)assert(((Registers[(IGW_TX_DATA+(i/4)*4)/4]>>(8*(i%4)))&255)==expected_reply[i]);
 /* Real generated CAN scheduler and shared bank, not just protocol helper. */
 memset(&CanWriteCache,0,sizeof CanWriteCache);
 uint8_t business[8]={0x78,0x56,0x34,0x12,2,31,0x55,0xaa};
 memset(&Pending,0,sizeof Pending);Pending.Id=IGW_CAN_RW_REQUEST_ID;Pending.DataLen=AL_CAN_LEN_8;
 for(unsigned b=0;b<8;b++)Pending.Data[b/4]|=(AL_U32)business[b]<<(8*(b%4));
 HasPending=1;for(unsigned i=0;i<20;i++)CanTick();
 assert(LastSent.Id==IGW_CAN_RW_REPLY_ID && (LastSent.Data[1]&255)==0x82);
 assert(RtuRegisters[31]==0x55aa && RtuRegisters[5]==2);
 /* Repeated CAN write must not execute a second time. */
 HasPending=1;for(unsigned i=0;i<20;i++)CanTick();
 assert(RtuRegisters[5]==2);
 igw_make_read(read_request,1,31,1);Queue(read_request,8);
 for(unsigned i=0;i<300;i++){CanTick();RtuTick();}
 assert(Registers[IGW_TX_LEN/4]==7);
 assert((Registers[(IGW_TX_DATA+4)/4]&255)==0xaa);
 /* RS485 write -> CAN read through scheduler. */
 write_request[2]=0;write_request[3]=31;write_request[4]=0xbe;write_request[5]=0xef;
 igw_append_crc(write_request,6);Queue(write_request,8);
 for(unsigned i=0;i<300;i++){CanTick();RtuTick();}
 memset(&Pending,0,sizeof Pending);Pending.Id=IGW_CAN_RW_REQUEST_ID;Pending.DataLen=AL_CAN_LEN_8;
 Pending.Data[0]=0x12345679;Pending.Data[1]=1u|(31u<<8);HasPending=1;
 for(unsigned i=0;i<20;i++)CanTick();
 assert(LastSent.Id==IGW_CAN_RW_REPLY_ID && LastSent.Data[1]==0xefbe1f81u && RtuRegisters[5]==3);
 puts("PASS: slave simultaneous100+100, isolated timeouts, FC06, CAN RW/dedup and bidirectional shared-register readback through scheduler");return 0;
}
#endif
