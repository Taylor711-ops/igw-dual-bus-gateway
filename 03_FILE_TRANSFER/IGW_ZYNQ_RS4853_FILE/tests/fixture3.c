#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "ramdisk0.inc"
#include "../zynq/src/file_engine.c"
static uint64_t tick=100;
uint64_t rs3_clock_ms(void){return tick;}
static int exact(int fd,void *data,size_t n,int wr){
    uint8_t *p=data;while(n){ssize_t k=wr?write(fd,p,n):read(fd,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;
}
int main(int argc,char **argv){
    if(argc!=4)return 2;
    int rd=atoi(argv[1]),wr=atoi(argv[2]);
    test_disk_start(argv[3]);if(rs3_file_init())return 2;
    for(;;){uint8_t in[258],out[258]={0};if(exact(rd,in,sizeof in,0))break;
        unsigned n=rsf_u16(in);if(n>256)break;
        size_t len=rs3_file_request(in+2,n,out+2);out[0]=(uint8_t)len;out[1]=(uint8_t)(len>>8);tick++;
        if(exact(wr,out,sizeof out,1))break;
    }
    test_disk_finish();return 0;
}
