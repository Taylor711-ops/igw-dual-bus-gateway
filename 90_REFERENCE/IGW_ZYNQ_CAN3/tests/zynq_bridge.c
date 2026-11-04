/* Host-test bridge: executes production Zynq handle_one() with a mocked CAN
 * controller. This is a protocol test, not a Zynq or electrical test. */
#include <string.h>
#define main zynq_entry_not_executed
#include "../zynq/main.c"
#undef main
static u32 incoming_frame[16];static int rx_ready;
XCanPs_Config *XCanPs_LookupConfig(u16 id){static XCanPs_Config c={0};(void)id;return &c;}
s32 XCanPs_CfgInitialize(XCanPs*a,XCanPs_Config*b,u32 c){(void)a;(void)b;(void)c;return 0;}
s32 XCanPs_SelfTest(XCanPs*a){(void)a;return 0;}
void XCanPs_EnterMode(XCanPs*a,u8 b){(void)a;(void)b;}
u8 XCanPs_GetMode(XCanPs*a){(void)a;return 0;}
s32 XCanPs_SetBaudRatePrescaler(XCanPs*a,u8 b){(void)a;(void)b;return 0;}
s32 XCanPs_SetBitTiming(XCanPs*a,u8 b,u8 c,u8 d){(void)a;(void)b;(void)c;(void)d;return 0;}
int XCanPs_IsTxFifoFull(XCanPs*a){(void)a;return 0;}
int XCanPs_IsRxEmpty(XCanPs*a){(void)a;return !rx_ready;}
s32 XCanPs_Send(XCanPs*a,u32*b){(void)a;(void)b;return 0;}
s32 XCanPs_Recv(XCanPs*a,u32*b){(void)a;memcpy(b,incoming_frame,sizeof incoming_frame);rx_ready=0;return 0;}
void XCanPs_GetBusErrorCounter(XCanPs*a,u8*b,u8*c){(void)a;*b=*c=0;}
void XTime_GetTime(XTime *t){*t=0;}
void zynq_reset(void){memset(regs,0,sizeof regs);memset(&writes,0,sizeof writes);memset(&reply,0,sizeof reply);
 regs[0]=0x1234;regs[1]=3;regs[4]=0x0801;regs[6]=0xffff;regs[8]=0x0901;regs[9]=0x0d01;regs[10]=1;regs[13]=0x1a26;
 master_seen=0;ul_client_init(&upload,3,0);}
unsigned zynq_get(unsigned address){return address<32?regs[address]:0;}
unsigned zynq_can(unsigned id,const uint8_t *in,uint8_t *out,uint64_t us){
 memset(incoming_frame,0,sizeof incoming_frame);incoming_frame[0]=id<<21;incoming_frame[1]=8u<<28;
 for(unsigned i=0;i<4;i++){incoming_frame[2]|=(u32)in[i]<<(8*i);incoming_frame[3]|=(u32)in[4+i]<<(8*i);}
 rx_ready=1;handle_one(us/1000u);if(!reply.valid)return 0;
 unsigned result=reply.id;memcpy(out,reply.data,8);reply.valid=0;return result;
}
unsigned zynq_tick(uint64_t ms,uint8_t *out){
 if(!master_seen)return 0;
 struct ul_client candidate=upload;struct ul_frame proposed;
 int ready=ul_client_tick(&candidate,ms,source_value(ms),&proposed);
 upload=candidate;
 if(!ready)return 0;
 memcpy(out,proposed.data,8);return proposed.id;
}
