static struct igw_nodes Nodes;
static struct {AL_BOOL pending;AL_CAN_FrameStruct frame;AL_U64 started;} CanReply;
static struct ul_client Uplink[IGW_NODE_COUNT];
static unsigned UplinkNext,RsUplinkNext;
static struct rsu_client RsUplink[IGW_NODE_COUNT];
static unsigned RsUplinkTx;
static uint64_t UplinkMs(void){return Microseconds(Now()-BootTick)/1000;}
static uint16_t UplinkSource(unsigned i,uint64_t now){
    uint16_t value=(uint16_t)((IGW_FIRST_NODE+i)*1000u+now/1000);
    Nodes.regs[i][11]=value;return value; /* generated device-local test counter */
}
static void UplinkInit(void){
    UplinkNext=RsUplinkNext=RsUplinkTx=0;uint64_t now=UplinkMs();
    for(unsigned i=0;i<IGW_NODE_COUNT;i++){ul_client_init(Uplink+i,IGW_FIRST_NODE+i,now);rsu_init(RsUplink+i,IGW_FIRST_NODE+i);}
}
static int UplinkReceive(const AL_CAN_FrameStruct *f){
    unsigned unit=f->Id>UL_WELCOME&&f->Id<=UL_WELCOME+16?f->Id-UL_WELCOME:
        f->Id>UL_ACK&&f->Id<=UL_ACK+16?f->Id-UL_ACK:
        f->Id>RSU_GRANT&&f->Id<=RSU_GRANT+16?f->Id-RSU_GRANT:
        f->Id>RSU_ACK&&f->Id<=RSU_ACK+16?f->Id-RSU_ACK:
        f->Id>RSU_RELEASED&&f->Id<=RSU_RELEASED+16?f->Id-RSU_RELEASED:
        f->Id>RSU_CANCEL&&f->Id<=RSU_CANCEL+16?f->Id-RSU_CANCEL:0;
    int i=igw_node_index(unit);if(i<0)return 0;
    if(f->DataLen!=AL_CAN_LEN_8||f->IsIdExt||f->IsRemote||f->IsBitSwitch||f->Koer!=AL_CAN_KOER_NONE)return 1;
    struct ul_frame frame;frame.id=f->Id;
    for(unsigned b=0;b<8;b++)frame.data[b]=(uint8_t)(f->Data[b/4]>>(8*(b%4)));
    uint64_t now=UplinkMs();if(f->Id>RSU_GRANT){rsu_receive(RsUplink+i,&frame,now);return 1;}
    ul_client_receive(Uplink+i,&frame,now,UplinkSource((unsigned)i,now));return 1;
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
static void RsUplinkTick(void){
    uint64_t now=UplinkMs();
    for(unsigned b=0;b<IGW_NODE_COUNT;b++){
        unsigned i=RsUplinkNext;RsUplinkNext=(RsUplinkNext+1)%IGW_NODE_COUNT;
        struct rsu_client *c=RsUplink+i;uint8_t frame[RSU_BYTES];
        if(c->cancelled&&RsUplinkTx==i+1){
            igw_wr(&Rtu,IGW_CONTROL,0);
            if(RtuTx.phase==1){RtuTx.phase=0;RsUplinkTx=0;}
        }
        if(!RtuTx.phase && !(igw_rd(&Rtu,IGW_STATUS)&IGW_RX_READY) && rsu_prepare(c,now,frame)){
            if(RtuTxBegin(frame,sizeof frame)==IGW_OK){
                /* Absolute deadlines retain the original grant receipt time. */
                AL_U64 origin=BoardUs(NULL)-(now-c->grant_ms)*1000ULL;
                RtuTx.latest_start=origin+RSU_START_MS*1000ULL;
                RtuTx.latest_finish=origin+RSU_TX_LIMIT_MS*1000ULL;RsUplinkTx=i+1;
            }
        }
        if(CanReply.pending)continue;
        if(c->seq==65535u&&!c->pending)ul_connect(Uplink+i,now);
        struct rsu_client candidate=*c;struct ul_frame f;
        uint32_t epoch=Uplink[i].phase==UL_CONNECT?0:Uplink[i].epoch;
        uint16_t source=(uint16_t)((IGW_FIRST_NODE+i)*1000u+now/1000);
        Nodes.regs[i][12]=source;
        int release=0;
        if(!RtuTx.phase && !(igw_rd(&Rtu,IGW_STATUS)&IGW_TX_BUSY) && (igw_rd(&Rtu,IGW_STATUS)&IGW_QUIET))release=rsu_release_tick(&candidate,now,&f);
        if(!release&&!rsu_tick(&candidate,now,epoch,source,&f)){*c=candidate;continue;}
        AL_CAN_FrameStruct tx;memset(&tx,0,sizeof tx);tx.BufferMode=AL_CAN_BUFFER_MODE_PTB;
        tx.Id=f.id;tx.DataLen=AL_CAN_LEN_8;
        for(unsigned k=0;k<8;k++)tx.Data[k/4]|=(AL_U32)f.data[k]<<(8*(k%4));
        if(AlCan_Hal_SendFrame(Can,&tx)==AL_OK)*c=candidate;
        return;
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
    if(RtuTx.phase){int done=RtuTxTick();
        if(RsUplinkTx){if(done){rsu_done(RsUplink+RsUplinkTx-1,UplinkMs());RsUplinkTx=0;}} /* Separate upload statistics, no false Modbus completions. */
        else if(done<0)++RtuStats.tx_fail;else if(done>0)++RtuStats.ok;
        return;}
    uint8_t rx[256],reply[256];size_t n=0;uint64_t ticks=0;
    int got=igw_take_frame(&Rtu,rx,&n,&ticks);
    if(got<0){++RtuStats.sw_bad;return;}if(got==0)return;
    if(n && igw_node_index(rx[0])>=0)++RtuStats.attempts;
    size_t length=igw_nodes_rtu(&Nodes,rx,n,reply,Microseconds(Now()-BootTick));
    if(length && RtuTxBegin(reply,length)!=IGW_OK)++RtuStats.tx_fail;
}
/* R17.2: sampling remains active; synchronous batch UART output is opt-in.
 * No wire protocol, bus ownership, timeout or device register changes. */
#ifndef IGW_RUNTIME_LOGGING
#define IGW_RUNTIME_LOGGING 0
#endif
#if IGW_RUNTIME_LOGGING != 0 && IGW_RUNTIME_LOGGING != 1
#error "IGW_RUNTIME_LOGGING must be 0 or 1"
#endif
static void RuntimeTelemetryStep(AL_U64 *last_log)
{
    if(Now()-*last_log<Budget(5000) || CanReply.pending || RtuTx.phase)return;
    if(IGW_RUNTIME_LOGGING){Summary();RtuSummary();
            for(unsigned i=0;i<IGW_NODE_COUNT;i++)IGW_LOG("[UPLINK] unit=%u acked=%llu retries=%llu unknown=%llu reconnects=%llu\r\n",IGW_FIRST_NODE+i,(unsigned long long)Uplink[i].acked,(unsigned long long)Uplink[i].retries,(unsigned long long)Uplink[i].unknown,(unsigned long long)Uplink[i].reconnects);
            for(unsigned i=0;i<IGW_NODE_COUNT;i++)IGW_LOG("[RS-UPLINK] unit=%u acked=%llu retries=%llu unknown=%llu expired=%llu\r\n",IGW_FIRST_NODE+i,(unsigned long long)RsUplink[i].accepted,(unsigned long long)RsUplink[i].retries,(unsigned long long)RsUplink[i].unknown,(unsigned long long)RsUplink[i].expired);
            IGW_LOG("[DUAL-SLAVE] %s; CAN ok is reply-submit, check master/PC\r\n",(Clean()&&RtuClean())?"CLEAN":"REVIEW");
    }else{
        Snapshot(AL_FALSE); /* Preserve REC/TEC/max/error accumulation without UART. */
    }
    *last_log=Now();
}
static void RunDual(void)
{
    IGW_LOG("[DUAL-SLAVE] R17.2 READY first_node=%u nodes=%u; independent banks; CAN request=0x210+unit reply=0x190+unit; RS485 FC03+FC06 9600 8E1\r\n",IGW_FIRST_NODE,IGW_NODE_COUNT);
    IGW_LOG("[UPLINK] R17 CAN host R16; RS485 host R17 with CAN RTS/GRANT/ACK, 2s generated counters; no unsolicited RS485 without lease\r\n");
    IGW_LOG("[RUN-LOG] R17.2 periodic_uart=%u; wire_protocol=R17; startup logs retained\r\n",(unsigned)IGW_RUNTIME_LOGGING);
    AL_U64 last_log=Now();
    for(;;){
        CanTick();RtuTick();UplinkTick();RsUplinkTick();
        RuntimeTelemetryStep(&last_log);
#ifdef IGW_HOST_TEST
        if(Stats.ok>=100 && RtuStats.ok>=100)break;
#endif
    }
}
