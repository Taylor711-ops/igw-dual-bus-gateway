static void R21Inject(unsigned data,const uint8_t p[8]){
 memset(&Pending,0,sizeof Pending);Pending.Id=(data?CBF_DATA:CBF_CTL)+1;Pending.DataLen=AL_CAN_LEN_8;
 for(unsigned i=0;i<8;i++){Pending.Data[i/4]|=(AL_U32)p[i]<<(8*(i%4));}HasPending=1;R21Count=0;
 for(unsigned i=0;i<200&&(HasPending||CanBlockPending());i++)CanTick();
 assert(!HasPending&&!CanBlockPending());
}
static struct rsf_packet R21Packet(unsigned op,uint64_t token,uint32_t offset,unsigned count,const uint8_t *p,unsigned n){
 static uint8_t packet[256],output[256];static uint32_t msg=900;uint8_t wire[8];size_t length=rsf_frame(packet,1,0,op,0,token,offset,count,p,n);assert(length);msg++;
 memset(wire,0,8);wire[0]=CBF_BEGIN;rsf_p32(wire+1,msg);wire[5]=(uint8_t)length;wire[6]=packet[length-2];wire[7]=packet[length-1];R21Inject(0,wire);assert(R21Count==1);
 for(unsigned i=0;i<cbf_fragments((unsigned)length);i++){cbf_data(wire,msg,i,packet,(unsigned)length);R21Inject(1,wire);}
 assert(R21Count==1&&CanBlocks[0].cached);
 cbf_control(wire,CBF_PULL,msg,65535,0);R21Inject(0,wire);assert(R21Count==1);
 uint8_t meta[8];for(unsigned k=0;k<8;k++)meta[k]=(uint8_t)(R21Wire[0].Data[k/4]>>(8*(k%4)));assert(meta[0]==CBF_META);unsigned size=rsf_u16(meta+5);
 for(unsigned i=0;i<cbf_fragments(size);i+=CBF_WINDOW){
  cbf_control(wire,CBF_PULL,msg,i,0);R21Inject(0,wire);assert(R21Count>=2&&R21Count<=5);
  for(unsigned j=0;j<R21Count-1;j++){uint8_t bytes[8];for(unsigned k=0;k<8;k++)bytes[k]=(uint8_t)(R21Wire[j].Data[k/4]>>(8*(k%4)));assert(R21Wire[j].Id==CBF_RDATA+1);unsigned start=bytes[2]*CBF_PAYLOAD,len=size-start;if(len>CBF_PAYLOAD)len=CBF_PAYLOAD;memcpy(output+start,bytes+3,len);}
 }
 struct rsf_packet reply;assert(rsf_parse(output,size,1,&reply));assert(!reply.status);return reply;
}
static void TestR21(void){
 Fixture(0,0);assert(Setup()==AL_OK);BootTick=Now();memset(&RtuStats,0,sizeof RtuStats);memset(&RtuTx,0,sizeof RtuTx);memset(&CanReply,0,sizeof CanReply);
 Rtu.read=BoardRead;Rtu.write=BoardWrite;Rtu.now_us=BoardUs;assert(!igw_configure(&Rtu,9600));RsFileInit();CanBlockInit();R21Capture=1;
 uint8_t bytes[4096],sha[32];for(unsigned i=0;i<sizeof bytes;i++)bytes[i]=(uint8_t)(i*23+(i>>8));igwf_digest(bytes,sizeof bytes,sha);
 R21Packet(RSF_PUT,200,4096,0,sha,32);
 for(unsigned i=0;i<4096;){unsigned n=4096-i;if(n>RSF_BLOCK)n=RSF_BLOCK;R21Packet(RSF_WRITE,200,i,n,bytes+i,n);i+=n;}
 R21Packet(RSF_COMMIT,200,0,0,NULL,0);R21Packet(RSF_CLOSE,200,0,0,NULL,0);assert(RsFiles[0].valid&&!RsFiles[1].valid);
 assert(!R20Call(RSF_GET,201,0,0,NULL,0).status);
 for(unsigned i=0;i<4096;){unsigned n=4096-i;if(n>RSF_BLOCK)n=RSF_BLOCK;struct rsf_packet a=R20Call(RSF_READ,201,i,n,NULL,0);assert(!a.status&&!memcmp(a.payload,bytes+i,n));i+=n;}
 assert(!R20Call(RSF_CLOSE,201,0,0,NULL,0).status);
 for(unsigned i=0;i<4096;i++){bytes[i]^=0xa5;}igwf_digest(bytes,sizeof bytes,sha);
 assert(!R20Call(RSF_PUT,202,4096,0,sha,32).status);
 for(unsigned i=0;i<4096;){unsigned n=4096-i;if(n>RSF_BLOCK)n=RSF_BLOCK;assert(!R20Call(RSF_WRITE,202,i,n,bytes+i,n).status);i+=n;}
 assert(!R20Call(RSF_COMMIT,202,0,0,NULL,0).status);assert(!R20Call(RSF_CLOSE,202,0,0,NULL,0).status);
 R21Packet(RSF_GET,203,0,0,NULL,0);
 for(unsigned i=0;i<4096;){unsigned n=4096-i;if(n>RSF_BLOCK)n=RSF_BLOCK;struct rsf_packet a=R21Packet(RSF_READ,203,i,n,NULL,0);assert(!memcmp(a.payload,bytes+i,n));i+=n;}
 R21Packet(RSF_CLOSE,203,0,0,NULL,0);R21Capture=0;
 puts("PASS R21 actual HAL route: credit4 CAN block packets, bounded reply burst, CAN->RS485 and RS485->CAN shared-file readback through real generated scheduler");
}
