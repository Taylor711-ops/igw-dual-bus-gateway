/* Native-only RAM/mapped-image disk. Never linked into slave firmware. */
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "ff.h"
#include "diskio.h"
#define TEST_DISK_BYTES (64u*1024u*1024u)
static uint8_t *TestDisk;
static long TestFailWrite=-1;
static int TestDiskFd=-1;
DSTATUS disk_status(BYTE drive){return drive==0&&TestDisk?0:STA_NOINIT;}
DSTATUS disk_initialize(BYTE drive){return disk_status(drive);}
DRESULT disk_read(BYTE drive,BYTE *out,DWORD sector,UINT count){if(drive!=0||!TestDisk||!count||sector>TEST_DISK_BYTES/512||count>TEST_DISK_BYTES/512-sector){return RES_PARERR;}memcpy(out,TestDisk+(size_t)sector*512,(size_t)count*512);return RES_OK;}
DRESULT disk_write(BYTE drive,const BYTE *in,DWORD sector,UINT count){
 if(TestFailWrite==0){return RES_ERROR;}if(TestFailWrite>0)TestFailWrite--;
 if(drive!=0||!TestDisk||!count||sector>TEST_DISK_BYTES/512||count>TEST_DISK_BYTES/512-sector){return RES_PARERR;}memcpy(TestDisk+(size_t)sector*512,in,(size_t)count*512);return RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE op,void *out){if(drive!=0||!TestDisk)return RES_PARERR;switch(op){case GET_SECTOR_SIZE:*(WORD *)out=512;break;case GET_SECTOR_COUNT:*(DWORD *)out=TEST_DISK_BYTES/512;break;case GET_BLOCK_SIZE:*(DWORD *)out=1;break;case CTRL_SYNC:if(TestDiskFd>=0&&msync(TestDisk,TEST_DISK_BYTES,MS_SYNC))return RES_ERROR;break;default:return RES_PARERR;}return RES_OK;}
DWORD get_fattime(void){return (DWORD)(2026-1980)<<25|1u<<21|1u<<16;}
static void test_disk_start(const char *path){
 int fresh=1;if(path){TestDiskFd=open(path,O_RDWR|O_CREAT,0600);assert(TestDiskFd>=0);struct stat st;assert(!fstat(TestDiskFd,&st));fresh=!st.st_size;assert(fresh||st.st_size==TEST_DISK_BYTES);assert(!ftruncate(TestDiskFd,TEST_DISK_BYTES));TestDisk=mmap(NULL,TEST_DISK_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,TestDiskFd,0);assert(TestDisk!=MAP_FAILED);}
 else {TestDisk=calloc(1,TEST_DISK_BYTES);assert(TestDisk);}
 if(fresh){MKFS_PARM options;memset(&options,0,sizeof options);options.fmt=FM_FAT32;uint8_t work[4096];assert(f_mkfs("0:",&options,work,sizeof work)==FR_OK);}
}
static void test_disk_finish(void){if(TestDiskFd>=0){msync(TestDisk,TEST_DISK_BYTES,MS_SYNC);munmap(TestDisk,TEST_DISK_BYTES);close(TestDiskFd);}else free(TestDisk);TestDisk=NULL;}
