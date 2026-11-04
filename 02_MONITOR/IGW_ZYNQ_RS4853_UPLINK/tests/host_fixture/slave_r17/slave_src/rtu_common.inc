#define IGW_LOG(...) AL_LOG(AL_LOG_LEVEL_NOTICE,__VA_ARGS__)
#define IGW_BASE ((uintptr_t)0x80100000U)
static igw_device Rtu;
static igw_counters RtuBaseline;
static igw_udp_metrics RtuStats;
static AL_U64 RtuMin,RtuMax,RtuSum,BootTick;
static AL_U64 Microseconds(AL_U64 ticks)
{
    return ticks/TimerHz*1000000ULL+(ticks%TimerHz)*1000000ULL/TimerHz;
}
static uint64_t BoardUs(void *opaque) { (void)opaque;return Microseconds(Now()); }
#ifdef IGW_HOST_TEST
uint32_t igw_host_read(unsigned offset);
void igw_host_write(unsigned offset,uint32_t value);
static uint32_t BoardRead(void *opaque,unsigned offset){(void)opaque;return igw_host_read(offset);}
static void BoardWrite(void *opaque,unsigned offset,uint32_t value){(void)opaque;igw_host_write(offset,value);}
#else
static uint32_t BoardRead(void *opaque,unsigned offset)
{
    (void)opaque;DSB();uint32_t v=*(volatile uint32_t *)(IGW_BASE+offset);DSB();return v;
}
static void BoardWrite(void *opaque,unsigned offset,uint32_t value)
{
    (void)opaque;DSB();*(volatile uint32_t *)(IGW_BASE+offset)=value;DSB();
}
#endif
/* Bounded, cooperative RS485 TX. No busy wait during the runtime. */
static struct {
    unsigned phase;
    uint8_t bytes[256];size_t length;
    uint32_t before;
    AL_U64 started,feed,latest_start,latest_finish;
} RtuTx;
static int RtuTxBegin(const uint8_t *p,size_t n)
{
    if (RtuTx.phase || !p || n<4U || n>256U) return IGW_BAD_LENGTH;
    memcpy(RtuTx.bytes,p,n);RtuTx.length=n;RtuTx.phase=1;RtuTx.started=BoardUs(NULL);RtuTx.latest_start=RtuTx.latest_finish=0;
    return IGW_OK;
}
/* 0=waiting, 1=completed, -1=failed. Every exit revokes TX authorization. */
static int RtuTxTick(void)
{
    if (!RtuTx.phase) return 0;
    AL_U64 now=BoardUs(NULL);uint32_t s=igw_rd(&Rtu,IGW_STATUS);
    if (RtuTx.phase==2 && igw_rd(&Rtu,IGW_TX_COUNT)!=RtuTx.before && !(s&IGW_TX_BUSY)) {
        igw_wr(&Rtu,IGW_CONTROL,0);RtuTx.phase=0;return 1;
    }
    if ((RtuTx.phase==1 && RtuTx.latest_start && now>=RtuTx.latest_start) ||
        (RtuTx.latest_finish && now>=RtuTx.latest_finish) || now-RtuTx.started>=200000ULL) {
        igw_wr(&Rtu,IGW_CONTROL,0);RtuTx.phase=0;return -1;
    }
    if (RtuTx.phase==1 && !(s&IGW_TX_BUSY) && (s&IGW_QUIET)) {
        for (unsigned i=0;i<RtuTx.length;i+=4) {
            uint32_t v=0;
            for (unsigned b=0;b<4 && i+b<RtuTx.length;++b)v|=(uint32_t)RtuTx.bytes[i+b]<<(8*b);
            igw_wr(&Rtu,IGW_TX_DATA+i,v);
        }
        igw_wr(&Rtu,IGW_TX_LEN,(uint32_t)RtuTx.length);RtuTx.before=igw_rd(&Rtu,IGW_TX_COUNT);
        s=igw_rd(&Rtu,IGW_STATUS);
        if ((s&IGW_TX_BUSY) || !(s&IGW_QUIET)) return 0;
        igw_wr(&Rtu,IGW_FEED,0x4b49434bU);igw_wr(&Rtu,IGW_CONTROL,1);
        RtuTx.feed=now;igw_wr(&Rtu,IGW_TX_KICK,1);RtuTx.phase=2;
    } else if (RtuTx.phase==2 && now-RtuTx.feed>=Rtu.feed_interval_us) {
        igw_wr(&Rtu,IGW_FEED,0x4b49434bU);RtuTx.feed=now;
    }
    return 0;
}
static AL_BOOL RtuClean(void)
{
    igw_counters c=igw_counter_delta(igw_get_counters(&Rtu),RtuBaseline);
    return !(RtuStats.timeouts||RtuStats.tx_fail||RtuStats.exceptions||RtuStats.mismatches||RtuStats.sw_bad||
        c.crc||c.uart||c.gap||c.overrun||c.watchdog);
}
static void RtuSummary(void)
{
    igw_counters c=igw_counter_delta(igw_get_counters(&Rtu),RtuBaseline);
    IGW_LOG("[RS485] attempts=%u ok=%u timeout=%u tx_fail=%u exception=%u mismatch=%u sw_bad=%u\r\n",
        RtuStats.attempts,RtuStats.ok,RtuStats.timeouts,RtuStats.tx_fail,RtuStats.exceptions,RtuStats.mismatches,RtuStats.sw_bad);
    IGW_LOG("[PL-RS485] good=%u tx=%u crc=%u uart=%u gap=%u overrun=%u wd=%u\r\n",
        c.good,c.tx,c.crc,c.uart,c.gap,c.overrun,c.watchdog);
    if (IGW_CAN_MASTER && RtuStats.ok)IGW_LOG("[RS485-RTT] min=%u avg=%u max=%u us\r\n",
        (unsigned)RtuMin,(unsigned)(RtuSum/RtuStats.ok),(unsigned)RtuMax);
}
