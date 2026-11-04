/* Generated master/main.c and slave/main.c are complete FD application files.
 * Uses the user's Anlogic CAN SDK. Classical CAN0 at nominal 500 kbit/s.
 * Application MASTER/SLAVE are test roles; CAN itself remains a multi-master bus.
 */
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "al_can_hal.h"
#include "al_log.h"
#ifndef CAN0PS_ENABLE
#error "Use igw_can_plat with CAN0 enabled"
#endif
#if CAN0_TYPE != CAN20B || CAN0_SLOW_RATE != 500000
#error "Use CAN0 classical CAN nominal 500k platform"
#endif

#define REQUEST_ID 0x201U
#define REPLY_ID   0x181U
#define REQUEST_TAG 0x43414e00U
#define REPLY_TAG   0x52535000U
#define POLLS 100U
#define WAIT_MS 200U
#define SPIN_LIMIT 10000000U

typedef struct {
    AL_U32 attempts, tx, rx, ok, timeout, tx_fail, rx_error, mismatch, ignored;
    AL_U32 bad_request, config_error, timer_error, rec_max, tec_max;
    AL_U32 koer_seen, busoff_seen, overflow_seen;
    AL_U32 rec, tec, koer, overflow, busoff;
    AL_U64 min_ticks, max_ticks, total_ticks;
} IgwCanStats;
static IgwCanStats Stats;
static AL_CAN_HalStruct *Can;
static AL_U64 TimerHz;
static AL_CAN_InitStruct Config = {
    .Protocol = AL_CAN_TYPE_2_0B,
    .TxIntrMode = AL_CAN_RUN_POLLING,
    .RxIntrMode = AL_CAN_RUN_POLLING,
    .SlowBitRate = AL_CAN_ARBITRATION_0_5M,
    .FastBitRate = AL_CAN_ARBITRATION_0_5M
};
static AL_U64 Now(AL_VOID) { return AlSys_GetTimerTickCount(); }
static AL_U64 Budget(AL_U32 ms) { return (TimerHz * ms + 999U) / 1000U; }
static AL_U32 Us(AL_U64 ticks) { return (AL_U32)(ticks * 1000000ULL / TimerHz); }

static AL_VOID Snapshot(AL_BOOL print)
{
    AL_REG base = Can->Dev.BaseAddr;
    AL_U32 rec = AL_REG32_GET_BITS(base + CAN_TECNT_REVNT_TDC_EALCAP_OFFSET,
        CAN_TECNT_REVNT_TDC_EALCAP_RECNT_SHIFT, CAN_TECNT_REVNT_TDC_EALCAP_RECNT_SIZE);
    AL_U32 tec = AL_REG32_GET_BITS(base + CAN_TECNT_REVNT_TDC_EALCAP_OFFSET,
        CAN_TECNT_REVNT_TDC_EALCAP_TECNT_SHIFT, CAN_TECNT_REVNT_TDC_EALCAP_TECNT_SIZE);
    AL_U32 koer = AlCan_ll_GetKoer(base);
    AL_U32 overflow = AL_REG32_GET_BIT(base + CAN_RCTRL_TCTRL_TCMD_CFG_STAT_OFFSET,
        CAN_RCTRL_TCTRL_TCMD_CFG_STAT_ROV_SHIFT);
    AL_U32 busoff = AlCan_ll_IsBusOff(base);
    Stats.rec=rec;Stats.tec=tec;Stats.koer=koer;Stats.overflow=overflow;Stats.busoff=busoff;
    if (rec > Stats.rec_max) Stats.rec_max = rec;
    if (tec > Stats.tec_max) Stats.tec_max = tec;
    Stats.koer_seen |= koer ? 1U << koer : 0U;
    Stats.overflow_seen |= overflow;
    Stats.busoff_seen |= busoff;
    if (print) AL_LOG(AL_LOG_LEVEL_NOTICE,
        "[CAN-HW] REC=%u TEC=%u KOER=%u overflow=%u busoff=%u\r\n",
        rec, tec, koer, overflow, busoff);
}

static AL_S32 Setup(AL_VOID)
{
    TimerHz = AlSys_GetTimerFreq();
    if (TimerHz < 1000ULL || TimerHz > 1000000000ULL) {
        ++Stats.timer_error; return (AL_S32)AL_CAN_ERR_ILLEGAL_PARAM;
    }
    AL_S32 ret = AlCan_Hal_Init(&Can, 0, &Config, AL_NULL);
    if (ret != AL_OK || !Can) { ++Stats.config_error; return ret != AL_OK ? ret : (AL_S32)AL_CAN_ERR_NULL_PTR; }
    AL_REG base = Can->Dev.BaseAddr;
    if (base != CAN0__BASE_ADDR) { ++Stats.config_error; return (AL_S32)AL_CAN_ERR_ILLEGAL_PARAM; }
    /* The SDK Init defaults to internal loopback. Explicitly replace it while
     * reset is asserted. This avoids an SDK config-mode busy-wait with no deadline.
     * No TX is submitted until all mode/timing fields have been checked below. */
    AlCan_ll_ClrIntr(base, AL_CAN_INTR_MASK_EN_ALL);
    AlCan_ll_SetCanCtrlReset(base, AL_TRUE);
    AlCan_ll_SetLbmi(base, AL_FALSE);
    AlCan_ll_SetLbme(base, AL_FALSE);
    AlCan_ll_SetLom(base, AL_FALSE);
    AlCan_ll_SetStby(base, AL_FALSE);
    AlCan_ll_SetSack(base, AL_CAN_SACK_NO_SELF_ACK);
    /* v1.2: disabled acceptance filters reject RX; they are NOT accept-all.
     * Enable filter 0 for every standard ID; application validates ID/tag/seq.
     * For this core mask bit 1 ignores the corresponding ID bit. */
    for (AL_U32 filter = 0; filter < 3U; ++filter) AlCan_ll_SetAcfenx(base, filter, AL_FALSE);
    Can->Dev.Host.OpsMode = AL_CAN_MODE_CONFIG;
    AL_CAN_FilterCfgStruct filter_cfg = {
        .FilterEn = AL_TRUE, .FilterIndex = AL_CAN_FILTER_0,
        .MaskType = AL_CAN_FILTER_MASK_STD,
        .MaskValue = 0x1fffffffU, .IdValue = 0U
    };
    ret = AlCan_Dev_SetFilter(&Can->Dev, &filter_cfg);
    if (ret != AL_OK) { ++Stats.config_error; return ret; }
    AlCan_ll_SetAcfadr(base, AL_CAN_FILTER_0);
    AlCan_ll_SetSelmask(base, AL_CAN_SELMASK_ACCEPT_MASK);
    DSB();
    AL_U32 filter_mask = AlCan_ll_ReadAcodexAmaskx(base);
    AlCan_ll_SetSelmask(base, AL_CAN_SELMASK_ACCEPT_CODE);
    DSB();
    AL_U32 filter_code = AlCan_ll_ReadAcodexAmaskx(base);
    AL_U32 expected_mask = filter_cfg.MaskValue |
        ((AL_U32)filter_cfg.MaskType << CAN_ACF_3_ACF_2_ACF_1_ACF_0_AIDE_SHIFT);
    if (AlCan_ll_IsAcfen(base) != 1U || filter_mask != expected_mask || filter_code != 0U) {
        AL_LOG(AL_LOG_LEVEL_NOTICE,
            "[CAN-FILTER] readback FAILED enabled=0x%x mask=0x%08x code=0x%08x\r\n",
            AlCan_ll_IsAcfen(base), filter_mask, filter_code);
        ++Stats.config_error; return (AL_S32)AL_CAN_ERR_ILLEGAL_PARAM;
    }
    Can->Dev.Host.OpsMode = AL_CAN_MODE_NORMAL;
    AlCan_ll_SetCanCtrlReset(base, AL_FALSE);
    /* v1.1: set TPSS AFTER reset release. On the user's cold boot the earlier
     * write during reset read back as zero. Keep the readback guard below;
     * do not silently permit persistent TX retries when TPSS does not latch. */
    DSB();
    AlSys_MDelay(1);
    AlCan_ll_SetTpss(base, AL_TRUE);
    DSB();
    AL_U32 fd = AL_REG32_GET_BIT(CAN_TOP_CFG_CTRL_ADDR, CAN_TOP_CFG_CTRL_ADDR_CAN0_FD_ENABLE_SHIFT);
    AL_U32 lbmi = AlCan_ll_IsLbmiEnabled(base), lbme = AlCan_ll_IsLbmeEnabled(base);
    AL_U32 lom = AlCan_ll_IsLom(base), stby = AlCan_ll_IsStby(base);
    AL_U32 sack = AL_REG32_GET_BIT(base + CAN_RCTRL_TCTRL_TCMD_CFG_STAT_OFFSET,
                                   CAN_RCTRL_TCTRL_TCMD_CFG_STAT_SACK_SHIFT);
    AL_U32 shot = AL_REG32_GET_BIT(base + CAN_RCTRL_TCTRL_TCMD_CFG_STAT_OFFSET,
                                   CAN_RCTRL_TCTRL_TCMD_CFG_STAT_TPSS_SHIFT);
    AL_U32 seg1 = AlCan_ll_GetSseg1(base)+2U, seg2 = AlCan_ll_GetSseg2(base)+1U;
    AL_U32 sjw = AlCan_ll_GetSsjw(base)+1U, presc = AlCan_ll_GetSpresc(base)+1U;
    AL_U32 filter_enabled = AlCan_ll_IsAcfen(base);
    AL_LOG(AL_LOG_LEVEL_NOTICE,
        "[CAN-FILTER] enabled=0x%x mask_raw=0x%08x code=0x%08x accept=ALL_STD\r\n",
        filter_enabled, filter_mask, filter_code);
    AL_LOG(AL_LOG_LEVEL_NOTICE,
        "[CAN-MODE] fd=%u internal=%u external=%u listen=%u standby=%u self_ack=%u single_shot=%u\r\n",
        fd, lbmi, lbme, lom, stby, sack, shot);
    AL_LOG(AL_LOG_LEVEL_NOTICE,
        "[CAN-TIMING] seg1=%u seg2=%u sjw=%u prescaler=%u target=500000 timer=%u\r\n",
        seg1, seg2, sjw, presc, (AL_U32)TimerHz);
    if (filter_enabled!=1U || fd || lbmi || lbme || lom || stby || sack || !shot || seg1!=64U || seg2!=16U || sjw!=16U || presc!=2U ||
        Can->Dev.Host.InputClk!=80000000U || Can->Dev.Host.Protocol!=AL_CAN_TYPE_2_0B ||
        Can->Dev.Host.TxIntrMode!=AL_CAN_RUN_POLLING || Can->Dev.Host.RxIntrMode!=AL_CAN_RUN_POLLING) {
        ++Stats.config_error; return (AL_S32)AL_CAN_ERR_ILLEGAL_PARAM;
    }
    Snapshot(AL_TRUE);
    return AL_OK;
}

static AL_VOID Frame(AL_CAN_FrameStruct *frame, AL_U32 id, AL_U32 seq, AL_U32 tag)
{
    memset(frame, 0, sizeof(*frame));
    frame->BufferMode=AL_CAN_BUFFER_MODE_PTB; frame->Id=id; frame->DataLen=AL_CAN_LEN_8;
    frame->Data[0]=seq; frame->Data[1]=tag^seq;
}
static AL_BOOL Valid(const AL_CAN_FrameStruct *frame, AL_U32 tag)
{
    return frame->DataLen==AL_CAN_LEN_8 && !frame->IsIdExt && !frame->IsRemote &&
        !frame->IsBitSwitch && frame->Koer==AL_CAN_KOER_NONE && frame->Data[0]!=0U &&
        frame->Data[1]==(tag^frame->Data[0]);
}

static AL_S32 Receive(AL_CAN_FrameStruct *frame)
{
    memset(frame,0,sizeof(*frame));
    AL_S32 ret=AlCan_Hal_RecvFrame(Can,frame);
    if(ret==AL_OK) ++Stats.rx;
    else if(ret!=AL_CAN_ERR_RECV_EMPTY) {
        ++Stats.rx_error;
        /* The supplied driver leaves KOER-error frames at the FIFO head. */
        if(ret==AL_CAN_ERR_RECV) AlCan_ll_SetRrel(Can->Dev.BaseAddr,AL_CAN_RREL_RELEASE);
    }
    return ret;
}
static AL_VOID Summary(AL_VOID)
{
    Snapshot(AL_TRUE);
    AL_LOG(AL_LOG_LEVEL_NOTICE,
        "[CAN-STATS] attempts=%u tx_submit=%u rx=%u ok=%u timeout=%u tx_fail=%u rx_error=%u mismatch=%u ignored=%u bad_request=%u\r\n",
        Stats.attempts,Stats.tx,Stats.rx,Stats.ok,Stats.timeout,Stats.tx_fail,Stats.rx_error,
        Stats.mismatch,Stats.ignored,Stats.bad_request);
    AL_LOG(AL_LOG_LEVEL_NOTICE,
        "[CAN-STATS] config_error=%u timer_error=%u REC_max=%u TEC_max=%u KOER_seen=0x%x overflow_seen=%u busoff_seen=%u\r\n",
        Stats.config_error,Stats.timer_error,Stats.rec_max,Stats.tec_max,Stats.koer_seen,
        Stats.overflow_seen,Stats.busoff_seen);
    if(Stats.ok && IGW_CAN_MASTER) AL_LOG(AL_LOG_LEVEL_NOTICE,
        "[CAN-RTT] min=%u avg=%u max=%u us (request start to PS reply read)\r\n",
        Us(Stats.min_ticks),Us(Stats.total_ticks/Stats.ok),Us(Stats.max_ticks));
}
static AL_BOOL Clean(AL_VOID)
{
    return !(Stats.timeout||Stats.tx_fail||Stats.rx_error||Stats.mismatch||Stats.bad_request||
        Stats.config_error||Stats.timer_error||Stats.rec_max||Stats.tec_max||Stats.koer_seen||
        Stats.overflow_seen||Stats.busoff_seen);
}
