#ifndef IGW_TEST_CAN_HAL_H
#define IGW_TEST_CAN_HAL_H
#include <stdint.h>
#include <stdio.h>
typedef void AL_VOID;
typedef uint32_t AL_U32;
typedef uint64_t AL_U64;
typedef int32_t AL_S32;
typedef uintptr_t AL_REG;
typedef unsigned AL_BOOL;
#define AL_TRUE 1U
#define AL_FALSE 0U
#define AL_NULL NULL
#define AL_OK 0
#define AL_CAN_ERR_TIMEOUT -1
#define AL_CAN_ERR_TP_BUSY -2
#define AL_CAN_ERR_IN_TS_FULL -3
#define AL_CAN_ERR_BUSY -4
#define AL_CAN_ERR_RECV_EMPTY -5
#define AL_CAN_ERR_RECV -6
#define AL_CAN_ERR_NULL_PTR -7
#define AL_CAN_ERR_ILLEGAL_PARAM -8
#define CAN20B 1
#define CAN0_TYPE CAN20B
#define CAN0_SLOW_RATE 500000
#define CAN0PS_ENABLE
#define HPF_EXPORT_TIME "HOST_TEST"
#define BITSTREAM_GENERATE_TIME "HOST_TEST_NOT_A_BIT"
#define CAN0__BASE_ADDR 0xf8408000UL
#define CAN_TOP_CFG_CTRL_ADDR 0xf8800000UL
#define CAN_TOP_CFG_CTRL_ADDR_CAN0_FD_ENABLE_SHIFT 0U
#define CAN_TECNT_REVNT_TDC_EALCAP_OFFSET 0xb0U
#define CAN_TECNT_REVNT_TDC_EALCAP_RECNT_SHIFT 16U
#define CAN_TECNT_REVNT_TDC_EALCAP_RECNT_SIZE 8U
#define CAN_TECNT_REVNT_TDC_EALCAP_TECNT_SHIFT 24U
#define CAN_TECNT_REVNT_TDC_EALCAP_TECNT_SIZE 8U
#define CAN_RCTRL_TCTRL_TCMD_CFG_STAT_OFFSET 0U
#define CAN_RCTRL_TCTRL_TCMD_CFG_STAT_ROV_SHIFT 29U
typedef enum {AL_CAN_TYPE_FD,AL_CAN_TYPE_2_0B} AL_CAN_ProtocolEnum;
typedef enum {AL_CAN_RUN_POLLING,AL_CAN_RUN_INTR} AL_CAN_RunModeEnum;
typedef enum {AL_CAN_ARBITRATION_0_25M,AL_CAN_ARBITRATION_0_5M} AL_CAN_BitRateEnum;
typedef enum {AL_CAN_LEN_0=0,AL_CAN_LEN_8=8} AL_CAN_DataLenEnum;
typedef enum {AL_CAN_KOER_NONE,AL_CAN_KOER_CRC=5} AL_CAN_KoerEnum;
typedef enum {AL_CAN_BUFFER_MODE_PTB,AL_CAN_BUFFER_MODE_STB} AL_CAN_BufferModeEnum;
typedef enum {AL_CAN_MODE_CONFIG,AL_CAN_MODE_NORMAL,AL_CAN_MODE_IN_LOOPBACK} AL_CAN_OpsModeEnum;
typedef enum {AL_CAN_SACK_NO_SELF_ACK,AL_CAN_SACK_SELF_ACK} AL_CAN_SackEnum;
typedef enum {AL_CAN_RREL_NO_RELEASE,AL_CAN_RREL_RELEASE} AL_CAN_RrelEnum;
typedef enum {AL_CAN_FILTER_0,AL_CAN_FILTER_1,AL_CAN_FILTER_2} AL_CAN_FilterIndexEnum;
typedef enum {AL_CAN_FILTER_MASK_BOTH=0,AL_CAN_FILTER_MASK_STD=2,AL_CAN_FILTER_MASK_EXT=3} AL_CAN_FilterMaskTypeEnum;
typedef enum {AL_CAN_SELMASK_ACCEPT_CODE,AL_CAN_SELMASK_ACCEPT_MASK} AL_CAN_SelmaskEnum;
typedef struct {AL_BOOL FilterEn;AL_CAN_FilterIndexEnum FilterIndex;
 AL_CAN_FilterMaskTypeEnum MaskType;AL_U32 MaskValue,IdValue;} AL_CAN_FilterCfgStruct;
#define CAN_ACF_3_ACF_2_ACF_1_ACF_0_AIDE_SHIFT 29U
typedef struct {AL_CAN_ProtocolEnum Protocol; AL_CAN_RunModeEnum TxIntrMode,RxIntrMode;
 AL_CAN_BitRateEnum SlowBitRate,FastBitRate;} AL_CAN_InitStruct;
typedef struct {AL_CAN_BufferModeEnum BufferMode;AL_U32 Id;AL_BOOL IsIdExt,IsRemote,IsBitSwitch,IsEnTts;
 AL_CAN_KoerEnum Koer;AL_CAN_DataLenEnum DataLen;AL_U32 Data[16],Rts[2];} AL_CAN_FrameStruct;
typedef struct {AL_REG BaseAddr;struct {AL_CAN_ProtocolEnum Protocol;AL_CAN_RunModeEnum TxIntrMode,RxIntrMode;AL_U32 InputClk;AL_CAN_OpsModeEnum OpsMode;}Host;} AL_CAN_DevStruct;
typedef struct {AL_CAN_DevStruct Dev;} AL_CAN_HalStruct;
#define AL_CAN_INTR_MASK_EN_ALL 0x002aaaaaU
#define DSB() do {} while(0)
AL_U32 mock_bits(AL_REG,AL_U32,AL_U32);
#define AL_REG32_GET_BITS(a,s,n) mock_bits(a,s,n)
#define AL_REG32_GET_BIT(a,s) mock_bits(a,s,1)
AL_S32 AlCan_Hal_Init(AL_CAN_HalStruct **,AL_U32,AL_CAN_InitStruct *,AL_VOID *);
AL_S32 AlCan_Hal_SendFrame(AL_CAN_HalStruct *,AL_CAN_FrameStruct *);
AL_S32 AlCan_Hal_RecvFrame(AL_CAN_HalStruct *,AL_CAN_FrameStruct *);
AL_VOID AlCan_ll_SetCanCtrlReset(AL_REG,AL_BOOL);
AL_VOID AlCan_ll_ClrIntr(AL_REG,AL_U32);
AL_U32 AlCan_ll_IsLbmiEnabled(AL_REG);
AL_U32 AlCan_ll_IsLbmeEnabled(AL_REG);
AL_U32 AlCan_ll_GetSseg1(AL_REG);
AL_U32 AlCan_ll_GetSseg2(AL_REG);
AL_U32 AlCan_ll_GetSsjw(AL_REG);
AL_U32 AlCan_ll_GetSpresc(AL_REG);
AL_U32 AlCan_ll_GetKoer(AL_REG);
AL_U32 AlCan_ll_IsBusOff(AL_REG);
AL_U64 AlSys_GetTimerFreq(AL_VOID);
AL_U64 AlSys_GetTimerTickCount(AL_VOID);
AL_VOID AlSys_MDelay(AL_U64);
#define CAN_RCTRL_TCTRL_TCMD_CFG_STAT_TPSS_SHIFT 4U
#define CAN_RCTRL_TCTRL_TCMD_CFG_STAT_SACK_SHIFT 31U
AL_VOID AlCan_ll_SetLbmi(AL_REG,AL_BOOL);
AL_VOID AlCan_ll_SetLbme(AL_REG,AL_BOOL);
AL_VOID AlCan_ll_SetLom(AL_REG,AL_BOOL);
AL_VOID AlCan_ll_SetStby(AL_REG,AL_BOOL);
AL_VOID AlCan_ll_SetSack(AL_REG,AL_CAN_SackEnum);
AL_VOID AlCan_ll_SetTpss(AL_REG,AL_BOOL);
AL_VOID AlCan_ll_SetAcfenx(AL_REG,AL_U32,AL_BOOL);
AL_U32 AlCan_ll_IsAcfen(AL_REG);
AL_VOID AlCan_ll_SetAcfadr(AL_REG,AL_U32);
AL_VOID AlCan_ll_SetSelmask(AL_REG,AL_CAN_SelmaskEnum);
AL_U32 AlCan_ll_ReadAcodexAmaskx(AL_REG);
AL_S32 AlCan_Dev_SetFilter(AL_CAN_DevStruct *,AL_CAN_FilterCfgStruct *);
AL_VOID AlCan_ll_SetRrel(AL_REG,AL_CAN_RrelEnum);
AL_U32 AlCan_ll_IsLom(AL_REG);
AL_U32 AlCan_ll_IsStby(AL_REG);
#endif
