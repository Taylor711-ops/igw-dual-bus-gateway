#ifndef IGW_RS3_UPLINK_HOST_H
#define IGW_RS3_UPLINK_HOST_H
/* Device 3 has RS485 only. A private one-shot grant authorizes one FC46
 * event; it is not an unscheduled request from the slave. The same process
 * owns FC03/FC06, R17 slots, and this slot. A timeout never proves the other
 * transmitter is idle, so it leaves the bus reserved. */
static void rs3_step(struct gateway *g){
 struct rs3_slot *s=&g->rs3;struct rs_up_host *u=&g->up;
 if(!s->enabled||!s->active)return;
 uint64_t now=now_us();
 if(!s->tx_complete){
  uint32_t delta=rd(TXCOUNT)-s->tx_before;
  if(delta==1){s->tx_complete=1;s->host_grant_tx++;wr(AUTH,0);}
  else if(delta>1){u->submit_fail++;wr(AUTH,0);stopping=1;return;}
 }
 if(now>=s->until&&!s->timed_out){s->timed_out=1;u->timeouts++;rs_snapshot_or_stop(g,1);}
 if(!(rd(STATUS)&1))return;
 unsigned len=rd(RXLEN);uint8_t p[12]={0};
 if(len==sizeof p){for(unsigned i=0;i<sizeof p;i+=4){uint32_t w=rd(0x100+i);for(unsigned j=0;j<4;j++)p[i+j]=(uint8_t)(w>>(8*j));}}
 wr(ACK,1);
 bool matched=s->tx_complete&&len==sizeof p&&p[0]==3&&p[1]==0x46&&
   crc16(p,len)==0&&ul_u32(p+2)==s->nonce;
 if(!matched){u->bad++;rs_snapshot_or_stop(g,1);return;}
 /* Matching nonce and a complete CRC-checked event prove this one-shot
  * transmission finished; even a late event may release the slot, but it
  * must not be reported as a successful timely upload. */
 bool clean=!s->timed_out;
 for(unsigned j=0;j<5;j++)if(rd(error_offsets[j])!=s->errors[j])clean=false;
 u->accounted_good++;
 if(clean){
  struct rs_up_node *n=&u->node[2];n->epoch=s->epoch;
  n->seq=(uint16_t)((p[6]<<8)|p[7]);n->value=(uint16_t)((p[8]<<8)|p[9]);
  n->valid=1;n->accepted++;n->seen=now/1000;
  printf("[RS3-UPLINK] granted slot unit=3 value=%u sequence=%u accepted=%"PRIu64"\n",n->value,n->seq,n->accepted);
 }else u->bad++;
 s->active=0;s->next_due=now+2000000;u->guard=now+10000;u->normal_turn=1;
 rs_snapshot_or_stop(g,1);
}
static void rs3_maybe_grant(struct gateway *g){
 struct rs3_slot *s=&g->rs3;struct rs_up_host *u=&g->up;
 if(!s->enabled||!u->enabled||u->fd<0||s->active||u->active||g->pending||stopping||
    !s->identity||!s->capable||(s->mode!=0xa526&&s->mode!=0xa527))return;
 uint64_t now=now_us();if(now<s->next_due||now<u->guard)return;
 /* One grant at most every two seconds, even under sustained node1/2
  * activity; no in-flight transaction may be interrupted. */
 if((rd(STATUS)&91)!=64||rd(AUTH)!=0)return;
 uint8_t p[8]={3,0x45};s->nonce=ul_epoch(&u->nonce_seed);ul_put32(p+2,s->nonce);
 uint16_t crc=crc16(p,6);p[6]=(uint8_t)crc;p[7]=(uint8_t)(crc>>8);
 for(unsigned i=0;i<8;i+=4){uint32_t w=(uint32_t)p[i]|((uint32_t)p[i+1]<<8)|((uint32_t)p[i+2]<<16)|((uint32_t)p[i+3]<<24);wr(0x200+i,w);}
 wr(TXLEN,8);if(rd(TXLEN)!=8){u->submit_fail++;stopping=1;return;}
 s->tx_before=rd(TXCOUNT);for(unsigned j=0;j<5;j++)s->errors[j]=rd(error_offsets[j]);
 s->active=1;s->tx_complete=0;s->timed_out=0;s->until=now+250000;
 wr(AUTH,1);wr(FEED,0x4b49434b);wr(KICK,1);
}
#endif
