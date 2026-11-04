/* IGW CAN-only node 3 for the supplied Zynq-7020 can_key_led XSA.
 * Only classic CAN control/uplink are implemented; no RS485 or file service.
 * Source-level and simulated checks are not a physical board validation.
 */
#include <stdint.h>
#include <string.h>
#include "xcanps.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "xtime_l.h"
#include "igw_can_rw_protocol.h"
#include "uplink_protocol.h"

#define NODE_ID 3u
#define REQ_ID (0x210u + NODE_ID)
#define REPLY_ID (0x190u + NODE_ID)
#define CAN_HZ XPAR_XCANPS_0_CAN_CLK_FREQ_HZ
#if CAN_HZ != 100000000
#error This timing is for the supplied 100 MHz CAN clock only; recalculate and verify if hardware changes.
#endif
/* 100 MHz / (9+1) / (1 + (14+1) + (3+1)) = 500 kbit/s;
 * sample point (1+15)/20 = 80%. SJW = (3+1) = 4 TQ.
 */
#define BRP_REG 9u
#define TS1_REG 14u
#define TS2_REG 3u
#define SJW_REG 3u
#define MANUAL 0xa526u
#define BIDIR_VERSION 0x1a26u

static XCanPs can;
static uint16_t regs[32];
static struct igw_can_write_cache writes;
static struct ul_client upload;
static XTime start_ticks;
static unsigned long rx_count, tx_count, tx_fail;
/* Do not start unsolicited traffic until the master has queried this node. */
static unsigned master_seen;
static struct { unsigned valid,id; uint8_t data[8]; } reply;

static uint64_t elapsed_ms(void)
{
    XTime t; XTime_GetTime(&t); t -= start_ticks;
    return ((uint64_t)t / COUNTS_PER_SECOND) * 1000u +
        (((uint64_t)t % COUNTS_PER_SECOND) * 1000u) / COUNTS_PER_SECOND;
}
static int wait_mode(unsigned wanted)
{
    uint64_t until = elapsed_ms() + 250u;
    while (elapsed_ms() < until) if (XCanPs_GetMode(&can) == wanted) return 1;
    return 0;
}
static int init_can(void)
{
    XCanPs_Config *cfg = XCanPs_LookupConfig(XPAR_XCANPS_0_DEVICE_ID);
    if (!cfg || XCanPs_CfgInitialize(&can,cfg,cfg->BaseAddr)!=XST_SUCCESS) return 0;
    XCanPs_EnterMode(&can,XCANPS_MODE_CONFIG);
    if (!wait_mode(XCANPS_MODE_CONFIG)) return 0;
    if (XCanPs_SetBaudRatePrescaler(&can,BRP_REG)!=XST_SUCCESS ||
        XCanPs_SetBitTiming(&can,SJW_REG,TS2_REG,TS1_REG)!=XST_SUCCESS) return 0;
    XCanPs_EnterMode(&can,XCANPS_MODE_NORMAL);
    return wait_mode(XCANPS_MODE_NORMAL);
}
/* XCanPs swaps data words when accessing the peripheral; the software word
 * therefore holds data bytes in little-endian order on Cortex-A9. */
static int send_frame(unsigned id,const uint8_t p[8])
{
    u32 f[XCANPS_MAX_FRAME_SIZE / sizeof(u32)]={0};
    if (XCanPs_IsTxFifoFull(&can)) return 0;
    f[0]=XCanPs_CreateIdValue(id,0,0,0,0); f[1]=XCanPs_CreateDlcValue(8);
    for (unsigned i=0;i<4;i++) { f[2]|=(u32)p[i]<<(8*i); f[3]|=(u32)p[4+i]<<(8*i); }
    if (XCanPs_Send(&can,f)!=XST_SUCCESS) {tx_fail++;return 0;}
    tx_count++;return 1;
}
static uint16_t source_value(uint64_t ms)
{
    return regs[28]==MANUAL ? regs[30] : (uint16_t)(NODE_ID*1000u + ms/1000u);
}
static void handle_one(uint64_t ms)
{
    u32 f[XCANPS_MAX_FRAME_SIZE / sizeof(u32)]={0};
    if (reply.valid || XCanPs_IsRxEmpty(&can)) return;
    if (XCanPs_Recv(&can,f)!=XST_SUCCESS) return;
    if ((f[0] & (XCANPS_IDR_IDE_MASK|XCANPS_IDR_RTR_MASK|XCANPS_IDR_SRR_MASK)) ||
        ((f[1]&XCANPS_DLCR_DLC_MASK)>>XCANPS_DLCR_DLC_SHIFT)!=8u) return;
    unsigned id=(f[0]&XCANPS_IDR_ID1_MASK)>>XCANPS_IDR_ID1_SHIFT;
    uint8_t p[8];for(unsigned i=0;i<4;i++){p[i]=(uint8_t)(f[2]>>(8*i));p[4+i]=(uint8_t)(f[3]>>(8*i));}
    rx_count++;
    if (id==REQ_ID) {
        regs[3]=(uint16_t)(ms/1000u);
        igw_can_rw_reply(&writes,regs,p,reply.data,ms*1000u);
        if (reply.data[4]==0x81u || reply.data[4]==0x82u) master_seen=1;
        reply.id=REPLY_ID;reply.valid=1;
    } else if (id==UL_WELCOME+NODE_ID || id==UL_ACK+NODE_ID) {
        struct ul_frame in;in.id=id;memcpy(in.data,p,8);
        ul_client_receive(&upload,&in,ms,source_value(ms));
    }
}
int main(void)
{
    XTime_GetTime(&start_ticks);
    memset(regs,0,sizeof regs);regs[0]=0x1234;regs[1]=NODE_ID;
    regs[4]=IGW_RW_REVISION;regs[6]=0xffff;regs[8]=IGW_CAN_RW_REVISION;
    regs[9]=0x0d01;regs[10]=1;regs[13]=BIDIR_VERSION;
    ul_client_init(&upload,NODE_ID,0);
    xil_printf("IGW ZYNQ NODE3 CAN ONLY: XPAR CAN clock=%lu Hz\r\n",(unsigned long)CAN_HZ);
    if(!init_can()){xil_printf("CAN_INIT=FAIL\r\n");return XST_FAILURE;}
    xil_printf("CAN_READY unit=3 bitrate=500000 sample=80%% rx=0x213 tx=0x193\r\n");
    uint64_t report=10000u;
    for (;;) {
        uint64_t ms=elapsed_ms();
        if(reply.valid && send_frame(reply.id,reply.data)) reply.valid=0;
        for(unsigned n=0;n<16 && !reply.valid;n++) {
            if (XCanPs_IsRxEmpty(&can)) break;
            handle_one(ms);
            if(reply.valid && send_frame(reply.id,reply.data)) reply.valid=0;
        }
        if(master_seen && !reply.valid && !XCanPs_IsTxFifoFull(&can)) {
            struct ul_client candidate=upload;struct ul_frame out;
            int proposed=ul_client_tick(&candidate,ms,source_value(ms),&out);
            if(!proposed || send_frame(out.id,out.data)) upload=candidate;
        }
        if(ms>=report) {
            u8 rec=0,tec=0;XCanPs_GetBusErrorCounter(&can,&rec,&tec);
            xil_printf("CAN3 rx=%lu tx=%lu submit_fail=%lu REC=%u TEC=%u\r\n",rx_count,tx_count,tx_fail,rec,tec);
            report=ms+10000u;
        }
    }
}
