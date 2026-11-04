#ifndef MOCK_XCANPS_H
#define MOCK_XCANPS_H
#include <stdint.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int32_t s32;
typedef struct {u32 BaseAddr;} XCanPs_Config;
typedef struct {int unused;} XCanPs;
#define XST_SUCCESS 0
#define XST_FAILURE 1
#define XCANPS_MAX_FRAME_SIZE 64u
#define XCANPS_MODE_CONFIG 1u
#define XCANPS_MODE_NORMAL 2u
#define XCANPS_IDR_ID1_MASK 0xFFE00000u
#define XCANPS_IDR_ID1_SHIFT 21u
#define XCANPS_IDR_SRR_MASK 0x00100000u
#define XCANPS_IDR_IDE_MASK 0x00080000u
#define XCANPS_IDR_RTR_MASK 0x00000001u
#define XCANPS_DLCR_DLC_MASK 0xF0000000u
#define XCANPS_DLCR_DLC_SHIFT 28u
#define XCanPs_CreateIdValue(id,srr,ide,id2,rtr) (((id)<<21)|((srr)<<20)|((ide)<<19)|((id2)<<1)|(rtr))
#define XCanPs_CreateDlcValue(n) ((n)<<28)
XCanPs_Config *XCanPs_LookupConfig(u16 id);
s32 XCanPs_CfgInitialize(XCanPs*,XCanPs_Config*,u32);
s32 XCanPs_SelfTest(XCanPs*);
void XCanPs_EnterMode(XCanPs*,u8);
u8 XCanPs_GetMode(XCanPs*);
s32 XCanPs_SetBaudRatePrescaler(XCanPs*,u8);
s32 XCanPs_SetBitTiming(XCanPs*,u8,u8,u8);
int XCanPs_IsTxFifoFull(XCanPs*);
int XCanPs_IsRxEmpty(XCanPs*);
s32 XCanPs_Send(XCanPs*,u32*);
s32 XCanPs_Recv(XCanPs*,u32*);
void XCanPs_GetBusErrorCounter(XCanPs*,u8*,u8*);
#endif
