#ifndef IGW_RW_WIRE_H
#define IGW_RW_WIRE_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
struct command { uint32_t v,seq,unit,fc,address,arg; };
static bool command_equal(const struct command *a,const struct command *b) {
    return a->v==b->v && a->seq==b->seq && a->unit==b->unit &&
        a->fc==b->fc && a->address==b->address && a->arg==b->arg;
}
static bool decimal(const char **cursor,uint32_t limit,uint32_t *out) {
    const char *p=*cursor;
    uint32_t value=0;
    if (*p<'0'||*p>'9') return false;
    do {
        uint32_t digit=(uint32_t)(*p-'0');
        if (value>limit/10 || (value==limit/10 && digit>limit%10)) return false;
        value=value*10+digit; p++;
    } while (*p>='0'&&*p<='9');
    *cursor=p; *out=value; return true;
}
static bool parse_command(const char *text,struct command *out) {
    struct command c={0};
    const char *p;
    if (!strncmp(text,"IGW1 RS485 ",11)) {
        p=text+11; c.v=1; c.unit=1; c.fc=3; c.arg=4;
        if (!decimal(&p,UINT32_MAX,&c.seq)||*p) return false;
    } else {
        if (!strncmp(text,"IGW2 RS485 READ ",16)) {c.fc=3; p=text+16;}
        else if (!strncmp(text,"IGW2 RS485 WRITE ",17)) {c.fc=6; p=text+17;}
        else return false;
        c.v=2;
        uint32_t *fields[]={&c.seq,&c.unit,&c.address,&c.arg};
        const uint32_t limits[]={UINT32_MAX,247,65535,65535};
        for (unsigned i=0;i<4;i++) {
            if (!decimal(&p,limits[i],fields[i])) return false;
            if (i<3) {if (*p!=' ')return false; p++;} else if (*p) return false;
        }
        /* This bring-up release is deliberately restricted to test unit 1. */
        if (c.unit!=1) return false;
        if (c.fc==3 && (!c.arg||c.arg>8||c.address>=32||c.arg>32-c.address)) return false;
        if (c.fc==6 && (c.address<16||c.address>31)) return false;
    }
    *out=c; return true;
}
#endif
