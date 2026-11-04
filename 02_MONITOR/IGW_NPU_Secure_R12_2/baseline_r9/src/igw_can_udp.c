/* RW_R9 private CAN business protocol + legacy bring-up. Dedicated interface. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Linux UAPI hardware type; older SDK glibc net/if_arp.h omits CAN. */
#ifndef ARPHRD_CAN
#define ARPHRD_CAN 280
#endif

static volatile sig_atomic_t stopping;
#ifdef IGW_TEST_TRANSPORT
static int test_can_fd = -1;
#endif
struct counters {
    uint64_t attempts, ok, timeout, tx_fail, mismatch, ignored, busy;
    uint64_t resets, reset_fail, invalid, udp_fail, duplicate;
    uint64_t rtt_min, rtt_max, rtt_sum;
};
#define CACHE_SIZE 1024u
struct command {uint32_t seq; uint16_t value; uint8_t op,address; bool legacy;};
struct cached_result {
    bool used,ready; uint64_t until; struct command cmd;
    struct sockaddr_in client; char text[1536];
};
static bool same_command(const struct command *a,const struct command *b) {
    return a->seq==b->seq && a->value==b->value && a->op==b->op &&
           a->address==b->address && a->legacy==b->legacy;
}
struct gateway {
    int udp_fd, can_fd;
    char can_name[IFNAMSIZ];
    struct in_addr peer_ip;
    uint64_t timeout_us, cooldown_until, started, sent_at, deadline;
    struct command cmd;
    unsigned active_slot;
    struct cached_result results[CACHE_SIZE];
    uint32_t seq;
    bool pending, bad_seen, faulted;
    struct sockaddr_in client;
    struct counters c;
};
static uint64_t now_us(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock_gettime"); exit(2); }
    return (uint64_t)t.tv_sec * 1000000 + (uint64_t)t.tv_nsec / 1000;
}
static void on_signal(int sig) { (void)sig; stopping=1; }
static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put_le32(uint8_t *p, uint32_t n) {
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8));
}
static bool same_client(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr==b->sin_addr.s_addr && a->sin_port==b->sin_port;
}
static int set_nonblock(int fd) {
    int n=fcntl(fd,F_GETFL,0);
    return n<0 ? -1 : fcntl(fd,F_SETFL,n|O_NONBLOCK);
}
static int can_open(struct gateway *g) {
#ifdef IGW_TEST_TRANSPORT
    int fd=dup(test_can_fd);
    if (fd>=0 && set_nonblock(fd)) { close(fd); return -1; }
    (void)g;
    return fd;
#else
    int fd=socket(PF_CAN,SOCK_RAW,CAN_RAW);
    if (fd<0) return -1;
    struct ifreq ifr={0};
    memcpy(ifr.ifr_name,g->can_name,strlen(g->can_name)+1);
    if (ioctl(fd,SIOCGIFHWADDR,&ifr) || ifr.ifr_hwaddr.sa_family!=ARPHRD_CAN) goto fail;
    if (ioctl(fd,SIOCGIFFLAGS,&ifr) || !(ifr.ifr_flags&IFF_UP)) goto fail;
    if (ioctl(fd,SIOCGIFINDEX,&ifr)) goto fail;
    struct sockaddr_can addr={0};
    addr.can_family=AF_CAN;
    addr.can_ifindex=ifr.ifr_ifindex;
    struct can_filter filter[2]={
        {.can_id=0x181,.can_mask=CAN_SFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG},
        {.can_id=0x191,.can_mask=CAN_SFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG}};
    if (setsockopt(fd,SOL_CAN_RAW,CAN_RAW_FILTER,filter,sizeof(filter)) ||
        bind(fd,(struct sockaddr *)&addr,sizeof(addr)) || set_nonblock(fd)) goto fail;
    return fd;
fail:
    close(fd);
    return -1;
#endif
}
/* A userspace socket timeout does not cancel hardware retransmission.  On this
 * dedicated test interface, down/up cancels the single outstanding PTB request.
 * This needs root/CAP_NET_ADMIN and must not be shared with other CAN senders. */
static int cancel_and_reopen(struct gateway *g) {
    if (g->can_fd>=0) close(g->can_fd);
    g->can_fd=-1;
    g->c.resets++;
#ifndef IGW_TEST_TRANSPORT
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    if (fd<0) goto fail;
    struct ifreq ifr={0};
    memcpy(ifr.ifr_name,g->can_name,strlen(g->can_name)+1);
    if (ioctl(fd,SIOCGIFFLAGS,&ifr)) { close(fd); goto fail; }
    short original=ifr.ifr_flags;
    ifr.ifr_flags=(short)(original&~IFF_UP);
    if (ioctl(fd,SIOCSIFFLAGS,&ifr)) { close(fd); goto fail; }
    ifr.ifr_flags=(short)(original|IFF_UP);
    if (ioctl(fd,SIOCSIFFLAGS,&ifr)) { close(fd); goto fail; }
    close(fd);
#endif
    g->can_fd=can_open(g);
    if (g->can_fd>=0) { g->faulted=false; return 0; }
#ifndef IGW_TEST_TRANSPORT
fail:
#endif
    g->c.reset_fail++;
    g->faulted=true;
    perror("CAN recovery failed; new CAN requests disabled");
    return -1;
}
static void send_text(struct gateway *g, const struct sockaddr_in *to, const char *s) {
    if (sendto(g->udp_fd,s,strlen(s),0,(const struct sockaddr *)to,sizeof(*to))<0)
        g->c.udp_fail++;
}
static void make_result(struct gateway *g, char *out, size_t cap,
                        const char *type, const char *status, uint32_t seq,
                        uint64_t rtt, const struct can_frame *frame) {
    char hex[17]="";
    if (frame) for (unsigned i=0;i<8;i++) snprintf(hex+i*2,3,"%02X",frame->data[i]);
    snprintf(out,cap,
        "{\"v\":1,\"type\":\"%s\",\"status\":\"%s\",\"seq\":%"PRIu32
        ",\"data\":\"%s\",\"can_rtt_us\":%"PRIu64",\"uptime_ms\":%"PRIu64
        ",\"stats\":{\"attempts\":%"PRIu64",\"ok\":%"PRIu64
        ",\"timeout\":%"PRIu64",\"tx_fail\":%"PRIu64",\"mismatch\":%"PRIu64
        ",\"ignored\":%"PRIu64",\"busy\":%"PRIu64",\"resets\":%"PRIu64
        ",\"reset_fail\":%"PRIu64",\"invalid\":%"PRIu64",\"udp_fail\":%"PRIu64
        ",\"duplicate\":%"PRIu64",\"rtt_min_us\":%"PRIu64
        ",\"rtt_avg_us\":%"PRIu64",\"rtt_max_us\":%"PRIu64"}}",
        type,status,seq,hex,rtt,(now_us()-g->started)/1000,
        g->c.attempts,g->c.ok,g->c.timeout,g->c.tx_fail,g->c.mismatch,
        g->c.ignored,g->c.busy,g->c.resets,g->c.reset_fail,g->c.invalid,g->c.udp_fail,
        g->c.duplicate,g->c.rtt_min,g->c.ok?g->c.rtt_sum/g->c.ok:0,g->c.rtt_max);
    if (!g->cmd.legacy) {
        char stats[1024], request[17], values[32]; uint8_t payload[8]={0};
        const char *start=strstr(out,"\"stats\":");
        snprintf(stats,sizeof stats,"%s",start?start:"\"stats\":{}}");
        put_le32(payload,g->cmd.seq);payload[4]=g->cmd.op;payload[5]=g->cmd.address;
        payload[6]=(uint8_t)(g->cmd.value>>8);payload[7]=(uint8_t)g->cmd.value;
        for(unsigned i=0;i<8;i++)snprintf(request+2*i,3,"%02X",payload[i]);
        if(frame && !strcmp(status,"ok") && g->cmd.op==1)
            snprintf(values,sizeof values,"[%u]",((unsigned)frame->data[6]<<8)|frame->data[7]);
        else strcpy(values,"[]");
        snprintf(out,cap,"{\"v\":2,\"type\":\"can_rw_reply\",\"status\":\"%s\",\"seq\":%"PRIu32
            ",\"op\":\"%s\",\"unit\":1,\"address\":%u,\"count\":1,\"value\":%u,"
            "\"execution\":\"%s\",\"request_can_id\":529,\"reply_can_id\":401,"
            "\"request_data\":\"%s\",\"data\":\"%s\",\"registers\":%s,\"can_rtt_us\":%"PRIu64",%s",
            status,seq,g->cmd.op==1?"READ":"WRITE",g->cmd.address,g->cmd.value,
            !strcmp(status,"ok")?(g->cmd.op==1?"read":"acknowledged"):
            !strcmp(status,"rejected")?"rejected":"unknown",request,hex,values,rtt,stats);
    }
}
static void immediate_cmd(struct gateway *g,const struct sockaddr_in *to,
        const char *type,const char *status,const struct command *cmd) {
    char out[1536];struct command saved=g->cmd;g->cmd=*cmd;
    make_result(g,out,sizeof out,type,status,cmd->seq,0,NULL);g->cmd=saved;
    send_text(g,to,out);
}
static void finish(struct gateway *g,const char *status,uint64_t rtt,
                   const struct can_frame *frame) {
    struct cached_result *e=&g->results[g->active_slot];
    make_result(g,e->text,sizeof e->text,"can_reply",status,g->seq,rtt,frame);
    e->ready=true;e->until=now_us()+60000000;
    send_text(g,&g->client,e->text);g->pending=false;
    printf("[GW] seq=%"PRIu32" status=%s can_rtt_us=%"PRIu64
           " attempts=%"PRIu64" ok=%"PRIu64" timeout=%"PRIu64"\n",
           g->seq,status,rtt,g->c.attempts,g->c.ok,g->c.timeout);
}
static bool parse_read(const char *s, uint32_t *seq) {
    if (strncmp(s,"IGW1 READ ",10)) return false;
    const char *p=s+10;
    if (*p<'0'||*p>'9') return false;
    errno=0;
    char *end;
    unsigned long v=strtoul(p,&end,10);
    if (errno || v>UINT32_MAX || *end) return false;
    *seq=(uint32_t)v;
    return true;
}
static bool number_field(const char **cursor,uint32_t maximum,uint32_t *out,bool last) {
    const char *p=*cursor;uint32_t v=0;
    if(*p<'0'||*p>'9')return false;
    do {unsigned d=(unsigned)(*p-'0');if(v>maximum/10 || (v==maximum/10 && d>maximum%10))return false;
        v=v*10+d;p++;}while(*p>='0'&&*p<='9');
    if(last ? *p!=0 : *p!=' ')return false;
    *out=v;*cursor=p+(last?0:1);return true;
}
static bool parse_command(const char *s,struct command *cmd) {
    memset(cmd,0,sizeof *cmd);
    if(parse_read(s,&cmd->seq)) {cmd->legacy=true;return true;}
    const char *p;uint32_t unit,address,value=0;
    if(!strncmp(s,"IGW2 CAN READ ",14)) {cmd->op=1;p=s+14;}
    else if(!strncmp(s,"IGW2 CAN WRITE ",15)) {cmd->op=2;p=s+15;}
    else return false;
    if(!number_field(&p,UINT32_MAX,&cmd->seq,false) || !number_field(&p,1,&unit,false) || unit!=1 ||
       !number_field(&p,31,&address,cmd->op==1))return false;
    if(cmd->op==2 && (address<16 || !number_field(&p,65535,&value,true)))return false;
    cmd->address=(uint8_t)address;cmd->value=(uint16_t)value;return true;
}
static void read_udp(struct gateway *g) {
    char buf[128];struct sockaddr_in from={0};socklen_t len=sizeof from;
    ssize_t n=recvfrom(g->udp_fd,buf,sizeof buf-1,MSG_TRUNC,(struct sockaddr *)&from,&len);
    if(n<0 || from.sin_addr.s_addr!=g->peer_ip.s_addr)return;
    struct command cmd={.legacy=true};
    if(n>=(ssize_t)sizeof buf-1 || memchr(buf,0,(size_t)n)) {
        g->c.invalid++;immediate_cmd(g,&from,"error","bad_request",&cmd);return;
    }
    buf[n]=0;
    if(!strcmp(buf,"IGW1 STATS")) {
        immediate_cmd(g,&from,"stats",g->faulted?"recovery_failed":"ok",&cmd);return;
    }
    if(!parse_command(buf,&cmd)) {
        cmd.legacy=true;g->c.invalid++;immediate_cmd(g,&from,"error","bad_request",&cmd);return;
    }
    uint64_t now=now_us();int free_slot=-1;
    for(unsigned i=0;i<CACHE_SIZE;i++) {
        struct cached_result *e=&g->results[i];
        if(e->used && e->ready && now>=e->until)e->used=false;
        if(!e->used) {if(free_slot<0)free_slot=(int)i;continue;}
        if(e->cmd.seq!=cmd.seq || !same_client(&from,&e->client))continue;
        if(!same_command(&e->cmd,&cmd))immediate_cmd(g,&from,"can_reply","sequence_conflict",&cmd);
        else {g->c.duplicate++;if(e->ready)send_text(g,&from,e->text);}
        return;
    }
    if(g->pending) {g->c.busy++;immediate_cmd(g,&from,"can_reply","busy",&cmd);return;}
    if(g->faulted) {immediate_cmd(g,&from,"can_reply","recovery_failed",&cmd);return;}
    if(now<g->cooldown_until) {immediate_cmd(g,&from,"can_reply","cooldown",&cmd);return;}
    if(free_slot<0) {immediate_cmd(g,&from,"can_reply","cache_full",&cmd);return;}
    g->active_slot=(unsigned)free_slot;
    struct cached_result *e=&g->results[g->active_slot];
    *e=(struct cached_result){.used=true,.cmd=cmd,.client=from};
    g->c.attempts++;g->client=from;g->cmd=cmd;g->seq=cmd.seq;g->bad_seen=false;
    struct can_frame f={0};f.can_id=cmd.legacy?0x201:0x211;f.can_dlc=8;
    put_le32(f.data,cmd.seq);
    if(cmd.legacy)put_le32(f.data+4,0x43414e00U^cmd.seq);
    else {f.data[4]=cmd.op;f.data[5]=cmd.address;f.data[6]=(uint8_t)(cmd.value>>8);f.data[7]=(uint8_t)cmd.value;}
    g->sent_at=now_us();
    if(write(g->can_fd,&f,sizeof f)!=(ssize_t)sizeof f) {
        g->c.tx_fail++;finish(g,"send_failed",0,NULL);return;
    }
    g->pending=true;g->deadline=g->sent_at+g->timeout_us;
}
static void read_can(struct gateway *g) {
    for (unsigned i=0;i<64;i++) {
        struct can_frame f;
        ssize_t n=read(g->can_fd,&f,sizeof(f));
        if (n<0) {
            if (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)
                g->faulted=true;
            return;
        }
        if (n!=(ssize_t)sizeof(f) || (f.can_id!=0x181 && f.can_id!=0x191) || f.can_dlc!=8) {
            g->c.ignored++; continue;
        }
        if (!g->pending || get_le32(f.data)!=g->seq) { g->c.ignored++; continue; }
        if (now_us()>g->deadline) { g->c.ignored++; continue; }
        if (f.can_id!=(g->cmd.legacy?0x181u:0x191u)) {g->c.ignored++;continue;}
        if(!g->cmd.legacy && f.data[5]==g->cmd.address && f.data[4]>=0xe1 && f.data[4]<=0xe5 && !f.data[6] && !f.data[7]) {
            g->c.mismatch++;finish(g,"rejected",now_us()-g->sent_at,&f);continue;
        }
        if (g->cmd.legacy ? get_le32(f.data+4)!=(0x52535000U^g->seq) :
            (f.data[4]!=(uint8_t)(0x80|g->cmd.op) || f.data[5]!=g->cmd.address ||
             (g->cmd.op==2 && (((unsigned)f.data[6]<<8)|f.data[7])!=g->cmd.value))) {
            g->c.mismatch++; g->bad_seen=true; continue;
        }
        uint64_t rtt=now_us()-g->sent_at;
        g->c.ok++;
        if (g->c.ok==1 || rtt<g->c.rtt_min) g->c.rtt_min=rtt;
        if (rtt>g->c.rtt_max) g->c.rtt_max=rtt;
        g->c.rtt_sum+=rtt;
        finish(g,"ok",rtt,&f);
    }
}
static unsigned parse_number(const char *s,unsigned min,unsigned max) {
    char *end;
    errno=0;
    unsigned long n=strtoul(s,&end,10);
    if (errno || !*s || *end || n<min || n>max) { fprintf(stderr,"Invalid number: %s\n",s); exit(2); }
    return (unsigned)n;
}
int main(int argc,char **argv) {
    static struct gateway g;
    g.udp_fd=-1;
    g.can_fd=-1;
    g.timeout_us=250000;
    strcpy(g.can_name,"can0");
    const char *bind_ip="192.168.31.222", *peer="192.168.31.100";
    unsigned port=15000;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--help")) {
            puts("igw_can_udp [--can can0] [--bind 192.168.31.222] [--peer 192.168.31.100] [--port 15000] [--timeout-ms 250]\nTimeout cancels the pending request by down/up of the dedicated CAN interface. Run as root. Ctrl+C stops.");
            return 0;
        }
        if (i+1>=argc) { fprintf(stderr,"Missing option value\n"); return 2; }
        const char *key=argv[i], *value=argv[++i];
        if (!strcmp(key,"--can")) {
            if (!*value || strlen(value)>=sizeof(g.can_name)) return 2;
            strcpy(g.can_name,value);
        } else if (!strcmp(key,"--bind")) bind_ip=value;
        else if (!strcmp(key,"--peer")) peer=value;
        else if (!strcmp(key,"--port")) port=parse_number(value,1,65535);
        else if (!strcmp(key,"--timeout-ms")) g.timeout_us=(uint64_t)parse_number(value,20,5000)*1000;
#ifdef IGW_TEST_TRANSPORT
        else if (!strcmp(key,"--test-can-fd")) test_can_fd=(int)parse_number(value,0,INT_MAX);
#endif
        else { fprintf(stderr,"Unknown option %s\n",key); return 2; }
    }
#ifndef IGW_TEST_TRANSPORT
    if (geteuid()!=0) { fprintf(stderr,"Run as root: timeout recovery needs CAN interface control\n"); return 2; }
#endif
    struct sockaddr_in local={0};
    local.sin_family=AF_INET;
    local.sin_port=htons((uint16_t)port);
    if (inet_pton(AF_INET,bind_ip,&local.sin_addr)!=1 || inet_pton(AF_INET,peer,&g.peer_ip)!=1) return 2;
    g.started=now_us();
    g.can_fd=can_open(&g);
    if (g.can_fd<0) { perror("Open CAN failed: configure and bring CAN up first"); return 2; }
    g.udp_fd=socket(AF_INET,SOCK_DGRAM,0);
    if (g.udp_fd<0 || bind(g.udp_fd,(struct sockaddr *)&local,sizeof(local)) || set_nonblock(g.udp_fd)) {
        perror("UDP bind"); close(g.can_fd); return 2;
    }
    struct sigaction sa={0};
    sa.sa_handler=on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL);
    sigaction(SIGTERM,&sa,NULL);
    setvbuf(stdout,NULL,_IOLBF,0);
    printf("[IGW-LINUX] CAN-RW R9 can=%s UDP=%s:%u peer=%s timeout=%"PRIu64"ms\n",
           g.can_name,bind_ip,port,peer,g.timeout_us/1000);
    puts("[READY] legacy 0x201/0x181; RW_R9 node1 0x211/0x191; dedicated CAN interface; no automatic write retry; RTT is not forwarding latency");
    while (!stopping) {
        uint64_t now=now_us();
        if (g.pending && now>=g.deadline) {
            g.c.timeout++;
            int ret=cancel_and_reopen(&g);
            g.cooldown_until=now_us()+2000000;
            finish(&g,ret ? "recovery_failed" : g.bad_seen ? "bad_reply" : "timeout",0,NULL);
        }
        struct pollfd p[2]={{g.udp_fd,POLLIN,0},{g.can_fd,POLLIN,0}};
        uint64_t remaining_now=now_us();
        int wait_ms=g.pending ? (int)((g.deadline>remaining_now ? g.deadline-remaining_now : 0)/1000) : 100;
        if (wait_ms>100) wait_ms=100;
        if (poll(p,2,wait_ms)<0) { if (errno==EINTR) continue; perror("poll"); break; }
        if (p[1].revents&POLLIN) read_can(&g);
        if (p[0].revents&POLLIN) read_udp(&g);
        if (p[1].revents&(POLLERR|POLLHUP|POLLNVAL)) g.faulted=true;
    }
    if (g.pending) (void)cancel_and_reopen(&g);
    if (g.can_fd>=0) close(g.can_fd);
    close(g.udp_fd);
    puts("[STOP] gateway exited");
    return 0;
}
