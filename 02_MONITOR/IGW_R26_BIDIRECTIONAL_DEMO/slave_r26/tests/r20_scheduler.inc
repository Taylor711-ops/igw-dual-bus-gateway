static struct rsf_packet R20Call(unsigned op,uint64_t token,uint32_t offset,unsigned count,const uint8_t *p,unsigned n){
 uint8_t frame[256];static uint8_t output[256];size_t length=rsf_frame(frame,1,0,op,0,token,offset,count,p,n);assert(length);uint32_t before=Registers[IGW_TX_COUNT/4];Queue(frame,length);
 for(unsigned i=0;i<7000;i++){RtuTick();if(!RtuTx.phase&&Registers[IGW_TX_COUNT/4]!=before)break;}
 assert(!RtuTx.phase&&Registers[IGW_TX_COUNT/4]==before+1&&Registers[IGW_CONTROL/4]==0);
 length=Registers[IGW_TX_LEN/4];for(unsigned b=0;b<length;b++)output[b]=(uint8_t)(Registers[(IGW_TX_DATA+(b/4)*4)/4]>>(8*(b%4)));
 struct rsf_packet reply;assert(rsf_parse(output,length,1,&reply));return reply;
}
static void TestR20(void){
 Fixture(0,0);assert(Setup()==AL_OK);BootTick=Now();memset(&RtuStats,0,sizeof RtuStats);memset(&RtuTx,0,sizeof RtuTx);Rtu.read=BoardRead;Rtu.write=BoardWrite;Rtu.now_us=BoardUs;
 assert(!igw_configure(&Rtu,9600));RsFileInit();R20Timing=1;
 uint8_t data[4096],digest[32];for(unsigned i=0;i<sizeof data;i++)data[i]=(uint8_t)(i*73+(i>>8));igwf_digest(data,sizeof data,digest);
 assert(!R20Call(RSF_PUT,100,4096,0,digest,32).status);
 for(unsigned i=0;i<4096;){unsigned n=4096-i;if(n>RSF_BLOCK)n=RSF_BLOCK;assert(!R20Call(RSF_WRITE,100,i,n,data+i,n).status);i+=n;}
 assert(!R20Call(RSF_COMMIT,100,0,0,NULL,0).status&&RsFiles[0].valid&&!RsFiles[1].valid);
 assert(!R20Call(RSF_CLOSE,100,0,0,NULL,0).status);
 assert(!R20Call(RSF_GET,101,0,0,NULL,0).status);
 for(unsigned i=0;i<4096;){unsigned n=4096-i;if(n>RSF_BLOCK)n=RSF_BLOCK;struct rsf_packet a=R20Call(RSF_READ,101,i,n,NULL,0);assert(!a.status&&!memcmp(a.payload,data+i,n));i+=n;}
 assert(!R20Call(RSF_CLOSE,101,0,0,NULL,0).status);
 /* Switch only after reply TX and 100 ms guard; expiration preserves a file. */
 assert(!R20Call(RSF_SPEED,102,115200,0,NULL,0).status);assert(Registers[IGW_DIV/4]==10417);
 Tick+=120000;RsFileTick();assert(RsFileBus.baud==115200&&Registers[IGW_DIV/4]==868);
 assert(!R20Call(RSF_GET,103,0,0,NULL,0).status);
 assert(R20Call(RSF_SPEED,104,9600,0,NULL,0).status==RSF_BUSY);
 Tick+=(RSF_IDLE_MS+1)*1000ULL;RsFileTick();assert(RsFileBus.baud==9600&&RsFiles[0].valid&&RsFiles[0].phase==RSF_FAILED);
 assert(!R20Call(RSF_GET,105,0,0,NULL,0).status);assert(!R20Call(RSF_CLOSE,105,0,0,NULL,0).status);
 assert(!R20Call(RSF_SPEED,106,230400,0,NULL,0).status);assert(Registers[IGW_DIV/4]==10417);
 Tick+=120000;RsFileTick();assert(RsFileBus.baud==230400&&Registers[IGW_DIV/4]==434);
 assert(!R20Call(RSF_GET,107,0,0,NULL,0).status);
 for(unsigned i=0;i<4096;){unsigned n=4096-i;if(n>RSF_BLOCK)n=RSF_BLOCK;struct rsf_packet a=R20Call(RSF_READ,107,i,n,NULL,0);assert(!a.status&&!memcmp(a.payload,data+i,n));i+=n;}
 assert(!R20Call(RSF_CLOSE,107,0,0,NULL,0).status);
 assert(!R20Call(RSF_PUT,110,4096,0,digest,32).status);
 for(unsigned i=0;i<4096;){unsigned n=4096-i;if(n>RSF_BLOCK)n=RSF_BLOCK;assert(!R20Call(RSF_WRITE,110,i,n,data+i,n).status);i+=n;}
 assert(!R20Call(RSF_COMMIT,110,0,0,NULL,0).status);assert(!R20Call(RSF_CLOSE,110,0,0,NULL,0).status);
 assert(!R20Call(RSF_SPEED,108,9600,0,NULL,0).status);Tick+=120000;RsFileTick();assert(RsFileBus.baud==9600&&Registers[IGW_DIV/4]==10417);
 assert(R20Call(RSF_SPEED,109,460800,0,NULL,0).status==RSF_BAD);
 R20Timing=0;
 puts("PASS R20 actual RTU scheduler/MMIO mailbox: binary 4KiB roundtrip, 214-byte frame at 9600, dynamic TX deadline, no unauthorized TX, negotiated 115200 and idle baseline return");
}
