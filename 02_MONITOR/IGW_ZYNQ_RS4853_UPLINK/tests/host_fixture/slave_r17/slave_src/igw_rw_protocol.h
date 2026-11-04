#ifndef IGW_RW_PROTOCOL_H
#define IGW_RW_PROTOCOL_H
#include "igw_protocol.h"

/* Only volatile demonstration registers 16..31 may be written. Unit 0
 * broadcasts intentionally receive no response AND perform no write here. */
#define IGW_RW_FIRST 16u
#define IGW_RW_LAST 31u
#define IGW_RW_REVISION 0x0801u
static inline void igw_rw_store(uint16_t regs[32], unsigned address, uint16_t value)
{
    regs[address]=value;regs[5]=(uint16_t)(regs[5]+1u);
    regs[6]=(uint16_t)address;regs[7]=value;
}
static inline size_t igw_slave_rw_reply(const uint8_t *p, size_t n,
        unsigned unit, uint16_t regs[32], uint8_t out[256])
{
    unsigned exception = 0, address, value;
    if (!unit || unit > 247 || !regs || !out || !igw_valid_adu(p,n) || p[0] != unit)
        return 0;
    if (p[1] != 6) return igw_slave_reply(p,n,unit,regs,32,out);
    if (n != 8) exception = 3;
    else {
        address = ((unsigned)p[2] << 8) | p[3];
        value = ((unsigned)p[4] << 8) | p[5];
        if (address < IGW_RW_FIRST || address > IGW_RW_LAST) exception = 2;
        else {
            igw_rw_store(regs,address,(uint16_t)value);
            for (unsigned i=0;i<6;i++) out[i]=p[i];
            return igw_append_crc(out,6);
        }
    }
    out[0]=(uint8_t)unit; out[1]=0x86; out[2]=(uint8_t)exception;
    return igw_append_crc(out,3);
}
#endif
