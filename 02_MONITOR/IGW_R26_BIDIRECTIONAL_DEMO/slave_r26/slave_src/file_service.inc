/* Separate RAM slot per logical node. No filesystem, flash or actuator writes. */
static struct igwf_store FileSlots[IGW_NODE_COUNT];
static void FileInit(void){
    for(unsigned i=0;i<IGW_NODE_COUNT;i++)igwf_init(FileSlots+i,IGW_FIRST_NODE+i);
    IGW_LOG("[FILE-R18] legacy RAM compatibility only; classic CAN request=0x600+unit reply=0x620+unit; max=65536 bytes per node; RAM volatile; SHA256 commit\r\n");
}
static int FileReceive(const AL_CAN_FrameStruct *request,AL_CAN_FrameStruct *reply){
    if(request->Id<=IGWF_REQ||request->Id>IGWF_REQ+16)return 0;
    unsigned unit=request->Id-IGWF_REQ;int i=(int)unit-IGW_FIRST_NODE;
    if(i<0||(unsigned)i>=IGW_NODE_COUNT||request->DataLen!=AL_CAN_LEN_8||request->IsIdExt||request->IsRemote||request->IsBitSwitch||request->Koer!=AL_CAN_KOER_NONE)return 0;
    uint8_t in[8],out[8];for(unsigned k=0;k<8;k++)in[k]=(uint8_t)(request->Data[k/4]>>(8*(k%4)));
    igwf_handle(FileSlots+i,in,out,Microseconds(Now()-BootTick)/1000);
    memset(reply,0,sizeof *reply);reply->BufferMode=AL_CAN_BUFFER_MODE_PTB;
    reply->Id=IGWF_REPLY+unit;reply->DataLen=AL_CAN_LEN_8;
    for(unsigned k=0;k<8;k++)reply->Data[k/4]|=(AL_U32)out[k]<<(8*(k%4));
    return 1;
}
