/* R17 RS485 multi-node single owner: legacy FC03/FC06 and opt-in controlled uploads.
 * UDP sequence is NOT carried in Modbus RTU; see README late-reply limit.
 * Independent process/port from the unchanged CAN gateway. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _FILE_OFFSET_BITS 64
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "rw_wire.h"
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include "rs_uplink_protocol.h"

enum { AUTH=0x0c, FEED=0x10, STATUS=0x1c, TXLEN=0x30, KICK=0x34,
       ACK=0x38, RXLEN=0x40, GOOD=0x50, CRC=0x54, UART=0x58,
       OVERRUN=0x5c, TXCOUNT=0x60, WD=0x64, GAP=0x68 };
static volatile sig_atomic_t stopping;
static volatile uint32_t *mmio;
static const char *lock_path="/tmp/igw_rs485_once.lock";
static bool lock_owned, write_owned;
#ifdef IGW_RS485_TEST
static int test_fd=-1;
#endif
struct counts { uint64_t attempts,ok,timeout,tx_fail,bad_reply,exception,busy,duplicate,ignored,udp_fail; };
enum { CACHE_SIZE=512 };
struct cached_result {
    bool valid; uint64_t until;
    struct sockaddr_in client; struct command cmd; char text[2048];
};
static const char *rs_state_path="/tmp/igw_r17_rs485_uplink.json";
struct rs_up_node {
 uint32_t epoch,nonce,want_epoch;uint16_t seq,value,want_seq;unsigned valid,waiting;uint32_t released_nonce;uint16_t released_seq;
 uint64_t accepted,duplicates,stale,invalid,seen,requested;
};
struct rs_up_host {
 int fd;unsigned enabled,test,active,unit,next,normal_turn,failed;
 uint32_t nonce_seed,nonce,epoch,errors[5];uint16_t seq;
 uint64_t started,until,guard,rebind,publish_due,timeouts,bad,submit_fail,cancel_due,accounted_good;unsigned timed_out,done_received;uint64_t done_at;
 struct rs_up_node node[16];
};
struct gateway {
    struct rs_up_host up;
    int udp;
    struct in_addr peer;
    struct sockaddr_in client;
    struct command cmd;
    struct cached_result saved[17][CACHE_SIZE];
    unsigned slot;
    uint8_t request[8];
    struct counts c;
    uint32_t tx_before;
    uint32_t errors_before[5];
    uint64_t sent,deadline,cooldown[17];
    unsigned nodes;
    bool pending,tx_complete;
    char accounting_owner[17];
};
static uint64_t now_us(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) { perror("clock_gettime"); exit(2); }
    return (uint64_t)t.tv_sec*1000000+(uint64_t)t.tv_nsec/1000;
}
static void stop_signal(int n) { (void)n; stopping=1; }
#ifdef IGW_RS485_TEST
static uint32_t rpc(uint32_t op,uint32_t off,uint32_t value) {
    uint32_t msg[3]={op,off,value},answer;
    ssize_t n;
    do { n=send(test_fd,msg,sizeof(msg),MSG_NOSIGNAL); } while (n<0 && errno==EINTR);
    if (n!=(ssize_t)sizeof(msg)) {
        fputs("Test MMIO send failed\n",stderr); exit(2);
    }
    do { n=recv(test_fd,&answer,sizeof(answer),MSG_WAITALL); } while (n<0 && errno==EINTR);
    if (n!=(ssize_t)sizeof(answer)) {
        fputs("Test MMIO transport failed\n",stderr); exit(2);
    }
    return answer;
}
static uint32_t rd(unsigned off) { return rpc(0,off,0); }
static void wr(unsigned off,uint32_t v) { (void)rpc(1,off,v); }
#else
static uint32_t rd(unsigned off) {
    __sync_synchronize();
    uint32_t v=mmio[off/4];
    __sync_synchronize();
    return v;
}
static void wr(unsigned off,uint32_t v) {
    __sync_synchronize(); mmio[off/4]=v; __sync_synchronize();
    (void)mmio[AUTH/4]; /* drain posted AXI writes */
}
#endif
static void cleanup(void) {
    if (write_owned) { wr(AUTH,0); write_owned=false; }
    if (mmio) munmap((void *)mmio,4096);
    if (lock_owned) { rmdir(lock_path); lock_owned=false; }
}
static uint16_t crc16(const uint8_t *p,unsigned n) {
    uint16_t c=0xffff;
    while (n--) {
        c^=*p++;
        for (unsigned b=0;b<8;b++) c=(uint16_t)((c>>1)^((c&1)?0xa001:0));
    }
    return c;
}
static bool same_client(const struct sockaddr_in *a,const struct sockaddr_in *b) {
    return a->sin_addr.s_addr==b->sin_addr.s_addr && a->sin_port==b->sin_port;
}
static void send_text(struct gateway *g,const struct sockaddr_in *to,const char *s) {
    if (sendto(g->udp,s,strlen(s),0,(const struct sockaddr *)to,sizeof(*to))<0) g->c.udp_fail++;
}
static void result(struct gateway *g,char *out,size_t cap,const char *status,
                   const struct command *cmd,uint64_t rtt,const uint8_t *p,unsigned n) {
    const struct command fallback={1,0,1,3,0,4};
    if (!cmd) cmd=&fallback;
    char hex[43]="",regs[100]="[]";
    if (n>21) n=0;
    for (unsigned i=0;i<n;i++) snprintf(hex+2*i,3,"%02X",p[i]);
    if (!strcmp(status,"ok") && cmd->fc==3) {
        size_t used=0;
        regs[used++]='[';
        for (unsigned i=0;i<cmd->arg;i++) {
            int count=snprintf(regs+used,sizeof(regs)-used,"%s%u",i?",":"",(p[3+2*i]<<8)|p[4+2*i]);
            if (count<0 || (size_t)count>=sizeof(regs)-used) {strcpy(regs,"[]");break;}
            used+=(size_t)count;
        }
        if (used<sizeof(regs)-1) {regs[used++]=']';regs[used]=0;}
    }
    const char *execution=cmd->fc==6 ? (!strcmp(status,"ok")?"acknowledged":
        !strcmp(status,"modbus_exception")?"rejected":"unknown") : "read";
    char accounting[512]="";
    if(g->up.enabled)snprintf(accounting,sizeof accounting,
        ",\"pl_accounting\":{\"schema\":\"IGW_RS485_PL_ACCOUNTING_R17_1\",\"mode\":\"ordinary_plus_validated_uplink\",\"owner_session\":\"%s\",\"validated_uplink_good\":%"PRIu64",\"bad_uplink_frames\":%"PRIu64",\"slot_timeouts\":%"PRIu64",\"control_submit_fail\":%"PRIu64"}",
        g->accounting_owner,g->up.accounted_good,g->up.bad,g->up.timeouts,g->up.submit_fail);
    snprintf(out,cap,
        "{\"v\":%u,\"type\":\"rs485_reply\",\"status\":\"%s\",\"seq\":%"PRIu32
        ",\"op\":\"%s\",\"unit\":%u,\"address\":%u,\"count\":%u,\"value\":%u,\"execution\":\"%s\""
        ",\"data\":\"%s\",\"registers\":%s,\"rs485_rtt_us\":%"PRIu64
        ",\"stats\":{\"attempts\":%"PRIu64",\"ok\":%"PRIu64",\"timeout\":%"PRIu64
        ",\"tx_fail\":%"PRIu64",\"bad_reply\":%"PRIu64",\"exception\":%"PRIu64
        ",\"busy\":%"PRIu64",\"duplicate\":%"PRIu64",\"ignored\":%"PRIu64
        ",\"udp_fail\":%"PRIu64"},\"pl\":{\"good\":%"PRIu32",\"tx\":%"PRIu32
        ",\"crc\":%"PRIu32",\"uart\":%"PRIu32",\"gap\":%"PRIu32
        ",\"overrun\":%"PRIu32",\"watchdog\":%"PRIu32"}%s}",
        cmd->v,status,cmd->seq,cmd->fc==6?"WRITE":"READ",cmd->unit,cmd->address,
        cmd->fc==3?cmd->arg:1,cmd->fc==6?cmd->arg:0,execution,
        hex,regs,rtt,g->c.attempts,g->c.ok,g->c.timeout,g->c.tx_fail,
        g->c.bad_reply,g->c.exception,g->c.busy,g->c.duplicate,g->c.ignored,g->c.udp_fail,
        rd(GOOD),rd(TXCOUNT),rd(CRC),rd(UART),rd(GAP),rd(OVERRUN),rd(WD),accounting);
}
static void immediate(struct gateway *g,const struct sockaddr_in *to,const char *status,const struct command *cmd) {
    char text[2048]; result(g,text,sizeof(text),status,cmd,0,NULL,0); send_text(g,to,text);
}
static void finish(struct gateway *g,const char *status,const uint8_t *p,unsigned n) {
    wr(AUTH,0);
    uint64_t rtt=now_us()-g->sent;
    struct cached_result *saved=&g->saved[g->cmd.unit][g->slot];
    result(g,saved->text,sizeof(saved->text),status,&g->cmd,rtt,p,n);
    saved->client=g->client; saved->cmd=g->cmd;
    saved->valid=true; saved->until=now_us()+60000000;
    send_text(g,&g->client,saved->text);
    printf("[RS-GW-R17.1] seq=%"PRIu32" unit=%u op=%s address=%u status=%s rs485_rtt_us=%"PRIu64
           " attempts=%"PRIu64" ok=%"PRIu64" timeout=%"PRIu64"\n",
           g->cmd.seq,g->cmd.unit,g->cmd.fc==6?"WRITE":"READ",g->cmd.address,status,rtt,g->c.attempts,g->c.ok,g->c.timeout);
    g->pending=false;
    if (strcmp(status,"ok")) g->cooldown[g->cmd.unit]=now_us()+2000000;
}
static const unsigned error_offsets[5]={CRC,UART,GAP,OVERRUN,WD};
#include "rs_uplink_host.h"
static void udp_read(struct gateway *g) {
    char buf[128]; struct sockaddr_in from={0}; socklen_t len=sizeof(from);
    ssize_t n=recvfrom(g->udp,buf,sizeof(buf)-1,MSG_TRUNC,(struct sockaddr *)&from,&len);
    if (n<0 || from.sin_addr.s_addr!=g->peer.s_addr) return;
    if (n>=(ssize_t)sizeof(buf)-1 || memchr(buf,0,(size_t)n)) {
        immediate(g,&from,"bad_request",NULL); return;
    }
    buf[n]=0;
    if (!strcmp(buf,"IGW1 STATS")) { immediate(g,&from,"stats",NULL); return; }
    struct command cmd;
    if (!parse_command(buf,&cmd) || cmd.unit>g->nodes) { immediate(g,&from,"bad_request",NULL); return; }
    uint64_t now=now_us();
    if (g->pending && cmd.seq==g->cmd.seq && cmd.unit==g->cmd.unit && same_client(&from,&g->client)) {
        if (!command_equal(&cmd,&g->cmd)) immediate(g,&from,"sequence_conflict",&cmd);
        else g->c.duplicate++;
        return;
    }
    int available=-1;
    for (unsigned i=0;i<CACHE_SIZE;i++) {
        struct cached_result *saved=&g->saved[cmd.unit][i];
        if (saved->valid && now>=saved->until) saved->valid=false;
        if (!saved->valid) {if (available<0)available=(int)i;continue;}
        if (saved->cmd.seq==cmd.seq && same_client(&from,&saved->client)) {
            if (!command_equal(&saved->cmd,&cmd)) immediate(g,&from,"sequence_conflict",&cmd);
            else {g->c.duplicate++;send_text(g,&from,saved->text);}
            return;
        }
    }
    if (g->pending) {g->c.busy++;immediate(g,&from,"busy",&cmd);return;}
    if (available<0) {immediate(g,&from,"cache_full",&cmd);return;}
    if (now<g->cooldown[cmd.unit]) { immediate(g,&from,"cooldown",&cmd); return; }
    if ((rd(STATUS)&91)!=64) { g->c.busy++; immediate(g,&from,"bus_not_idle",&cmd); return; }
    if (rd(AUTH)!=0) { immediate(g,&from,"ownership_error",&cmd); stopping=1; return; }
    g->up.normal_turn=0;
    g->cmd=cmd; g->client=from; g->slot=(unsigned)available; g->c.attempts++;
    g->tx_before=rd(TXCOUNT);
    for (unsigned i=0;i<5;i++) g->errors_before[i]=rd(error_offsets[i]);
    uint8_t *q=g->request;
    q[0]=(uint8_t)cmd.unit; q[1]=(uint8_t)cmd.fc;
    q[2]=(uint8_t)(cmd.address>>8);q[3]=(uint8_t)cmd.address;
    q[4]=(uint8_t)(cmd.arg>>8);q[5]=(uint8_t)cmd.arg;
    uint16_t crc=crc16(q,6);q[6]=(uint8_t)crc;q[7]=(uint8_t)(crc>>8);
    for(unsigned i=0;i<8;i+=4) {
        uint32_t word=(uint32_t)q[i]|((uint32_t)q[i+1]<<8)|((uint32_t)q[i+2]<<16)|((uint32_t)q[i+3]<<24);
        wr(0x200+i,word);
    }
    wr(TXLEN,8);
    if (rd(TXLEN)!=8) {
        g->sent=now_us(); g->c.tx_fail++; finish(g,"config_error",NULL,0); return;
    }
    g->sent=now_us(); g->deadline=g->sent+250000;
    g->tx_complete=false; g->pending=true;
    wr(AUTH,1); wr(FEED,0x4b49434b); wr(KICK,1);
}
static void device_step(struct gateway *g) {
    uint64_t now=now_us();
    uint32_t s=rd(STATUS);
    if (!g->pending) {
        if(g->up.enabled && g->up.active){if(s&1)rs_uplink_frame(g);return;}
        if (s&1) { wr(ACK,1); g->c.ignored++; }
        return;
    }
    if (!g->tx_complete) {
        uint32_t delta=rd(TXCOUNT)-g->tx_before;
        if (delta==1) { g->tx_complete=true; wr(AUTH,0); }
        else if (delta>1) { g->c.tx_fail++; finish(g,"unexpected_tx",NULL,0); return; }
    }
    if (now>=g->deadline) {
        g->c.timeout++;
        if (!g->tx_complete) g->c.tx_fail++;
        finish(g,g->tx_complete?"timeout":"tx_timeout",NULL,0);
        if (s&1) wr(ACK,1);
        return;
    }
    if (!g->tx_complete || !(s&1)) return;
    unsigned n=rd(RXLEN);
    uint8_t bytes[24]={0};
    unsigned expected=g->cmd.fc==6?8:5+2*g->cmd.arg;
    if(n>=4 && n<=256 && (rd(0x100)&255u)!=g->cmd.unit) {
        wr(ACK,1);g->c.ignored++;return;
    }
    if (n!=expected && n!=5) {
        wr(ACK,1); g->c.bad_reply++; finish(g,"bad_length",NULL,0); return;
    }
    for (unsigned i=0;i<n;i+=4) {
        uint32_t word=rd(0x100+i);
        for (unsigned j=0;j<4 && i+j<n;j++) bytes[i+j]=(uint8_t)(word>>(8*j));
    }
    wr(ACK,1);
    if (now_us()>=g->deadline) {
        g->c.timeout++; finish(g,"timeout",NULL,0); return;
    }
    /* A delayed frame for another node cannot satisfy this transaction. */
    if(bytes[0]!=g->cmd.unit) {g->c.ignored++;return;}
    bool exception=n==5 && bytes[1]==(g->cmd.fc|0x80) && bytes[2]!=0;
    bool matched=g->cmd.fc==6 ? (n==8 && !memcmp(bytes,g->request,8)) :
        (n==expected && bytes[1]==3 && bytes[2]==2*g->cmd.arg);
    if (crc16(bytes,n)!=0 || bytes[0]!=g->cmd.unit || !(exception||matched)) {
        g->c.bad_reply++; finish(g,"bad_reply",bytes,n); return;
    }
    for (unsigned i=0;i<5;i++) if (rd(error_offsets[i])!=g->errors_before[i]) {
        g->c.bad_reply++; finish(g,"pl_error",bytes,n); return;
    }
    if (exception) { g->c.exception++; finish(g,"modbus_exception",bytes,n); return; }
    g->c.ok++; finish(g,"ok",bytes,n);
}
static bool identity(void) {
    static const uint32_t checks[][2]={
        {0,0x32574749},{4,0x20000},{8,0x1f},{0x18,50000000},
        {0x20,5208},{0x24,1},{0x28,85932},{0x2c,200508},{AUTH,0}};
    for (unsigned i=0;i<sizeof(checks)/sizeof(checks[0]);i++) {
        uint32_t got=rd(checks[i][0]);
        if (got!=checks[i][1]) {
            fprintf(stderr,"RS485 preflight offset=0x%X got=0x%08"PRIX32" expected=0x%08"PRIX32"\n",
                checks[i][0],got,checks[i][1]); return false;
        }
    }
    return true;
}
int main(int argc,char **argv) {
    static struct gateway g;
    g.udp=-1;g.nodes=1;g.up.fd=-1;
    const char *bind_ip="0.0.0.0",*peer_ip="192.168.31.100";
    unsigned port=15001;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--help")) {
            puts("igw_rs485_rw [--bind IP] [--peer IP] [--port 15001] [--nodes 1..16] [--uplink]\nPinned R2 50MHz, base80400000, nodes1..N FC03 read0..31 count1..8; FC06 volatile16..31; IGW1 legacy and IGW2. Timeout250ms. Root required."); return 0;
        }
        if (!strcmp(argv[i],"--uplink")){g.up.enabled=1;continue;}
        if (i+1>=argc) return 2;
        const char *key=argv[i],*value=argv[++i];
        if (!strcmp(key,"--bind")) bind_ip=value;
        else if (!strcmp(key,"--peer")) peer_ip=value;
        else if (!strcmp(key,"--nodes")) {
            const char *p=value;uint32_t count;
            if(!decimal(&p,16,&count)||*p||!count)return 2;
            g.nodes=count;
        }
        else if (!strcmp(key,"--port")) {
            char *end; errno=0; unsigned long v=strtoul(value,&end,10);
            if (errno || !*value || *end || v<1 || v>65535) return 2;
            port=(unsigned)v;
        }
#ifdef IGW_RS485_TEST
        else if (!strcmp(key,"--test-fd")) test_fd=atoi(value);
        else if (!strcmp(key,"--test-lock")) lock_path=value;
        else if (!strcmp(key,"--test-control-fd")){g.up.fd=atoi(value);g.up.test=1;}
        else if (!strcmp(key,"--test-state"))rs_state_path=value;
#endif
        else return 2;
    }
    struct sockaddr_in local={0};
    local.sin_family=AF_INET;
    local.sin_port=htons((uint16_t)port);
    if (inet_pton(AF_INET,bind_ip,&local.sin_addr)!=1 || inet_pton(AF_INET,peer_ip,&g.peer)!=1) return 2;
    if (mkdir(lock_path,0700)) { perror("RS485 ownership lock (do not run once-script concurrently)"); return 2; }
    lock_owned=true; atexit(cleanup);
    struct sigaction sa={0}; sa.sa_handler=stop_signal; sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL); sigaction(SIGTERM,&sa,NULL);
#ifndef IGW_RS485_TEST
    int fd=open("/dev/mem",O_RDWR|O_SYNC);
    if (fd<0) { perror("/dev/mem"); return 2; }
    void *p=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t)0x80400000ULL);
    close(fd);
    if (p==MAP_FAILED) { perror("mmap RS485"); return 2; }
    mmio=p;
#endif
    if (!identity()) return 2;
    g.udp=socket(AF_INET,SOCK_DGRAM,0);
    if (g.udp<0 || bind(g.udp,(struct sockaddr *)&local,sizeof(local))) { perror("UDP bind"); return 2; }
    int flags=fcntl(g.udp,F_GETFL,0);
    if (flags<0 || fcntl(g.udp,F_SETFL,flags|O_NONBLOCK)) { perror("fcntl"); close(g.udp); return 2; }
    write_owned=true;
    wr(ACK,1);
    setvbuf(stdout,NULL,_IOLBF,0);
    if(g.up.enabled){
        int random=open("/dev/urandom",O_RDONLY);
        uint64_t owner_id;
        if(random<0||read(random,&g.up.nonce_seed,4)!=4||read(random,&owner_id,sizeof owner_id)!=(ssize_t)sizeof owner_id){perror("RS grant/owner seed");return 2;}close(random);
        snprintf(g.accounting_owner,sizeof g.accounting_owner,"%016"PRIx64,owner_id);
        if(g.up.fd<0)g.up.fd=rs_control_open(g.nodes);
        if(g.up.fd<0){perror("RS CAN coordination");return 2;}
        int f=fcntl(g.up.fd,F_GETFL,0);if(f<0||fcntl(g.up.fd,F_SETFL,f|O_NONBLOCK)){perror("RS control nonblock");return 2;}
        g.up.started=now_us()/1000;rs_snapshot_or_stop(&g,1);
        puts("[RS-UPLINK-READY] R17 CAN-coordinated device initiated RS485 uploads, host shadow R17; value travels on RS485 only");
    }
    printf("[READY] RS485-RW R17.1 %s:%u peer=%s nodes=%u FC03+FC06 9600 8E1; application RTT\n",bind_ip,port,peer_ip,g.nodes);
    int exit_status=0;
    while (!stopping) {
        rs_control_step(&g);
        device_step(&g);
        rs_maybe_grant(&g);
        struct pollfd pfd={0};
        pfd.fd=g.udp;
        pfd.events=(!g.up.enabled||(!g.up.active&&now_us()>=g.up.guard))?POLLIN:0;
        int ret=poll(&pfd,1,1);
        if (ret<0 && errno!=EINTR) { perror("poll"); exit_status=2; break; }
        if (!stopping && ret>0 && (pfd.revents&POLLIN)) udp_read(&g);
    }
    if(g.up.enabled&&g.up.active){
        /* Stop granting, request cancellation, continue receive/drain handshake. */
        uint64_t end=now_us()+1000000;g.up.until=0;
        while(g.up.active&&now_us()<end){rs_control_step(&g);device_step(&g);(void)poll(NULL,0,1);}
        if(g.up.active){
            fputs("REVIEW: RS transmitter quiescence unconfirmed; RS ownership lock retained. Stop/reset slave before recovery; do not delete an active lock.\n",stderr);
            lock_owned=false;g.up.failed=1;
        }
    }
    wr(AUTH,0);
    if(g.up.enabled){rs_snapshot_or_stop(&g,0);
        for(unsigned i=0;i<g.nodes;i++)printf("[RS-UPLINK-TOTAL] unit=%u accepted=%"PRIu64" duplicates=%"PRIu64" stale=%"PRIu64" invalid=%"PRIu64"\n",i+1,g.up.node[i].accepted,g.up.node[i].duplicates,g.up.node[i].stale,g.up.node[i].invalid);
        printf("[RS-UPLINK-SLOTS] timeout=%"PRIu64" bad_frames=%"PRIu64" control_submit_fail=%"PRIu64"\n",g.up.timeouts,g.up.bad,g.up.submit_fail);
        if(g.up.fd>=0)close(g.up.fd);
    }
    if(g.up.failed)exit_status=2;
    close(g.udp);
    puts("[STOP] RS485 gateway exited; TX authorization=0");
    return exit_status;
}
