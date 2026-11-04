AL_S32 main(AL_VOID)
{
    memset(&Stats,0,sizeof Stats);memset(&RtuStats,0,sizeof RtuStats);memset(&RtuTx,0,sizeof RtuTx);
    AlLog_SetLevel(AL_LOG_LEVEL_NOTICE);Can=AL_NULL;
    IGW_LOG("\r\n[IGW-DUAL] gateway UPLINK_R17_2 role=%s CAN(v1.2)+RS485+UDP\r\n",IGW_CAN_MASTER?"MASTER":"SLAVE");
    IGW_LOG("[PLATFORM] HPF=%s BIT_METADATA=%s\r\n",HPF_EXPORT_TIME,BITSTREAM_GENERATE_TIME);
    AL_S32 ret=Setup();if(ret!=AL_OK)goto stop;
    BootTick=Now();for(unsigned i=0;i<100;++i)(void)Now();
    if(Now()==BootTick){++Stats.timer_error;ret=(AL_S32)AL_CAN_ERR_ILLEGAL_PARAM;goto stop;}
    memset(&Rtu,0,sizeof Rtu);Rtu.read=BoardRead;Rtu.write=BoardWrite;Rtu.now_us=BoardUs;
    IGW_LOG("[PL-RS485] magic=0x%08x version=0x%08x clock=%u Hz\r\n",
        igw_rd(&Rtu,IGW_MAGIC),igw_rd(&Rtu,IGW_VERSION),igw_rd(&Rtu,IGW_CLOCK));
    ret=(AL_S32)igw_configure(&Rtu,9600);if(ret!=IGW_OK)goto stop;
    RtuBaseline=igw_get_counters(&Rtu);
    IGW_LOG("[RS485-CONFIG] 9600 8E1 multi-node base=0x80100000 div=%u\r\n",igw_rd(&Rtu,IGW_DIV));
#if IGW_CAN_MASTER
    memset(&Net,0,sizeof Net);memset(&CanJob,0,sizeof CanJob);memset(&RtuJob,0,sizeof RtuJob);PollEnabled=AL_FALSE;
    Udp=AL_NULL;ret=NetworkInit();if(ret!=AL_OK)goto stop;
#else
    memset(&CanReply,0,sizeof CanReply);igw_nodes_init(&Nodes);UplinkInit();
#endif
    RunDual();return AL_OK;
stop:
    IGW_LOG("[FATAL] dual init ret=0x%08x; no polling started\r\n",(AL_U32)ret);
    if(Can)AlCan_ll_SetCanCtrlReset(Can->Dev.BaseAddr,AL_TRUE);
#ifndef IGW_HOST_TEST
    for(;;)AlSys_MDelay(1000);
#endif
    return ret;
}
