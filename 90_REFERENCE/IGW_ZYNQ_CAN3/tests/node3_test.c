#include <assert.h>
#include <stdio.h>
#include <string.h>
#define main zynq_entry_not_executed
#include "../zynq/main.c"
#undef main
static u32 pending_rx[16],last_tx[16];static int rx_ready,tx_block;static u8 mode;
XCanPs_Config *XCanPs_LookupConfig(u16 id){static XCanPs_Config c={0};(void)id;return &c;}
s32 XCanPs_CfgInitialize(XCanPs*a,XCanPs_Config*b,u32 c){(void)a;(void)b;(void)c;return 0;}
s32 XCanPs_SelfTest(XCanPs*a){(void)a;return 0;}
void XCanPs_EnterMode(XCanPs*a,u8 b){(void)a;mode=b;}
u8 XCanPs_GetMode(XCanPs*a){(void)a;return mode;}
s32 XCanPs_SetBaudRatePrescaler(XCanPs*a,u8 b){(void)a;assert(b==9);return 0;}
s32 XCanPs_SetBitTiming(XCanPs*a,u8 b,u8 c,u8 d){(void)a;assert(b==3&&c==3&&d==14);return 0;}
int XCanPs_IsTxFifoFull(XCanPs*a){(void)a;return tx_block;}
int XCanPs_IsRxEmpty(XCanPs*a){(void)a;return !rx_ready;}
s32 XCanPs_Send(XCanPs*a,u32*b){(void)a;memcpy(last_tx,b,sizeof last_tx);return 0;}
s32 XCanPs_Recv(XCanPs*a,u32*b){(void)a;memcpy(b,pending_rx,sizeof pending_rx);rx_ready=0;return 0;}
void XCanPs_GetBusErrorCounter(XCanPs*a,u8*b,u8*c){(void)a;*b=*c=0;}
void XTime_GetTime(XTime *t){static XTime now;*t=now++;}
static void incoming(unsigned id,const unsigned char d[8],unsigned flags){
 memset(pending_rx,0,sizeof pending_rx);pending_rx[0]=(id<<21)|flags;pending_rx[1]=8u<<28;
 for(unsigned i=0;i<4;i++){pending_rx[2]|=(u32)d[i]<<(i*8);pending_rx[3]|=(u32)d[4+i]<<(i*8);}rx_ready=1;
}
static void take_reply(unsigned id,const unsigned char d[8],unsigned ms){incoming(id,d,0);handle_one(ms);}
int main(void){
 assert(init_can()); memset(regs,0,sizeof regs);memset(&writes,0,sizeof writes);
 regs[0]=0x1234;regs[1]=3;regs[4]=0x0801;regs[6]=0xffff;regs[8]=0x0901;regs[13]=0x1a26;
 unsigned char read[8]={1,0,0,0,1,1,0,0};
 take_reply(0x213,read,1);assert(master_seen&&reply.valid&&reply.id==0x193&&reply.data[4]==0x81&&reply.data[7]==3);
 tx_block=1;assert(!send_frame(reply.id,reply.data));tx_block=0;assert(send_frame(reply.id,reply.data));
 assert((last_tx[0]>>21)==0x193);assert(last_tx[2]==0x00000001u);assert(last_tx[3]==0x03000181u);reply.valid=0;
 unsigned char wr[8]={2,0,0,0,2,16,0x30,0x39};take_reply(0x213,wr,3);
 assert(reply.valid&&reply.data[4]==0x82&&regs[16]==12345&&regs[5]==1&&regs[6]==16&&regs[7]==12345);reply.valid=0;
 take_reply(0x213,wr,4);assert(reply.valid&&regs[5]==1);reply.valid=0;
 wr[7]++;take_reply(0x213,wr,5);assert(reply.data[4]==0xe4&&regs[16]==12345&&regs[5]==1);reply.valid=0;
 read[0]=3;take_reply(0x212,read,6);assert(!reply.valid);
 incoming(0x213,read,XCANPS_IDR_IDE_MASK);handle_one(7);assert(!reply.valid);
 ul_client_init(&upload,3,0);struct ul_server_node host={0};uint32_t counter=1;unsigned changed=0;struct ul_frame a,b;
 assert(ul_client_tick(&upload,0,3000,&a)&&a.id==0x303);
 assert(ul_server_receive(&host,3,&counter,&a,&b,0,&changed)&&b.id==0x343);
 take_reply(b.id,b.data,1);assert(!reply.valid);
 assert(ul_client_tick(&upload,1,3000,&a)&&a.id==0x383);
 assert(ul_server_receive(&host,3,&counter,&a,&b,2,&changed)&&changed&&host.value==3000&&b.id==0x3c3);
 take_reply(b.id,b.data,2);assert(upload.acked==1&&upload.phase==UL_IDLE);
 regs[28]=MANUAL;regs[30]=54321;assert(source_value(1500)==54321);
 assert(ul_client_tick(&upload,1002,source_value(1002),&a)&&a.id==0x383);
 assert(ul_server_receive(&host,3,&counter,&a,&b,1002,&changed)&&changed&&host.value==54321);
 take_reply(b.id,b.data,1002);assert(upload.acked==2);
 puts("PASS_NODE3_PROTOCOL simulated RX/TX packing, id filter, read/write, duplicate suppression and CAN uplink");
}
