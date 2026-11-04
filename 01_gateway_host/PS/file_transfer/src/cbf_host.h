#ifndef CBF_HOST_H
#define CBF_HOST_H
#include "cbf_protocol.h"
/* Adapter functions provided by the Linux client/test harness. */
static int wire_send(unsigned data,const uint8_t p[8]);
static int wire_receive(unsigned *data,uint8_t p[8],uint64_t end_us);
static uint64_t host_us(void);
static int host_stopped(void);
static unsigned wire_ignored,link_retries;
static uint32_t message_id;
static unsigned message_count;
static int cbh_control(unsigned type,unsigned index,uint8_t out[8]){cbf_control(out,type,message_id,index,0);return wire_send(0,out);}
static int cbh_wait(unsigned type,uint8_t out[8],uint64_t end){
 while(!host_stopped()&&host_us()<end){unsigned data;int ret=wire_receive(&data,out,end);if(ret<0)return -1;if(!ret)break;
  if(data||rsf_u32(out+1)!=message_id){wire_ignored++;continue;}
  if(out[0]==CBF_ERROR){fprintf(stderr,"CAN_BLOCK_REMOTE_ERROR code=%u\n",out[7]);return -1;}
  if(out[0]!=type){wire_ignored++;continue;}return 1;
 }return 0;
}
static int cbh_send(const uint8_t *packet,unsigned size){
 if(size<RSF_HEAD+2||size>CBF_PACKET)return -1;
 if(++message_count>65535u){fputs("CAN_BLOCK_MESSAGE_BUDGET_EXHAUSTED\n",stderr);return -1;}
 if(!++message_id)message_id=1;
 uint64_t ack_budget=(packet[2]==RSF_COMMIT||packet[2]==RSF_GENERATE)?30000000:600000;
 unsigned total=cbf_fragments(size),next=0;uint8_t wire[8],answer[8];int ready=0;
 for(unsigned attempt=0;attempt<5&&!host_stopped();attempt++){
  if(attempt)link_retries++;
  memset(wire,0,8);wire[0]=CBF_BEGIN;rsf_p32(wire+1,message_id);wire[5]=(uint8_t)size;wire[6]=packet[size-2];wire[7]=packet[size-1];
  if(wire_send(0,wire))return -1;
  int got=cbh_wait(CBF_ACK,answer,host_us()+ack_budget);if(got<0)return -1;
  if(got&&answer[7]==CBF_WINDOW&&(next=rsf_u16(answer+5))<=total){ready=1;break;}
 }
 if(!ready)return -1;
 unsigned stalled=0;
 while(next<total&&!host_stopped()){
  unsigned end=(next/CBF_WINDOW+1)*CBF_WINDOW;if(end>total)end=total;
  for(unsigned i=next;i<end;i++){cbf_data(wire,message_id,i,packet,size);if(wire_send(1,wire))return -1;}
  int got=cbh_wait(CBF_ACK,answer,host_us()+ack_budget);if(got<0)return -1;
  unsigned advanced=got?rsf_u16(answer+5):next;
  if(got&&(answer[7]!=CBF_WINDOW||advanced>total))return -1;
  if(got&&advanced>next){next=advanced;stalled=0;}
  else{link_retries++;if(++stalled>=5)return -1;}
 }
 return next==total?0:-1;
}
static int cbh_read(uint8_t packet[256],size_t *size){
 uint8_t wire[8],answer[8];unsigned length=0;
 for(unsigned attempt=0;attempt<5&&!host_stopped();attempt++){
  if(attempt)link_retries++;
  if(cbh_control(CBF_PULL,65535,wire))return -1;
  int got=cbh_wait(CBF_META,answer,host_us()+100000);if(got<0)return -1;
  if(got&&!answer[7]){length=rsf_u16(answer+5);break;}
 }
 if(length<RSF_HEAD+2||length>CBF_PACKET)return -1;
 uint8_t seen[(CBF_PACKET+CBF_PAYLOAD-1)/CBF_PAYLOAD]={0};unsigned total=cbf_fragments(length);
 for(unsigned begin=0;begin<total;){
  unsigned end=begin+CBF_WINDOW;if(end>total)end=total;int complete=0;
  for(unsigned attempt=0;attempt<5&&!host_stopped();attempt++){
   if(attempt)link_retries++;
   if(cbh_control(CBF_PULL,begin,wire))return -1;
   uint64_t deadline=host_us()+100000;unsigned got_end=0;
   while(!host_stopped()&&host_us()<deadline){unsigned data;int ret=wire_receive(&data,answer,deadline);if(ret<0)return -1;if(!ret)break;
    if(!data){
     if(rsf_u32(answer+1)!=message_id){wire_ignored++;continue;}
     if(answer[0]==CBF_ERROR)return -1;
     if(answer[0]==CBF_END&&rsf_u16(answer+5)==end&&!answer[7])got_end=1;else wire_ignored++;
    }else{
     unsigned i=answer[2];if(rsf_u16(answer)!=(message_id&65535u)||i<begin||i>=end){wire_ignored++;continue;}
     unsigned start=i*CBF_PAYLOAD,n=length-start;if(n>CBF_PAYLOAD)n=CBF_PAYLOAD;
     for(unsigned k=n;k<CBF_PAYLOAD;k++)if(answer[3+k])return -1;
     if(seen[i]&&memcmp(packet+start,answer+3,n))return -1;
     memcpy(packet+start,answer+3,n);seen[i]=1;
    }
    unsigned all=got_end;for(unsigned i=begin;i<end;i++)all&=seen[i];
    if(all){complete=1;break;}
   }
   if(complete)break;
  }
  if(!complete){return -1;}begin=end;
 }
 *size=length;return 0;
}
#endif
