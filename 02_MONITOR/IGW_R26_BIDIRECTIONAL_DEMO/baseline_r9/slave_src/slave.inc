static uint16_t RtuRegisters[32]={0x1234,1,0,0,IGW_RW_REVISION,0,0xffff,0,IGW_CAN_RW_REVISION};
static struct igw_can_write_cache CanWriteCache;
static struct {AL_BOOL pending;AL_CAN_FrameStruct frame;AL_U64 started;} CanReply;
static void CanTick(void)
{
    if(CanReply.pending){
        AL_S32 ret=AlCan_Hal_SendFrame(Can,&CanReply.frame);
        if(ret==AL_OK){++Stats.tx;++Stats.ok;CanReply.pending=AL_FALSE;}
        else if((ret!=AL_CAN_ERR_TP_BUSY && ret!=AL_CAN_ERR_IN_TS_FULL && ret!=AL_CAN_ERR_BUSY) ||
                Now()-CanReply.started>=Budget(WAIT_MS)){++Stats.tx_fail;CanReply.pending=AL_FALSE;}
    }
    if(CanReply.pending)return;
    for(unsigned i=0;i<4;++i){
        AL_CAN_FrameStruct request;AL_S32 ret=Receive(&request);
        if(ret==AL_CAN_ERR_RECV_EMPTY)break;
        if(ret!=AL_OK)break;
        if(request.Id==IGW_CAN_RW_REQUEST_ID) {
            if(request.DataLen!=AL_CAN_LEN_8 || request.IsIdExt || request.IsRemote ||
               request.IsBitSwitch || request.Koer!=AL_CAN_KOER_NONE) {++Stats.bad_request;continue;}
            uint8_t p[8],out[8];
            for(unsigned b=0;b<8;b++)p[b]=(uint8_t)(request.Data[b/4]>>(8*(b%4)));
            RtuRegisters[3]=(uint16_t)(Microseconds(Now()-BootTick)/1000000ULL);
            igw_can_rw_reply(&CanWriteCache,RtuRegisters,p,out,Microseconds(Now()));
            memset(&CanReply.frame,0,sizeof CanReply.frame);
            CanReply.frame.BufferMode=AL_CAN_BUFFER_MODE_PTB;
            CanReply.frame.Id=IGW_CAN_RW_REPLY_ID;CanReply.frame.DataLen=AL_CAN_LEN_8;
            for(unsigned b=0;b<8;b++)CanReply.frame.Data[b/4]|=(AL_U32)out[b]<<(8*(b%4));
            ++Stats.attempts;CanReply.pending=AL_TRUE;CanReply.started=Now();break;
        }
        if(request.Id!=REQUEST_ID){++Stats.ignored;continue;}
        if(!Valid(&request,REQUEST_TAG)){++Stats.bad_request;continue;}
        ++Stats.attempts;Frame(&CanReply.frame,REPLY_ID,request.Data[0],REPLY_TAG);
        CanReply.pending=AL_TRUE;CanReply.started=Now();break;
    }
}
static void RtuTick(void)
{
    if(RtuTx.phase){int done=RtuTxTick();if(done<0)++RtuStats.tx_fail;else if(done>0)++RtuStats.ok;return;}
    uint8_t rx[256],reply[256];size_t n=0;uint64_t ticks=0;
    int got=igw_take_frame(&Rtu,rx,&n,&ticks);
    if(got<0){++RtuStats.sw_bad;return;}if(got==0)return;
    if(rx[0]==1)++RtuStats.attempts;
    RtuRegisters[2]=(uint16_t)RtuStats.attempts;
    RtuRegisters[3]=(uint16_t)(Microseconds(Now()-BootTick)/1000000ULL);
    size_t length=igw_slave_rw_reply(rx,n,1,RtuRegisters,reply);
    if(length && RtuTxBegin(reply,length)!=IGW_OK)++RtuStats.tx_fail;
}
static void RunDual(void)
{
    IGW_LOG("[DUAL-SLAVE] READY CAN0 500k request=0x201 reply=0x181; RS485 9600 8E1 unit=1 FC03+FC06 RW_R9 shared registers16..31 volatile; CAN RW request=0x211 reply=0x191\r\n");
    AL_U64 last_log=Now();
    for(;;){
        CanTick();RtuTick();
        if(Now()-last_log>=Budget(5000) && !CanReply.pending && !RtuTx.phase){Summary();RtuSummary();
            IGW_LOG("[DUAL-SLAVE] %s; CAN ok is reply-submit, check master/PC\r\n",(Clean()&&RtuClean())?"CLEAN":"REVIEW");last_log=Now();}
#ifdef IGW_HOST_TEST
        if(Stats.ok>=100 && RtuStats.ok>=100)break;
#endif
    }
}
