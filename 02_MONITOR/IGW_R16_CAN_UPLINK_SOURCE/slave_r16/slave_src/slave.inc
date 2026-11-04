static struct igw_nodes Nodes;
static struct {AL_BOOL pending;AL_CAN_FrameStruct frame;AL_U64 started;} CanReply;
static struct ul_client Uplink[IGW_NODE_COUNT];
static unsigned UplinkNext;
static uint64_t UplinkMs(void){return Microseconds(Now()-BootTick)/1000;}
static uint16_t UplinkSource(unsigned i,uint64_t now){
    uint16_t value=(uint16_t)((IGW_FIRST_NODE+i)*1000u+now/1000);
    Nodes.regs[i][11]=value;return value; /* generated device-local test counter */
}
static void UplinkInit(void){
    UplinkNext=0;uint64_t now=UplinkMs();
    for(unsigned i=0;i<IGW_NODE_COUNT;i++)ul_client_init(Uplink+i,IGW_FIRST_NODE+i,now);
}
static int UplinkReceive(const AL_CAN_FrameStruct *f){
    unsigned unit=f->Id>UL_WELCOME&&f->Id<=UL_WELCOME+16?f->Id-UL_WELCOME:
        f->Id>UL_ACK&&f->Id<=UL_ACK+16?f->Id-UL_ACK:0;
    int i=igw_node_index(unit);if(i<0)return 0;
    if(f->DataLen!=AL_CAN_LEN_8||f->IsIdExt||f->IsRemote||f->IsBitSwitch||f->Koer!=AL_CAN_KOER_NONE)return 1;
    struct ul_frame frame;frame.id=f->Id;
    for(unsigned b=0;b<8;b++)frame.data[b]=(uint8_t)(f->Data[b/4]>>(8*(b%4)));
    uint64_t now=UplinkMs();ul_client_receive(Uplink+i,&frame,now,UplinkSource((unsigned)i,now));return 1;
}
static void UplinkTick(void){
    if(CanReply.pending)return; /* existing command responses have priority */
    uint64_t now=UplinkMs();
    for(unsigned budget=0;budget<IGW_NODE_COUNT;budget++){
        unsigned i=UplinkNext;UplinkNext=(UplinkNext+1)%IGW_NODE_COUNT;
        struct ul_client candidate=Uplink[i];struct ul_frame frame;
        if(!ul_client_tick(&candidate,now,UplinkSource(i,now),&frame)){Uplink[i]=candidate;continue;}
        AL_CAN_FrameStruct tx;memset(&tx,0,sizeof tx);tx.BufferMode=AL_CAN_BUFFER_MODE_PTB;
        tx.Id=frame.id;tx.DataLen=AL_CAN_LEN_8;
        for(unsigned b=0;b<8;b++)tx.Data[b/4]|=(AL_U32)frame.data[b]<<(8*(b%4));
        if(AlCan_Hal_SendFrame(Can,&tx)==AL_OK)Uplink[i]=candidate;
        return; /* bounded, no UART/CAN busy wait */
    }
}
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
        if(UplinkReceive(&request))continue;
        if(request.Id>=0x211 && request.Id<=0x220 && igw_node_index(request.Id-0x210)>=0) {
            if(request.DataLen!=AL_CAN_LEN_8 || request.IsIdExt || request.IsRemote ||
               request.IsBitSwitch || request.Koer!=AL_CAN_KOER_NONE) {++Stats.bad_request;continue;}
            uint8_t p[8],out[8];
            for(unsigned b=0;b<8;b++)p[b]=(uint8_t)(request.Data[b/4]>>(8*(b%4)));
            int reply_id=igw_nodes_can(&Nodes,request.Id,p,out,Microseconds(Now()-BootTick));
            memset(&CanReply.frame,0,sizeof CanReply.frame);
            CanReply.frame.BufferMode=AL_CAN_BUFFER_MODE_PTB;
            CanReply.frame.Id=(AL_U32)reply_id;CanReply.frame.DataLen=AL_CAN_LEN_8;
            for(unsigned b=0;b<8;b++)CanReply.frame.Data[b/4]|=(AL_U32)out[b]<<(8*(b%4));
            ++Stats.attempts;CanReply.pending=AL_TRUE;CanReply.started=Now();break;
        }
        if(request.Id!=REQUEST_ID || igw_node_index(1)<0){++Stats.ignored;continue;}
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
    if(n && igw_node_index(rx[0])>=0)++RtuStats.attempts;
    size_t length=igw_nodes_rtu(&Nodes,rx,n,reply,Microseconds(Now()-BootTick));
    if(length && RtuTxBegin(reply,length)!=IGW_OK)++RtuStats.tx_fail;
}
static void RunDual(void)
{
    IGW_LOG("[DUAL-SLAVE] R16 READY first_node=%u nodes=%u; independent banks; CAN request=0x210+unit reply=0x190+unit; RS485 FC03+FC06 9600 8E1\r\n",IGW_FIRST_NODE,IGW_NODE_COUNT);
    IGW_LOG("[UPLINK] R16 autonomous CAN state writes to host R16 per node; generated test counters, 1s period, ACK/retry; RS485 remains polled\r\n");
    AL_U64 last_log=Now();
    for(;;){
        CanTick();RtuTick();UplinkTick();
        if(Now()-last_log>=Budget(5000) && !CanReply.pending && !RtuTx.phase){Summary();RtuSummary();
            for(unsigned i=0;i<IGW_NODE_COUNT;i++)IGW_LOG("[UPLINK] unit=%u acked=%llu retries=%llu unknown=%llu reconnects=%llu\r\n",IGW_FIRST_NODE+i,(unsigned long long)Uplink[i].acked,(unsigned long long)Uplink[i].retries,(unsigned long long)Uplink[i].unknown,(unsigned long long)Uplink[i].reconnects);
            IGW_LOG("[DUAL-SLAVE] %s; CAN ok is reply-submit, check master/PC\r\n",(Clean()&&RtuClean())?"CLEAN":"REVIEW");last_log=Now();}
#ifdef IGW_HOST_TEST
        if(Stats.ok>=100 && RtuStats.ok>=100)break;
#endif
    }
}
