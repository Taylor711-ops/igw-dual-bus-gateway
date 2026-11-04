/* Uses the R23 persistent SD file banks: CAN and RS485 read/write the same committed
 * file. Legacy R18 64KiB CAN slots remain independent for compatibility. */
static struct cbf_store CanBlocks[IGW_NODE_COUNT];
static unsigned CanBlockNext;
static AL_U64 CanBlockPendingSince;
static void CanBlockInit(void){
 memset(CanBlocks,0,sizeof CanBlocks);CanBlockNext=0;CanBlockPendingSince=0;
 for(unsigned i=0;i<IGW_NODE_COUNT;i++)CanBlocks[i].unit=IGW_FIRST_NODE+i;
 IGW_LOG("[FILE-R23] classic CAN block192 credit4 max=%u shared CAN/RS485 bytes/node; control=0x680+unit reply=0x6a0+unit data=0x700+unit reply_data=0x720+unit\r\n",RSF_MAX);
}
static int CanBlockPending(void){for(unsigned i=0;i<IGW_NODE_COUNT;i++)if(cbf_peek(CanBlocks+i))return 1;return 0;}
static int CanBlockTxTick(void){
 for(unsigned b=0;b<IGW_NODE_COUNT;b++){
  unsigned i=CanBlockNext;CanBlockNext=(CanBlockNext+1)%IGW_NODE_COUNT;const struct cbf_frame *f=cbf_peek(CanBlocks+i);if(!f)continue;
  if(!CanBlockPendingSince)CanBlockPendingSince=Now();
  AL_CAN_FrameStruct tx;memset(&tx,0,sizeof tx);tx.BufferMode=AL_CAN_BUFFER_MODE_PTB;tx.Id=f->id;tx.DataLen=AL_CAN_LEN_8;
  for(unsigned k=0;k<8;k++)tx.Data[k/4]|=(AL_U32)f->data[k]<<(8*(k%4));
  AL_S32 ret=AlCan_Hal_SendFrame(Can,&tx);
  if(ret==AL_OK){cbf_sent(CanBlocks+i);++Stats.tx;CanBlockPendingSince=0;}
  else if((ret!=AL_CAN_ERR_TP_BUSY&&ret!=AL_CAN_ERR_IN_TS_FULL&&ret!=AL_CAN_ERR_BUSY)||Now()-CanBlockPendingSince>=Budget(WAIT_MS)){
   ++Stats.tx_fail;CanBlocks[i].q_head=CanBlocks[i].q_count;CanBlockPendingSince=0;
  }
  return 1;
 }
 return 0;
}
static int CanBlockReceive(const AL_CAN_FrameStruct *f){
 unsigned data=0,unit=0;
 if(f->Id>CBF_CTL&&f->Id<=CBF_CTL+16)unit=f->Id-CBF_CTL;
 else if(f->Id>CBF_DATA&&f->Id<=CBF_DATA+16){unit=f->Id-CBF_DATA;data=1;}
 else return 0;
 int i=igw_node_index(unit);if(i<0)return 1;
 if(f->DataLen!=AL_CAN_LEN_8||f->IsIdExt||f->IsRemote||f->IsBitSwitch||f->Koer!=AL_CAN_KOER_NONE)return 1;
 uint8_t p[8];for(unsigned k=0;k<8;k++)p[k]=(uint8_t)(f->Data[k/4]>>(8*(k%4)));
 cbf_receive(CanBlocks+i,RsFiles+i,data,p,Microseconds(Now()-BootTick)/1000);return 1;
}
