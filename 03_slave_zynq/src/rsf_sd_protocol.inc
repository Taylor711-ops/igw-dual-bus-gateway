/* R23 shared CAN/RS485 protocol using SD generations, not full-file arrays. */
struct rsf_store {
 uint8_t digest[32],expected[32],last_data[RSF_BLOCK],read_data[RSF_BLOCK];
 uint32_t size,next,last_offset;unsigned last_count,unit,phase,valid,last_mode;
 uint64_t token,seen;struct sd_bank *bank;
};
static void rsf_restore(struct rsf_store *s){s->valid=s->bank->valid;s->size=s->bank->valid?s->bank->size:0;memcpy(s->digest,s->bank->digest,32);}
static void rsf_abandon(struct rsf_store *s){(void)sd_close(s->bank);s->phase=RSF_FAILED;rsf_restore(s);}
static void rsf_expire(struct rsf_store *s,uint64_t now){if(s->phase!=RSF_EMPTY&&s->phase!=RSF_FAILED&&now-s->seen>=RSF_IDLE_MS)rsf_abandon(s);}
static int rsf_active(struct rsf_store *s){return s->phase==RSF_WRITING||s->phase==RSF_READING;}
static size_t rsf_handle(struct rsf_store *s,const uint8_t *p,size_t n,uint8_t out[256],uint64_t now){
 struct rsf_packet q;unsigned error=RSF_OK,count=0,length=0;uint32_t offset=0;const uint8_t *payload=NULL;
 if(!rsf_parse(p,n,0,&q)||q.unit!=s->unit)return 0;
 rsf_expire(s,now);offset=q.offset;count=q.count;
 if(!SdReady){error=RSF_STORAGE;goto done;}
 if(q.status||q.op<RSF_PUT||q.op>RSF_ABORT){error=RSF_BAD;goto done;}
 if(q.op==RSF_PUT||q.op==RSF_GET||q.op==RSF_GENERATE){
  if(q.count||(q.op==RSF_PUT?q.length!=32:q.length!=0)||q.offset>RSF_MAX||(q.op==RSF_GET&&q.offset)){error=RSF_BAD;goto done;}
  if(q.token==s->token){
   if(s->phase==RSF_FAILED||s->phase==RSF_EMPTY||q.op!=s->last_mode||(q.op!=RSF_GET&&q.offset!=s->size)||(q.op==RSF_PUT&&memcmp(q.payload,s->expected,32))){error=RSF_CONFLICT;goto done;}
  }else{
   if(rsf_active(s)){error=RSF_BUSY;goto done;}
   if(q.op==RSF_GET&&!s->valid){error=RSF_NO_FILE;goto done;}
   s->token=q.token;s->last_mode=q.op;s->next=0;s->last_count=0;
   if(q.op==RSF_PUT){
    s->size=q.offset;memcpy(s->expected,q.payload,32);
    if(sd_begin(s->bank)){rsf_abandon(s);error=RSF_STORAGE;goto done;}s->phase=RSF_WRITING;
   }else{
    if(q.op==RSF_GENERATE){if(sd_generate(s->bank,q.token,q.offset,s->digest)){rsf_abandon(s);error=RSF_STORAGE;goto done;}rsf_restore(s);}
    if(sd_open_read(s->bank)){rsf_abandon(s);error=RSF_STORAGE;goto done;}rsf_restore(s);s->phase=RSF_READING;
   }
  }
  offset=s->size;if(q.op!=RSF_PUT){payload=s->digest;length=32;}
 }else{
  if(q.token!=s->token||s->phase==RSF_FAILED){error=RSF_SESSION;goto done;}
  if(q.op==RSF_WRITE){
   if(s->phase!=RSF_WRITING||!q.count||q.length!=q.count||q.offset>s->size||q.count>s->size-q.offset){error=RSF_RANGE;goto done;}
   if(q.offset==s->next){
    if(sd_write(s->bank,q.offset,q.payload,q.count)){rsf_abandon(s);error=RSF_STORAGE;goto done;}
    s->last_offset=q.offset;s->last_count=q.count;memcpy(s->last_data,q.payload,q.count);s->next+=q.count;
   }else if(q.offset!=s->last_offset||q.count!=s->last_count||memcmp(s->last_data,q.payload,q.count)){error=RSF_CONFLICT;goto done;}
  }else if(q.op==RSF_READ){
   if(s->phase!=RSF_READING||q.length||!q.count||q.offset>s->size||q.count>s->size-q.offset){error=RSF_RANGE;goto done;}
   if(sd_read(s->bank,q.offset,s->read_data,q.count)){rsf_abandon(s);error=RSF_STORAGE;goto done;}payload=s->read_data;length=q.count;
  }else{
   if(q.length||q.count||q.offset){error=RSF_BAD;goto done;}
   if(q.op==RSF_COMMIT){
    if(s->phase!=RSF_COMMITTED){
     if(s->phase!=RSF_WRITING||s->next!=s->size){error=RSF_INCOMPLETE;goto done;}
     int ret=sd_commit(s->bank,s->size,s->expected,s->digest);
     if(ret){rsf_abandon(s);error=ret==-2?RSF_DIGEST:RSF_STORAGE;goto done;}
     rsf_restore(s);s->phase=RSF_COMMITTED;
    }
    payload=s->digest;length=32;
   }else if(q.op==RSF_CLOSE){
    if(s->phase==RSF_WRITING){error=RSF_INCOMPLETE;goto done;}if(sd_close(s->bank)){rsf_abandon(s);error=RSF_STORAGE;goto done;}s->phase=RSF_EMPTY;
   }else if(q.op==RSF_ABORT)rsf_abandon(s);
  }
 }
 s->seen=sd_now_ms();
done:
 return rsf_frame(out,q.unit,1,q.op,error,q.token,offset,count,payload,length);
}
