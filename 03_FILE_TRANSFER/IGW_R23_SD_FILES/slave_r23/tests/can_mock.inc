static int Scenario;
static AL_U64 Tick;
static AL_CAN_HalStruct Handle;
static AL_CAN_FrameStruct Pending,LastSent;
static unsigned HasPending,ReleaseCalls,SentCalls;
static AL_CAN_FrameStruct R21Wire[8];static unsigned R21Count,R21Capture;
static AL_U32 Lbmi,Lbme,Lom,Stby,Sack,Shot,InReset;
static AL_U32 FilterEnabled,FilterMask[3],FilterCode[3],FilterIndex,FilterSelect;
static AL_BOOL Accept(const AL_CAN_FrameStruct *f){
 for(unsigned i=0;i<3;++i){
  AL_U32 mask=FilterMask[i];
  if(!(FilterEnabled&(1U<<i)))continue;
  if((mask&(1U<<30)) && f->IsIdExt!=((mask>>29)&1U))continue;
  if(!((f->Id^FilterCode[i])&~mask&0x1fffffffU))return AL_TRUE;
 }
 return AL_FALSE;
}
AL_U64 AlSys_GetTimerFreq(void){return 1000000ULL;}
AL_U64 AlSys_GetTimerTickCount(void){Tick+=100;return Tick;}
AL_VOID AlSys_MDelay(AL_U64 ms){Tick+=ms*1000ULL;}
AL_U32 mock_bits(AL_REG addr,AL_U32 shift,AL_U32 size){
 (void)size;
 if(addr==CAN_TOP_CFG_CTRL_ADDR)return 0;
 if(shift==CAN_TECNT_REVNT_TDC_EALCAP_RECNT_SHIFT)return Scenario==8?1:0;
 if(shift==CAN_TECNT_REVNT_TDC_EALCAP_TECNT_SHIFT)return 0;
 if(shift==CAN_RCTRL_TCTRL_TCMD_CFG_STAT_TPSS_SHIFT)return Shot;
 if(shift==CAN_RCTRL_TCTRL_TCMD_CFG_STAT_SACK_SHIFT)return Sack;
 return 0;
}
AL_VOID AlCan_ll_SetLbmi(AL_REG b,AL_BOOL v){(void)b;Lbmi=Scenario==6?1:v;}
AL_VOID AlCan_ll_SetLbme(AL_REG b,AL_BOOL v){(void)b;Lbme=v;}
AL_VOID AlCan_ll_SetLom(AL_REG b,AL_BOOL v){(void)b;Lom=v;}
AL_VOID AlCan_ll_SetStby(AL_REG b,AL_BOOL v){(void)b;Stby=v;}
AL_VOID AlCan_ll_SetSack(AL_REG b,AL_CAN_SackEnum v){(void)b;Sack=Scenario==9?1:(AL_U32)v;}
AL_VOID AlCan_ll_SetTpss(AL_REG b,AL_BOOL v){(void)b;Shot=(Scenario==13 || (Scenario==12&&InReset))?0:v;}
AL_VOID AlCan_ll_SetAcfenx(AL_REG b,AL_U32 index,AL_BOOL v){(void)b;assert(index<3);if(v)FilterEnabled|=1U<<index;else FilterEnabled&=~(1U<<index);}
AL_U32 AlCan_ll_IsAcfen(AL_REG b){(void)b;return FilterEnabled;}
AL_VOID AlCan_ll_SetAcfadr(AL_REG b,AL_U32 index){(void)b;assert(index<3);FilterIndex=index;}
AL_VOID AlCan_ll_SetSelmask(AL_REG b,AL_CAN_SelmaskEnum s){(void)b;FilterSelect=s;}
AL_U32 AlCan_ll_ReadAcodexAmaskx(AL_REG b){(void)b;return FilterSelect==AL_CAN_SELMASK_ACCEPT_MASK?FilterMask[FilterIndex]:FilterCode[FilterIndex];}
AL_S32 AlCan_Dev_SetFilter(AL_CAN_DevStruct *d,AL_CAN_FilterCfgStruct *c){
 assert(d==&Handle.Dev&&InReset&&d->Host.OpsMode==AL_CAN_MODE_CONFIG);
 assert(c->FilterIndex==AL_CAN_FILTER_0&&c->FilterEn&&c->MaskType==AL_CAN_FILTER_MASK_STD);
 if(Scenario==14)return AL_CAN_ERR_ILLEGAL_PARAM;
 AlCan_ll_SetAcfenx(d->BaseAddr,c->FilterIndex,Scenario==15?AL_FALSE:c->FilterEn);
 FilterMask[c->FilterIndex]=c->MaskValue|((AL_U32)c->MaskType<<29);
 FilterCode[c->FilterIndex]=c->IdValue;
 if(Scenario==16)FilterMask[0]^=1U;
 if(Scenario==17)FilterCode[0]=1U;
 return AL_OK;
}
AL_VOID AlCan_ll_SetRrel(AL_REG b,AL_CAN_RrelEnum v){(void)b;assert(v==AL_CAN_RREL_RELEASE);++ReleaseCalls;HasPending=0;}
AL_U32 AlCan_ll_IsLbmiEnabled(AL_REG b){(void)b;return Lbmi;}
AL_U32 AlCan_ll_IsLbmeEnabled(AL_REG b){(void)b;return Lbme;}
AL_U32 AlCan_ll_IsLom(AL_REG b){(void)b;return Lom;}
AL_U32 AlCan_ll_IsStby(AL_REG b){(void)b;return Stby;}
AL_U32 AlCan_ll_GetSseg1(AL_REG b){(void)b;return 62;}
AL_U32 AlCan_ll_GetSseg2(AL_REG b){(void)b;return 15;}
AL_U32 AlCan_ll_GetSsjw(AL_REG b){(void)b;return 15;}
AL_U32 AlCan_ll_GetSpresc(AL_REG b){(void)b;return 1;}
AL_U32 AlCan_ll_GetKoer(AL_REG b){(void)b;return 0;}
AL_U32 AlCan_ll_IsBusOff(AL_REG b){(void)b;return 0;}
AL_VOID AlCan_ll_ClrIntr(AL_REG b,AL_U32 mask){(void)b;assert(mask==AL_CAN_INTR_MASK_EN_ALL);}
AL_VOID AlCan_ll_SetCanCtrlReset(AL_REG b,AL_BOOL v){(void)b;InReset=v;if(Scenario==12)Shot=0;if(Scenario==18&&!v)FilterEnabled=0;}
AL_S32 AlCan_Hal_Init(AL_CAN_HalStruct **out,AL_U32 id,AL_CAN_InitStruct *cfg,AL_VOID *cb){
 assert(id==0&&cb==NULL&&cfg->Protocol==AL_CAN_TYPE_2_0B);
 assert(cfg->SlowBitRate==AL_CAN_ARBITRATION_0_5M);
 if(Scenario==10)return AL_CAN_ERR_NULL_PTR;
 Handle.Dev.BaseAddr=CAN0__BASE_ADDR;Handle.Dev.Host.Protocol=cfg->Protocol;
 Handle.Dev.Host.TxIntrMode=cfg->TxIntrMode;Handle.Dev.Host.RxIntrMode=cfg->RxIntrMode;
 Handle.Dev.Host.InputClk=80000000;Lbmi=1;Lbme=Lom=Stby=Sack=Shot=InReset=0;
 FilterEnabled=1;FilterMask[0]=0x1fffffffU;FilterCode[0]=0;
 *out=&Handle;return AL_OK;
}
AL_S32 AlCan_Hal_SendFrame(AL_CAN_HalStruct *h,AL_CAN_FrameStruct *f){
 assert(h==&Handle&&f->DataLen==AL_CAN_LEN_8&&!Lbmi&&!Lbme&&!Sack&&Shot);
 ++SentCalls;
 if(Scenario==7)return AL_CAN_ERR_TP_BUSY;
 LastSent=*f;if(R21Capture){assert(R21Count<8);R21Wire[R21Count++]=*f;}
 if(IGW_CAN_MASTER){
  assert(f->Id==REQUEST_ID&&Valid(f,REQUEST_TAG));
  Frame(&Pending,REPLY_ID,f->Data[0],REPLY_TAG);HasPending=Scenario!=1;
  if(Scenario==2)Pending.Data[1]^=1;
  if(Scenario==3)Pending.Data[0]=f->Data[0]>1U?f->Data[0]-1U:1000U;
  if(Scenario==3)Pending.Data[1]=REPLY_TAG^Pending.Data[0];
  if(Scenario==4)Pending.Id=REQUEST_ID;
  if(Scenario==11)Pending.IsIdExt=1;
 }
 return AL_OK;
}
AL_S32 AlCan_Hal_RecvFrame(AL_CAN_HalStruct *h,AL_CAN_FrameStruct *f){
 assert(h==&Handle);
 if(!HasPending)return AL_CAN_ERR_RECV_EMPTY;
 if(!Accept(&Pending)){HasPending=0;return AL_CAN_ERR_RECV_EMPTY;}
 if(Scenario==5)return AL_CAN_ERR_RECV;
 *f=Pending;HasPending=0;return AL_OK;
}
static void ResetFixture(int scenario){
 Scenario=scenario;Tick=scenario==0?UINT64_MAX-2000U:0U;
 HasPending=ReleaseCalls=SentCalls=0;memset(&Handle,0,sizeof(Handle));
 memset(&Stats,0,sizeof(Stats));Can=NULL;
 FilterEnabled=FilterIndex=FilterSelect=0;memset(FilterMask,0,sizeof(FilterMask));memset(FilterCode,0,sizeof(FilterCode));
}
