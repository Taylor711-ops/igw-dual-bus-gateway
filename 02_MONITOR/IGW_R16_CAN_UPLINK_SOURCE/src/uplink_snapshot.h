#ifndef IGW_UPLINK_SNAPSHOT_H
#define IGW_UPLINK_SNAPSHOT_H
#ifndef IGW_UPLINK_PATH
#define IGW_UPLINK_PATH "/tmp/igw_r16_uplink.json"
#endif
/* Same-root, atomic, bounded volatile diagnostic snapshot; no browser path input. */
static void uplink_json(FILE *out){
    char raw[8192];size_t used=0;struct stat st;struct jd d;
    int fd=open(IGW_UPLINK_PATH,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)goto unavailable;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_size<=0||st.st_size>=(off_t)sizeof raw){close(fd);goto unavailable;}
    while(used<sizeof raw-1){ssize_t n=read(fd,raw+used,sizeof raw-1-used);if(n<0&&errno==EINTR)continue;if(n<0){close(fd);goto unavailable;}if(!n)break;used+=(size_t)n;}
    close(fd);raw[used]=0;
    if(memchr(raw,0,used)||jparse(&d,raw,used)||!jeq(&d,jget(&d,0,"schema"),"IGW_CAN_UPLINK_R16"))goto unavailable;
    int running=jget(&d,0,"running");uint64_t sampled;
    int available=!jfield(&d,0,"sampled_ms",&sampled)&&sampled<=now_ms()&&now_ms()-sampled<=3000&&
        running>=0&&d.t[running].type=='b'&&d.t[running].end-d.t[running].start==4&&
        !memcmp(raw+d.t[running].start,"true",4);
    fprintf(out,",\"uplink\":{\"available\":%s,\"snapshot\":%s}",available?"true":"false",raw);return;
unavailable:fputs(",\"uplink\":{\"available\":false,\"snapshot\":null}",out);
}
#endif
