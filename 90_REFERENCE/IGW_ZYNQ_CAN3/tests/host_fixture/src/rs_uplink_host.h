#ifndef IGW_RS_UPLINK_HOST_H
#define IGW_RS_UPLINK_HOST_H
/* Included inside the single RS485 MMIO owner, after rd/wr and CRC helpers. */
static int rs_control_open(unsigned nodes){
 int fd=socket(PF_CAN,SOCK_RAW|SOCK_NONBLOCK,CAN_RAW);if(fd<0)return -1;
 struct can_filter filters[32];for(unsigned i=0;i<nodes;i++){filters[2*i]=(struct can_filter){.can_id=RSU_RTS+i+1,.can_mask=CAN_SFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG};filters[2*i+1]=(struct can_filter){.can_id=RSU_DONE+i+1,.can_mask=CAN_SFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG};}
 struct sockaddr_can addr={.can_family=AF_CAN,.can_ifindex=(int)if_nametoindex("can0")};
 if(!addr.can_ifindex||setsockopt(fd,SOL_CAN_RAW,CAN_RAW_FILTER,filters,2*nodes*sizeof filters[0])||bind(fd,(struct sockaddr *)&addr,sizeof addr)){close(fd);return -1;}return fd;
}
static int rs_publish(struct gateway *g,int running){
 struct rs_up_host *u=&g->up;char tmp[512];
 if(snprintf(tmp,sizeof tmp,"%s.XXXXXX",rs_state_path)>=(int)sizeof tmp)return -1;
 int fd=mkstemp(tmp);if(fd<0)return -1;FILE *f=fdopen(fd,"w");if(!f){close(fd);unlink(tmp);return -1;}
 uint64_t now=now_us()/1000;
 fprintf(f,"{\"schema\":\"IGW_RS485_UPLINK_R17\",\"running\":%s,\"sampled_ms\":%"PRIu64",\"uptime_ms\":%"PRIu64",\"source\":\"device_counter_or_configured_value\",\"coordination\":\"CAN_RTS_GRANT_ACK\",\"register_space\":\"host_rs485_uplink_shadow\",\"slot_active\":%s,\"slot_unit\":%u,\"slot_timeouts\":%"PRIu64",\"bad_frames\":%"PRIu64",\"control_submit_fail\":%"PRIu64",\"devices\":[",running?"true":"false",now,now-u->started,u->active?"true":"false",u->unit,u->timeouts,u->bad,u->submit_fail);
 for(unsigned i=0;i<g->nodes;i++){
  const struct rs_up_node *n=u->node+i;
  fprintf(f,"%s{\"unit\":%u,\"valid\":%s,\"address\":17,\"value\":%u,\"sequence\":%u,\"epoch\":%"PRIu32",\"accepted\":%"PRIu64",\"duplicates\":%"PRIu64",\"stale\":%"PRIu64",\"invalid\":%"PRIu64",\"age_ms\":",i?",":"",i+1,n->valid?"true":"false",n->value,n->seq,n->epoch,n->accepted,n->duplicates,n->stale,n->invalid);
  if(n->valid)fprintf(f,"%"PRIu64,now-n->seen);else fputs("null",f);fputc('}',f);
 }
 fputs("]}",f);int failed=ferror(f);if(fclose(f))failed=1;
 if(failed||rename(tmp,rs_state_path)){unlink(tmp);return -1;}return 0;
}
static int rs_can_send(struct gateway *g,unsigned id,const uint8_t data[8]){
 struct can_frame f;memset(&f,0,sizeof f);f.can_id=id;f.can_dlc=8;memcpy(f.data,data,8);
 if(write(g->up.fd,&f,sizeof f)!=(ssize_t)sizeof f){g->up.submit_fail++;return -1;}return 0;
}
static void rs_send_ack(struct gateway *g,unsigned unit,const struct rs_up_node *n){
 uint8_t data[8];ul_put32(data,n->nonce);ul_put16(data+4,n->seq);data[6]=(uint8_t)(n->value>>8);data[7]=(uint8_t)n->value;
 (void)rs_can_send(g,RSU_ACK+unit,data);
}
static void rs_snapshot_or_stop(struct gateway *g,int running){
 if(rs_publish(g,running)){perror("RS uplink snapshot");g->up.failed=1;stopping=1;}
}
static void rs_control_step(struct gateway *g){
 struct rs_up_host *u=&g->up;if(!u->enabled)return;
 uint64_t now=now_us();
 if(u->fd<0){if(now<u->rebind)return;u->fd=rs_control_open(g->nodes);if(u->fd<0){u->rebind=now+250000;return;}}
 for(unsigned b=0;b<32;b++){
  struct can_frame f;ssize_t len=read(u->fd,&f,sizeof f);
  if(len<0){if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)break;
   if(u->test){u->failed=1;stopping=1;break;}
   close(u->fd);u->fd=-1;u->rebind=now+250000;break;}
  if(len!=(ssize_t)sizeof f||f.can_dlc!=8||(f.can_id&(CAN_EFF_FLAG|CAN_RTR_FLAG|CAN_ERR_FLAG)))continue;
  unsigned unit=f.can_id>RSU_RTS&&f.can_id<=RSU_RTS+g->nodes?f.can_id-RSU_RTS:f.can_id>RSU_DONE&&f.can_id<=RSU_DONE+g->nodes?f.can_id-RSU_DONE:0;if(!unit)continue;
  struct rs_up_node *n=u->node+unit-1;uint32_t epoch=ul_u32(f.data);uint16_t seq=ul_u16(f.data+4);
  if(f.can_id==RSU_DONE+unit){
   if(!epoch||!seq||ul_u16(f.data+6)!=RSU_DONE_MAGIC){n->invalid++;continue;}
   if(u->active&&u->unit==unit&&u->nonce==epoch&&u->seq==seq){
    /* Explicit transmitter quiescence, never the passage of time, releases RS ownership. */
    if(!u->done_received){u->done_received=1;u->done_at=now;}
   }
   if(n->released_nonce==epoch&&n->released_seq==seq){uint8_t a[8];memcpy(a,f.data,8);ul_put16(a+6,RSU_RELEASED_MAGIC);(void)rs_can_send(g,RSU_RELEASED+unit,a);}
   continue;
  }
  if(!epoch||!seq||ul_u16(f.data+6)!=RSU_RTS_MAGIC){n->invalid++;continue;}
  if(n->valid&&n->epoch==epoch&&seq==n->seq){n->duplicates++;rs_snapshot_or_stop(g,1);if(!stopping)rs_send_ack(g,unit,n);continue;}
  if(n->valid&&n->epoch==epoch&&seq<n->seq){n->stale++;continue;}
  if(u->active&&u->unit==unit&&u->epoch==epoch&&u->seq==seq)continue;
  n->waiting=1;n->want_epoch=epoch;n->want_seq=seq;n->requested=now;
 }
 if(u->active&&u->done_received&&now>=u->done_at+20000&&(rd(STATUS)&91)==64){
  struct rs_up_node *n=u->node+u->unit-1;n->released_nonce=u->nonce;n->released_seq=u->seq;
  uint8_t a[8];ul_put32(a,u->nonce);ul_put16(a+4,u->seq);ul_put16(a+6,RSU_RELEASED_MAGIC);
  (void)rs_can_send(g,RSU_RELEASED+u->unit,a);u->active=0;u->guard=now+10000;u->normal_turn=1;
 }
 if(u->active&&now>=u->until){
  if(!u->timed_out){u->timeouts++;u->timed_out=1;}
  if(now>=u->cancel_due){uint8_t a[8];ul_put32(a,u->nonce);ul_put16(a+4,u->seq);ul_put16(a+6,RSU_CANCEL_MAGIC);(void)rs_can_send(g,RSU_CANCEL+u->unit,a);u->cancel_due=now+250000;}
  /* Failed CAN coordination freezes RS485 grants/polling rather than colliding. */
 }
 if(now>=u->publish_due){rs_snapshot_or_stop(g,1);u->publish_due=now+1000000;}
}
static void rs_maybe_grant(struct gateway *g){
 struct rs_up_host *u=&g->up;if(!u->enabled||u->fd<0||u->active||g->pending||stopping)return;
 uint64_t now=now_us();if(now<u->guard)return;
 /* Give a queued ordinary request a turn between grants; idle has no delay. */
 if(u->normal_turn){struct pollfd p={g->udp,POLLIN,0};if(poll(&p,1,0)>0&&(p.revents&POLLIN))return;u->normal_turn=0;}
 if((rd(STATUS)&91)!=64||rd(AUTH)!=0)return;
 for(unsigned b=0;b<g->nodes;b++){
  unsigned i=u->next;u->next=(u->next+1)%g->nodes;struct rs_up_node *n=u->node+i;
  if(!n->waiting)continue;
  n->waiting=0;if(now-n->requested>1000000)continue;
  uint8_t data[8];uint32_t nonce=ul_epoch(&u->nonce_seed);ul_put32(data,nonce);ul_put32(data+4,n->want_epoch);
  /* Reserve before submission. Failed send still reserves a full lease: a
   * kernel may have queued it even when caller cannot establish delivery. */
  u->active=1;u->done_received=0;u->timed_out=0;u->cancel_due=0;u->unit=i+1;u->epoch=n->want_epoch;u->seq=n->want_seq;u->nonce=nonce;u->until=now+RSU_LEASE_MS*1000ULL;
  for(unsigned j=0;j<5;j++)u->errors[j]=rd(error_offsets[j]);
  (void)rs_can_send(g,RSU_GRANT+i+1,data);break;
 }
}
static void rs_uplink_frame(struct gateway *g){
 struct rs_up_host *u=&g->up;unsigned len=rd(RXLEN);uint8_t data[RSU_BYTES]={0};
 if(len==RSU_BYTES){for(unsigned i=0;i<RSU_BYTES;i+=4){uint32_t v=rd(0x100+i);for(unsigned j=0;j<4;j++)data[i+j]=(uint8_t)(v>>(8*j));}}
 wr(ACK,1);
 int matched=len==RSU_BYTES&&data[0]==u->unit&&data[1]==RSU_FC&&rsu_crc(data,len)==0&&ul_u32(data+2)==u->nonce&&ul_u32(data+6)==u->epoch&&ul_u16(data+10)==u->seq;
 for(unsigned j=0;j<5;j++)if(rd(error_offsets[j])!=u->errors[j])matched=0;
 if(!matched||now_us()>=u->until){u->bad++;return;}
 struct rs_up_node *n=u->node+u->unit-1;
 uint16_t value=(uint16_t)((unsigned)data[12]<<8|data[13]);
 if(n->valid&&n->epoch==u->epoch&&n->seq==u->seq){
  if(n->value!=value){n->invalid++;return;}n->duplicates++;
 }else{n->epoch=u->epoch;n->seq=u->seq;n->value=value;n->valid=1;n->accepted++;n->seen=now_us()/1000;}
 u->accounted_good++; /* One increment per validated physical RS upload ADU, including duplicates. */
 n->nonce=u->nonce;
 rs_snapshot_or_stop(g,1); /* Atomic host shadow publication precedes ACK. */
 if(!stopping){rs_send_ack(g,u->unit,n);printf("[RS-UPLINK] unit=%u host_register=17 value=%u sequence=%u accepted=%"PRIu64" duplicates=%"PRIu64"\n",u->unit,n->value,n->seq,n->accepted,n->duplicates);}
 /* Keep exclusive ownership until device DONE confirms TX idle, even if
  * DATA was accepted or the nominal deadline expired. */
}
#endif
