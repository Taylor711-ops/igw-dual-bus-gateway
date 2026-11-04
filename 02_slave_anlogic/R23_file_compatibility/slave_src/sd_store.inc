/* R23 file storage. FAT32-compatible 8.3 names, drive1 from the supplied SDK.
 * Two independent data/metadata generations per unit; no rename-over-old.
 * No format call. Files are confined to 1:/IGW23. */
#include "ff.h"
#include "diskio.h"
#define SD_META_BYTES 88u
#define SD_CHUNK 4096u
static FATFS SdFs __attribute__((aligned(64)));
struct sd_bank {unsigned unit,valid,slot,open,work;uint32_t size;uint64_t generation;uint8_t digest[32];FIL file;};
static struct sd_bank SdBanks[IGW_NODE_COUNT] __attribute__((aligned(64)));
static uint8_t SdBuffer[SD_CHUNK] __attribute__((aligned(64)));
static unsigned SdReady;
static const char *SdReason="not_initialized";
static FRESULT SdLast;
static uint64_t sd_now_ms(void);
static void sd_path(char out[40],unsigned unit,unsigned slot,const char *extension){snprintf(out,40,"1:/IGW23/U%02u%c.%s",unit,slot?'B':'A',extension);}
static int sd_close(struct sd_bank *b){if(!b->open)return 0;SdLast=f_close(&b->file);b->open=0;return SdLast==FR_OK?0:-1;}
static int sd_hash_path(const char *path,uint32_t size,uint8_t digest[32]){
 FIL f __attribute__((aligned(64)));SdLast=f_open(&f,path,FA_READ);if(SdLast!=FR_OK)return SdLast==FR_NO_FILE?0:-1;
 int result=1;if(f_size(&f)!=size)result=0;struct igwf_sha sha;igwf_sha_init(&sha);
 for(uint32_t offset=0;result>0&&offset<size;){UINT count=size-offset,got=0;if(count>SD_CHUNK)count=SD_CHUNK;SdLast=f_read(&f,SdBuffer,count,&got);if(SdLast!=FR_OK){result=-1;break;}if(got!=count){result=0;break;}igwf_sha_update(&sha,SdBuffer,got);offset+=got;}
 FRESULT closed=f_close(&f);if(closed!=FR_OK){SdLast=closed;return -1;}if(result>0)igwf_sha_final(&sha,digest);return result;
}
static void sd_meta_encode(uint8_t p[SD_META_BYTES],unsigned unit,uint32_t size,uint64_t generation,const uint8_t digest[32]){
 memset(p,0,SD_META_BYTES);memcpy(p,"IGWSD23",7);rsf_p32(p+8,unit);rsf_p32(p+12,size);rsf_p32(p+16,(uint32_t)generation);rsf_p32(p+20,(uint32_t)(generation>>32));memcpy(p+24,digest,32);igwf_digest(p,56,p+56);
}
static int sd_meta_read(unsigned unit,unsigned slot,uint32_t *size,uint64_t *generation,uint8_t digest[32]){
 char path[40];sd_path(path,unit,slot,"MET");FIL f __attribute__((aligned(64)));SdLast=f_open(&f,path,FA_READ);
 if(SdLast==FR_NO_FILE||SdLast==FR_NO_PATH){return 0;}if(SdLast!=FR_OK)return -1;
 uint8_t meta[SD_META_BYTES],hash[32];UINT got=0;int valid=f_size(&f)==SD_META_BYTES;
 if(valid){SdLast=f_read(&f,meta,sizeof meta,&got);if(SdLast!=FR_OK){f_close(&f);return -1;}valid=got==sizeof meta;}
 SdLast=f_close(&f);if(SdLast!=FR_OK)return -1;if(!valid)return 0;
 igwf_digest(meta,56,hash);if(memcmp(meta,"IGWSD23\0",8)||rsf_u32(meta+8)!=unit||memcmp(hash,meta+56,32))return 0;
 *size=rsf_u32(meta+12);*generation=rsf_u32(meta+16)|(uint64_t)rsf_u32(meta+20)<<32;
 if(*size>RSF_MAX||!*generation){return 0;}memcpy(digest,meta+24,32);return 1;
}
static int sd_scan(struct sd_bank *b){
 b->valid=0;b->open=0;uint32_t sizes[2]={0};uint64_t generations[2]={0};uint8_t hashes[2][32];unsigned valid[2]={0};
 for(unsigned slot=0;slot<2;slot++){
  int ret=sd_meta_read(b->unit,slot,sizes+slot,generations+slot,hashes[slot]);if(ret<0)return -1;if(!ret)continue;
  char path[40];uint8_t actual[32];sd_path(path,b->unit,slot,"BIN");ret=sd_hash_path(path,sizes[slot],actual);if(ret<0)return -1;
  if(ret&&memcmp(actual,hashes[slot],32)==0)valid[slot]=1;
 }
 if(valid[0]&&valid[1]&&generations[0]==generations[1])return -1;
 if(valid[0]||valid[1]){unsigned slot=valid[1]&&(!valid[0]||generations[1]>generations[0]);b->valid=1;b->slot=slot;b->size=sizes[slot];b->generation=generations[slot];memcpy(b->digest,hashes[slot],32);}
 return 0;
}
static int sd_begin(struct sd_bank *b){
 if(!SdReady||b->open||b->generation==UINT64_MAX)return -1;
 b->work=b->valid?!b->slot:0;char path[40];sd_path(path,b->unit,b->work,"MET");
 /* Remove the inactive commit record before reusing its data file. */
 SdLast=f_unlink(path);if(SdLast!=FR_OK&&SdLast!=FR_NO_FILE&&SdLast!=FR_NO_PATH)return -1;
 sd_path(path,b->unit,b->work,"BIN");SdLast=f_open(&b->file,path,FA_CREATE_ALWAYS|FA_READ|FA_WRITE);if(SdLast!=FR_OK)return -1;b->open=1;return 0;
}
static int sd_write(struct sd_bank *b,uint32_t offset,const uint8_t *data,unsigned count){
 if(!SdReady||!b->open){return -1;}SdLast=f_lseek(&b->file,offset);if(SdLast!=FR_OK||f_tell(&b->file)!=offset)return -1;
 UINT wrote=0;SdLast=f_write(&b->file,data,count,&wrote);return SdLast==FR_OK&&wrote==count?0:-1;
}
static int sd_open_read(struct sd_bank *b){
 if(!SdReady||!b->valid||b->open){return -1;}char path[40];sd_path(path,b->unit,b->slot,"BIN");SdLast=f_open(&b->file,path,FA_READ);if(SdLast!=FR_OK)return -1;
 b->open=1;if(f_size(&b->file)!=b->size){sd_close(b);return -1;}return 0;
}
static int sd_read(struct sd_bank *b,uint32_t offset,uint8_t *data,unsigned count){
 if(!SdReady||!b->open){return -1;}SdLast=f_lseek(&b->file,offset);if(SdLast!=FR_OK||f_tell(&b->file)!=offset)return -1;
 UINT got=0;SdLast=f_read(&b->file,data,count,&got);return SdLast==FR_OK&&got==count?0:-1;
}
/* Return -2 for digest mismatch, -1 for storage error. New generation is
 * selected only after file sync/close, fresh media read+SHA, and metadata
 * sync/close+readback. FAT/card-wide power-cut atomicity is not claimed. */
static int sd_commit(struct sd_bank *b,uint32_t size,const uint8_t expected[32],uint8_t actual[32]){
 if(!SdReady||!b->open){return -1;}SdLast=f_sync(&b->file);if(SdLast!=FR_OK){sd_close(b);return -1;}if(sd_close(b))return -1;
 char path[40];sd_path(path,b->unit,b->work,"BIN");int ret=sd_hash_path(path,size,actual);if(ret!=1)return -1;if(memcmp(actual,expected,32))return -2;
 uint8_t meta[SD_META_BYTES];uint64_t next=b->generation+1;sd_meta_encode(meta,b->unit,size,next,actual);
 sd_path(path,b->unit,b->work,"MET");FIL f __attribute__((aligned(64)));SdLast=f_open(&f,path,FA_CREATE_ALWAYS|FA_WRITE);if(SdLast!=FR_OK)return -1;
 UINT wrote=0;SdLast=f_write(&f,meta,sizeof meta,&wrote);int good=SdLast==FR_OK&&wrote==sizeof meta;if(good){SdLast=f_sync(&f);good=SdLast==FR_OK;}FRESULT closed=f_close(&f);if(!good||closed!=FR_OK)return -1;
 uint32_t checked_size=0;uint64_t checked_gen=0;uint8_t checked_hash[32];ret=sd_meta_read(b->unit,b->work,&checked_size,&checked_gen,checked_hash);
 if(ret!=1||checked_size!=size||checked_gen!=next||memcmp(checked_hash,actual,32))return -1;
 b->slot=b->work;b->valid=1;b->size=size;b->generation=next;memcpy(b->digest,actual,32);return 0;
}
static int sd_generate(struct sd_bank *b,uint64_t token,uint32_t size,uint8_t digest[32]){
 if(sd_begin(b)){return -1;}struct igwf_sha sha;igwf_sha_init(&sha);
 for(uint32_t offset=0;offset<size;){unsigned count=size-offset;if(count>SD_CHUNK)count=SD_CHUNK;for(unsigned i=0;i<count;i++)SdBuffer[i]=rsf_generated(b->unit,token,offset+i);igwf_sha_update(&sha,SdBuffer,count);if(sd_write(b,offset,SdBuffer,count)){sd_close(b);return -1;}offset+=count;}
 igwf_sha_final(&sha,digest);uint8_t actual[32];return sd_commit(b,size,digest,actual);
}
/* Refuse to reuse a nonempty foreign directory. An empty directory left
 * between mkdir and marker creation can be claimed after restart. */
static int sd_owner(void){
 static const uint8_t marker[]="IGW_SD_STORE_R23\n";FIL f;SdLast=f_open(&f,"1:/IGW23/OWNER.ID",FA_READ);
 if(SdLast==FR_OK){uint8_t b[sizeof marker];UINT n=0;int good=f_size(&f)==sizeof marker-1;SdLast=f_read(&f,b,sizeof marker,&n);good=good&&SdLast==FR_OK&&n==sizeof marker-1&&!memcmp(b,marker,n);FRESULT closed=f_close(&f);return good&&closed==FR_OK?0:-1;}
 if(SdLast!=FR_NO_FILE)return -1;
 DIR dir;FILINFO entry;SdLast=f_opendir(&dir,"1:/IGW23");if(SdLast!=FR_OK)return -1;SdLast=f_readdir(&dir,&entry);int empty=SdLast==FR_OK&&!entry.fname[0];FRESULT closed=f_closedir(&dir);if(!empty||closed!=FR_OK)return -1;
 SdLast=f_open(&f,"1:/IGW23/OWNER.ID",FA_CREATE_NEW|FA_WRITE);if(SdLast!=FR_OK)return -1;UINT n=0;SdLast=f_write(&f,marker,sizeof marker-1,&n);int good=SdLast==FR_OK&&n==sizeof marker-1;if(good){SdLast=f_sync(&f);good=SdLast==FR_OK;}closed=f_close(&f);return good&&closed==FR_OK?0:-1;
}
static int sd_storage_init(void){
 SdReady=0;SdReason="mount";memset(&SdFs,0,sizeof SdFs);memset(SdBanks,0,sizeof SdBanks);SdLast=f_mount(&SdFs,"1:",1);if(SdLast!=FR_OK)return -1;
 /* Drive1 is SDK SD/MMC0. Refuse other filesystem types; never format. */
 SdReason="filesystem";if(SdFs.fs_type!=FS_FAT32&&SdFs.fs_type!=FS_FAT16)return -1;
 SdReason="directory";SdLast=f_mkdir("1:/IGW23");if(SdLast!=FR_OK&&SdLast!=FR_EXIST)return -1;
 SdReason="directory_owner";if(sd_owner())return -1;SdReason="scan_generations";
 for(unsigned i=0;i<IGW_NODE_COUNT;i++){SdBanks[i].unit=IGW_FIRST_NODE+i;if(sd_scan(SdBanks+i))return -1;}
 SdLast=FR_OK;SdReason="ready";SdReady=1;return 0;
}

_Static_assert(sizeof SdBuffer+sizeof SdFs+sizeof SdBanks<65536u,"SD workspace exceeds 64KiB");
