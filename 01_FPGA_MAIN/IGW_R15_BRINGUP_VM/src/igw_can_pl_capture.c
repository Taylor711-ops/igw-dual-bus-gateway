/* R15 passive RX evidence collector. No CAN/RS485 transmit operations.
 * MMIO writes: POP=1 only. Sole-reader file lock prevents torn head records.
 * Do not run capture on the R13 bitstream; use guarded_capture.sh only after
 * a newly built R15 BOOT has been reviewed, installed and cold booted.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
static volatile sig_atomic_t stopping;
static void request_stop(int sig){(void)sig;stopping=1;}
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
static void die(const char *s){perror(s);exit(1);}
static uint32_t mmread(volatile uint32_t *r,unsigned offset){__sync_synchronize();uint32_t v=r[offset/4];__sync_synchronize();return v;}
static void print_record(const uint32_t w[8]){
    unsigned dlc=w[2]&15,rtr=(w[2]>>4)&1;
    printf("{\"seq\":%"PRIu32",\"id\":%u,\"dlc\":%u,\"rtr\":%u,\"ack\":%u,\"crc15\":%u,\"sof_ticks\":%"PRIu64",\"duration_cycles\":%"PRIu32",\"data\":\"",w[0],w[1]&2047,dlc,rtr,(w[2]>>6)&1,(w[2]>>16)&32767,((uint64_t)w[4]<<32)|w[3],w[7]);
    for(unsigned i=0;i<(rtr?0:dlc)&&i<8;i++)printf("%02X",(w[5+i/4]>>(8*(i%4)))&255);
    puts("\"}");
}
static uint64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))die("clock");return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;}
int main(int argc,char **argv){
    if(argc==3&&!strcmp(argv[1],"--decode")){
        FILE *f=fopen(argv[2],"rb");if(!f)die("fixture");unsigned char b[32];size_t n;
        while((n=fread(b,1,sizeof b,f))!=0){if(n!=32){fclose(f);fputs("truncated record\n",stderr);return 2;}uint32_t w[8];for(unsigned i=0;i<8;i++)w[i]=(uint32_t)b[4*i]|((uint32_t)b[4*i+1]<<8)|((uint32_t)b[4*i+2]<<16)|((uint32_t)b[4*i+3]<<24);print_record(w);}
        if(ferror(f))die("fixture read");
        return fclose(f)?1:0;
    }
    if(argc!=3||strcmp(argv[1],"--capture-seconds")){fputs("Usage: --decode records.bin OR --capture-seconds 1..600 (R15 hardware only)\n",stderr);return 2;}
    char *end;errno=0;long seconds=strtol(argv[2],&end,10);if(errno||*end||seconds<1||seconds>600)return 2;
    int lock=open("/tmp/igw_plcan_capture.lock",O_CREAT|O_RDWR,0600);if(lock<0)die("lock open");if(flock(lock,LOCK_EX|LOCK_NB))die("another FIFO reader active");
    int fd=open("/dev/mem",O_RDWR|O_SYNC);if(fd<0)die("devmem open");
    void *map=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t)0x80400000);if(map==MAP_FAILED)die("mmap");
    volatile uint32_t *r=(volatile uint32_t *)((unsigned char *)map+0x800);
    if(mmread(r,0)!=0x4e414349||mmread(r,4)!=0x10000||mmread(r,8)!=31||mmread(r,0x18)!=50000000||mmread(r,0x1c)!=500000||mmread(r,0x44)!=16){fputs("R15 identity mismatch\n",stderr);return 3;}
    struct sigaction action;memset(&action,0,sizeof action);action.sa_handler=request_stop;sigemptyset(&action.sa_mask);
    if(sigaction(SIGTERM,&action,NULL)||sigaction(SIGINT,&action,NULL))die("sigaction");
    uint32_t before[10],after[10];for(unsigned i=0;i<10;i++)before[i]=mmread(r,0x20+4*i);
    uint64_t stop=now_ms()+1000*(uint64_t)seconds,count=0;struct timespec pause={0,500000};
    fprintf(stderr,"CAPTURE_READY INITIAL_FIFO=%"PRIu32" INITIAL_GOOD=%"PRIu32" INITIAL_DROP=%"PRIu32"\n",mmread(r,0x14),before[0],before[1]);
    while(!stopping&&now_ms()<stop){
        if(mmread(r,0x14)){
            uint32_t w[8];for(unsigned i=0;i<8;i++)w[i]=mmread(r,0x80+4*i);
            print_record(w);__sync_synchronize();r[3]=1;__sync_synchronize();count++;
        }else if(nanosleep(&pause,NULL)&&errno!=EINTR)die("sleep");
    }
    for(unsigned i=0;i<10;i++)after[i]=mmread(r,0x20+4*i);
    fprintf(stderr,"CAPTURED=%"PRIu64" GOOD_DELTA=%"PRIu32" DROP_DELTA=%"PRIu32" STUFF_DELTA=%"PRIu32" CRC_DELTA=%"PRIu32" FORMAT_DELTA=%"PRIu32" UNSUPPORTED_DELTA=%"PRIu32" ACK_DELTA=%"PRIu32" TIMEOUT_DELTA=%"PRIu32" FIFO_REMAINING=%"PRIu32"\n",count,after[0]-before[0],after[1]-before[1],after[2]-before[2],after[3]-before[3],after[4]-before[4],after[5]-before[5],after[6]-before[6],after[7]-before[7],mmread(r,0x14));
    if(munmap(map,4096))die("munmap");
    close(fd);close(lock);return 0;
}
