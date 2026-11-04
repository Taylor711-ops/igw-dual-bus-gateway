/* Zynq PS side of node 3 file support.  Numeric RTU and uplink remain PL.
 * Connect UART1 through EMIO; UART0 is reserved for diagnostic output. */
#include <stdint.h>
#include "xparameters.h"
#include "xstatus.h"
#include "xuartps.h"
#include "xuartps_hw.h"
#include "xtime_l.h"
#include "xil_printf.h"
#include "file_engine.h"

#ifndef XPAR_PS7_UART_1_DEVICE_ID
#error Enable Zynq PS UART1 through EMIO in the hardware platform.
#endif
#define BAUD 9600u
#define GAP_US 3500u /* >1.5 chars and <3.5 chars at 9600, 8E1 */
#define UART_ERRORS (XUARTPS_IXR_PARITY|XUARTPS_IXR_FRAMING|XUARTPS_IXR_OVER)
static XUartPs Port;
static XTime Boot;

static uint64_t clock_us(void) {
    XTime now;
    XTime_GetTime(&now);
    /* Divide before multiplying to avoid overflow during long sessions. */
    uint64_t d=(uint64_t)(now-Boot);
    return (d/COUNTS_PER_SECOND)*1000000u+(d%COUNTS_PER_SECOND)*1000000u/COUNTS_PER_SECOND;
}
uint64_t rs3_clock_ms(void) { return clock_us()/1000u; }

static void drain_own_echo(u32 base) {
    while (XUartPs_IsReceiveData(base)) (void)XUartPs_RecvByte(base);
    XUartPs_WriteReg(base,XUARTPS_ISR_OFFSET,UART_ERRORS);
}
static void send_reply(u32 base,const uint8_t *p,size_t n) {
    for (size_t i=0;i<n;i++) XUartPs_SendByte(base,p[i]);
    while ((XUartPs_ReadReg(base,XUARTPS_SR_OFFSET)&(XUARTPS_SR_TXEMPTY|XUARTPS_SR_TACTIVE))!=XUARTPS_SR_TXEMPTY) { }
    drain_own_echo(base);
}

int main(void) {
    XTime_GetTime(&Boot);
    XUartPs_Config *cfg=XUartPs_LookupConfig(XPAR_PS7_UART_1_DEVICE_ID);
    if (!cfg||XUartPs_CfgInitialize(&Port,cfg,cfg->BaseAddress)!=XST_SUCCESS) {
        xil_printf("IGW3_FILE_UART_INIT=FAIL\r\n");return 1;
    }
    XUartPsFormat fmt={BAUD,XUARTPS_FORMAT_8_BITS,XUARTPS_FORMAT_EVEN_PARITY,XUARTPS_FORMAT_1_STOP_BIT};
    if (XUartPs_SetDataFormat(&Port,&fmt)!=XST_SUCCESS) {
        xil_printf("IGW3_FILE_UART_FORMAT=FAIL\r\n");return 1;
    }
    XUartPs_SetOperMode(&Port,XUARTPS_OPER_MODE_NORMAL);
    u32 base=Port.Config.BaseAddress;
    XUartPs_WriteReg(base,XUARTPS_ISR_OFFSET,UART_ERRORS);
    int storage=rs3_file_init();
    /* Numeric traffic may have arrived while the card was scanned. */
    drain_own_echo(base);
    xil_printf("IGW3_FILE_SD=%s reason=%s fresult=%u\r\n",storage?"FAIL":"READY",rs3_storage_reason(),rs3_storage_error());
    xil_printf("IGW3_FILE_UART=READY 9600 8E1 unit=3\r\n");
    uint8_t frame[256],out[256];size_t n=0;int bad=0;uint64_t last=0;
    for (;;) {
        u32 errors=XUartPs_ReadReg(base,XUARTPS_ISR_OFFSET)&UART_ERRORS;
        if (errors) { bad=1; XUartPs_WriteReg(base,XUARTPS_ISR_OFFSET,errors); }
        while (XUartPs_IsReceiveData(base)) {
            uint8_t c=XUartPs_RecvByte(base);uint64_t now=clock_us();
            if (n && now-last>=GAP_US) { n=0;bad=0; }
            if (n<sizeof frame) frame[n++]=c; else bad=1;
            last=now;
        }
        if (n&&clock_us()-last>=GAP_US) {
            if (!bad) {size_t reply=rs3_file_request(frame,n,out);if(reply)send_reply(base,out,reply);}
            n=0;bad=0;
        } else if (!n&&bad) bad=0;
        rs3_file_tick();
    }
}
