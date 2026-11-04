/* R23 shared persistent SD files. Bounded static workspace, unchanged UART lease. */
_Static_assert(sizeof(struct rsf_store)*IGW_NODE_COUNT<16384u,"unexpected protocol state size");
static uint64_t sd_now_ms(void){return Microseconds(Now()-BootTick)/1000;}
static struct rsf_store RsFiles[IGW_NODE_COUNT];
static struct {unsigned baud,target;uint64_t changed_after,last_frame;uint64_t token;unsigned unit;} RsFileBus;
static void RsFileInit(void){
 memset(&RsFileBus,0,sizeof RsFileBus);RsFileBus.baud=9600;
 int storage=sd_storage_init();
 for(unsigned i=0;i<IGW_NODE_COUNT;i++){memset(RsFiles+i,0,sizeof RsFiles[i]);RsFiles[i].unit=IGW_FIRST_NODE+i;RsFiles[i].bank=SdBanks+i;rsf_restore(RsFiles+i);}
 IGW_LOG("[SD-R23] MOUNT=%s drive=1: directory=IGW23 FatFs=86631 max=%u last_fresult=%u reason=%s\r\n",storage?"FAIL":"PASS",RSF_MAX,(unsigned)SdLast,SdReason);
 for(unsigned i=0;i<IGW_NODE_COUNT;i++)IGW_LOG("[SD-R23] unit=%u valid=%u bytes=%u generation=%llu slot=%c\r\n",RsFiles[i].unit,SdBanks[i].valid,SdBanks[i].size,(unsigned long long)SdBanks[i].generation,SdBanks[i].slot?'B':'A');
 IGW_LOG("[FILE-R24-EXPERIMENT] shared SD file banks; block=192 baud=9600/115200/230400; no RAM fallback; storage=%s\r\n",SdReady?"READY":"UNAVAILABLE");
}
static void RsFileTick(void){
 uint64_t now=Microseconds(Now()-BootTick)/1000;
 for(unsigned i=0;i<IGW_NODE_COUNT;i++)rsf_expire(RsFiles+i,now);
 if(RsFileBus.baud!=9600&&now-RsFileBus.last_frame>=RSF_IDLE_MS){RsFileBus.target=9600;RsFileBus.changed_after=now;}
 if(RsFileBus.target&&now>=RsFileBus.changed_after&&!RtuTx.phase&&!(igw_rd(&Rtu,IGW_STATUS)&IGW_TX_BUSY)&&(igw_rd(&Rtu,IGW_STATUS)&IGW_QUIET)){
  unsigned target=RsFileBus.target;
  if(igw_configure(&Rtu,target)==IGW_OK){RsFileBus.baud=target;RsFileBus.target=0;RsFileBus.last_frame=now;}
 }
}
static size_t RsFileReceive(const uint8_t *p,size_t n,uint8_t out[256]){
 struct rsf_packet q;if(!rsf_parse(p,n,0,&q))return 0;
 int i=igw_node_index(q.unit);if(i<0)return 0;
 uint64_t now=Microseconds(Now()-BootTick)/1000;
 if(RsFileBus.target)return 0; /* no traffic during the explicit switch guard */
 if(q.op==RSF_SPEED){
  unsigned error=RSF_OK;
  if(q.status||q.length||q.count||(q.offset!=9600&&q.offset!=115200&&q.offset!=230400))error=RSF_BAD;
  for(unsigned k=0;k<IGW_NODE_COUNT;k++){rsf_expire(RsFiles+k,now);if(rsf_active(RsFiles+k))error=RSF_BUSY;}
  if(!error){RsFileBus.target=q.offset;RsFileBus.changed_after=now+100;RsFileBus.token=q.token;RsFileBus.unit=q.unit;RsFileBus.last_frame=now;}
  return rsf_frame(out,q.unit,1,q.op,error,q.token,q.offset,0,NULL,0);
 }
 size_t reply=rsf_handle(RsFiles+i,p,n,out,now);
 if(reply)RsFileBus.last_frame=now;
 return reply;
}
