#include <string.h>
#include "igw_can_rw_protocol.h"
static struct igw_can_write_cache can_cache;
static uint16_t registers[32];
void bridge_reset(void) {
    memset(registers,0,sizeof(registers));memset(&can_cache,0,sizeof can_cache);
    registers[0]=0x1234;registers[1]=1;registers[4]=IGW_RW_REVISION;registers[6]=0xffff;registers[8]=IGW_CAN_RW_REVISION;
}
unsigned bridge_reply(const uint8_t *request,unsigned length,uint8_t *reply) {
    if (igw_valid_adu(request,length) && request[0]==1) registers[2]++;
    registers[3]=123;
    return (unsigned)igw_slave_rw_reply(request,length,1,registers,reply);
}
unsigned bridge_get(unsigned address) {return address<32?registers[address]:0;}
void bridge_set(unsigned address,unsigned value) {if(address<32)registers[address]=(uint16_t)value;}

void bridge_can(const uint8_t *p,uint8_t *out,uint64_t now) {igw_can_rw_reply(&can_cache,registers,p,out,now);}
