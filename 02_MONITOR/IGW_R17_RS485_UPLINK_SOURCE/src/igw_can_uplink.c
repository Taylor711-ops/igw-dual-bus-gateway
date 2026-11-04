#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "uplink_protocol.h"
static volatile sig_atomic_t stopping;
static void stop_signal(int sig){(void)sig;stopping=1;}
static uint64_t clock_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))abort();return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;}
static unsigned number(const char *s,unsigned lo,unsigned hi){char *end;errno=0;unsigned long x=strtoul(s,&end,10);if(errno||!*s||*end||x<lo||x>hi){fputs("invalid number\n",stderr);exit(2);}return (unsigned)x;}
static int publish(const char *path,const struct ul_server_node ns[16],unsigned nodes,int running,uint64_t started){
    char temp[512];if(snprintf(temp,sizeof temp,"%s.XXXXXX",path)>=(int)sizeof temp)return -1;
    int fd=mkstemp(temp);if(fd<0)return -1;
    FILE *f=fdopen(fd,"w");if(!f){close(fd);unlink(temp);return -1;}
    uint64_t now=clock_ms();
    fprintf(f,"{\"schema\":\"IGW_CAN_UPLINK_R16\",\"running\":%s,\"sampled_ms\":%"PRIu64",\"uptime_ms\":%"PRIu64",\"source\":\"device_generated_test_counter\",\"register_space\":\"host_uplink_shadow\",\"devices\":[",running?"true":"false",now,now-started);
    for(unsigned i=0;i<nodes;i++){
        const struct ul_server_node *n=ns+i;
        fprintf(f,"%s{\"unit\":%u,\"valid\":%s,\"address\":16,\"value\":%u,\"sequence\":%u,\"epoch\":%"PRIu32",\"accepted\":%"PRIu64",\"duplicates\":%"PRIu64",\"stale\":%"PRIu64",\"invalid\":%"PRIu64",\"sessions\":%"PRIu64",\"age_ms\":",i?",":"",i+1,n->valid?"true":"false",n->value,n->seq,n->epoch,n->accepted,n->duplicates,n->stale,n->invalid,n->sessions);
        if(n->valid)fprintf(f,"%"PRIu64,now-n->seen_ms);else fputs("null",f);
        fputc('}',f);
    }
    fputs("]}",f);int failed=ferror(f);if(fclose(f))failed=1;
    if(failed||rename(temp,path)){unlink(temp);return -1;}
    return 0;
}
static int can_open(unsigned nodes){
    int fd=socket(PF_CAN,SOCK_RAW|SOCK_NONBLOCK,CAN_RAW);if(fd<0)return -1;
    struct can_filter filters[32];
    for(unsigned i=0;i<nodes;i++){
        filters[2*i]=(struct can_filter){.can_id=UL_HELLO+i+1,.can_mask=CAN_SFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG};
        filters[2*i+1]=(struct can_filter){.can_id=UL_DATA+i+1,.can_mask=CAN_SFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG};
    }
    struct sockaddr_can address={.can_family=AF_CAN,.can_ifindex=(int)if_nametoindex("can0")};
    if(!address.can_ifindex||setsockopt(fd,SOL_CAN_RAW,CAN_RAW_FILTER,filters,2*nodes*sizeof filters[0])||bind(fd,(struct sockaddr *)&address,sizeof address)){close(fd);return -1;}
    return fd;
}
int main(int argc,char **argv){
    unsigned nodes=2;const char *path="/tmp/igw_r16_uplink.json";int fd=-1,lock=-1;
#ifdef IGW_UPLINK_HOST_TEST
    const char *lock_path=NULL;
#else
    const char *lock_path="/tmp/igw_r16_uplink.lock";
#endif
    for(int i=1;i<argc;i++){
        if(i+1>=argc)return 2;
        const char *key=argv[i],*value=argv[++i];
        if(!strcmp(key,"--nodes"))nodes=number(value,1,16);
#ifdef IGW_UPLINK_HOST_TEST
        else if(!strcmp(key,"--test-fd"))fd=(int)number(value,0,100000);
        else if(!strcmp(key,"--state-file"))path=value;
#endif
        else return 2;
    }
    if(lock_path){lock=open(lock_path,O_CREAT|O_RDWR,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){perror("uplink already owned");return 2;}}
    if(fd<0)fd=can_open(nodes);
    if(fd<0){perror("CAN uplink socket");return 2;}
#ifdef IGW_UPLINK_HOST_TEST
    int test_transport=1;
#else
    int test_transport=0;
#endif
    int flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)){perror("nonblock");return 2;}
    struct sigaction action={0};action.sa_handler=stop_signal;sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);sigaction(SIGHUP,&action,NULL);
    uint32_t counter=0;int random=open("/dev/urandom",O_RDONLY);
    if(random<0||read(random,&counter,sizeof counter)!=(ssize_t)sizeof counter){perror("epoch seed");return 2;}
    close(random);struct ul_server_node ns[16]={0};uint64_t started=clock_ms(),next_publish=started;int result=0;
    if(publish(path,ns,nodes,1,started)){perror("initial snapshot");return 2;}
    setvbuf(stdout,NULL,_IOLBF,0);printf("[READY] CAN_UPLINK_R16 nodes=%u host_register=16 mode=device_initiated_latest_state no_bus_configuration\n",nodes);
    while(!stopping){
        if(fd<0){
            fd=can_open(nodes);
            if(fd<0){
                if(publish(path,ns,nodes,1,started)){result=1;break;}
                (void)poll(NULL,0,250);continue;
            }
            puts("[UPLINK] CAN socket rebound after interface recovery");
        }
        struct pollfd p={fd,POLLIN,0};int ret=poll(&p,1,100);
        if(ret<0){if(errno==EINTR)continue;perror("poll");result=1;break;}
        if(p.revents&(POLLERR|POLLHUP|POLLNVAL)){
            if(test_transport){fputs("CAN surrogate transport closed\n",stderr);result=1;break;}
            /* The legacy gateway may down/up can0 to cancel a timed-out TX.
             * Preserve committed host shadows while rebinding the RX socket. */
            close(fd);fd=-1;puts("[UPLINK] CAN socket recovery; latest shadows retained");
            (void)poll(NULL,0,100);continue;
        }
        for(unsigned budget=0;budget<64&&(p.revents&POLLIN);budget++){
            struct can_frame wire;ssize_t size=read(fd,&wire,sizeof wire);
            if(size<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR){perror("CAN read");result=1;stopping=1;}break;}
            if(size!=(ssize_t)sizeof wire||wire.can_dlc!=8||(wire.can_id&(CAN_EFF_FLAG|CAN_RTR_FLAG|CAN_ERR_FLAG)))continue;
            unsigned id=wire.can_id,unit=id>UL_DATA&&id<=UL_DATA+nodes?id-UL_DATA:id>UL_HELLO&&id<=UL_HELLO+nodes?id-UL_HELLO:0;
            if(!unit)continue;
            struct ul_frame in={.id=id},out;memcpy(in.data,wire.data,8);unsigned modified;
            int reply=ul_server_receive(ns+unit-1,unit,&counter,&in,&out,clock_ms(),&modified);
            /* Publish state before ACK. A failed publication stops the service;
             * it must never acknowledge data that the web cannot observe. */
            if(publish(path,ns,nodes,1,started)){perror("publish");result=1;stopping=1;break;}
            if(modified)printf("[UPLINK] unit=%u host_register=16 value=%u sequence=%u accepted=%"PRIu64" duplicates=%"PRIu64"\n",unit,ns[unit-1].value,ns[unit-1].seq,ns[unit-1].accepted,ns[unit-1].duplicates);
            if(reply){memset(&wire,0,sizeof wire);wire.can_id=out.id;wire.can_dlc=8;memcpy(wire.data,out.data,8);
                if(write(fd,&wire,sizeof wire)!=(ssize_t)sizeof wire)perror("ACK submit; device may retry");}
        }
        if(clock_ms()>=next_publish){if(publish(path,ns,nodes,1,started)){perror("periodic snapshot");result=1;break;}next_publish=clock_ms()+1000;}
    }
    (void)publish(path,ns,nodes,0,started);
    for(unsigned i=0;i<nodes;i++)printf("[UPLINK-TOTAL] unit=%u accepted=%"PRIu64" duplicates=%"PRIu64" stale=%"PRIu64" invalid=%"PRIu64" sessions=%"PRIu64"\n",i+1,ns[i].accepted,ns[i].duplicates,ns[i].stale,ns[i].invalid,ns[i].sessions);
    if(fd>=0)close(fd);
    if(lock>=0)close(lock);
    puts("[STOP] CAN uplink stopped; host registers are volatile");return result;
}
