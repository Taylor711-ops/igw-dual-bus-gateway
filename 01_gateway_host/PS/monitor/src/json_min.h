#ifndef IGW_JSON_MIN_H
#define IGW_JSON_MIN_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Strict bounded ASCII JSON reader for our numeric gateway schemas.
 * Rejects duplicate object keys, signed/floating numbers and string escapes.
 * Those constructs are never emitted by the pinned gateway wire protocol. */
struct jt {unsigned start,end,next;char type;};
struct jd {const char *s;size_t length,pos;unsigned count;struct jt t[512];};
static inline void jw(struct jd *d){while(d->pos<d->length && strchr(" \t\r\n",d->s[d->pos]))d->pos++;}
static inline int jparse_value(struct jd *d,unsigned depth) {
    jw(d);if(depth>12 || d->count>=512 || d->pos>=d->length)return -1;
    int me=(int)d->count++;struct jt *t=&d->t[me];t->start=(unsigned)d->pos;t->type=d->s[d->pos++];
    if(t->type=='{' || t->type=='[') {
        char end=t->type=='{'?'}':']';jw(d);
        if(d->pos<d->length && d->s[d->pos]==end)d->pos++;
        else for(;;) {
            if(t->type=='{') {
                int key=jparse_value(d,depth+1);if(key<0 || d->t[key].type!='"')return -1;
                for(unsigned k=(unsigned)me+1;k<(unsigned)key;) {
                    if(d->t[k].end-d->t[k].start==d->t[key].end-d->t[key].start &&
                       !memcmp(d->s+d->t[k].start,d->s+d->t[key].start,d->t[k].end-d->t[k].start))return -1;
                    unsigned v=d->t[k].next;if(v>=(unsigned)key)return -1;k=d->t[v].next;
                }
                jw(d);if(d->pos>=d->length || d->s[d->pos++]!=':')return -1;
            }
            if(jparse_value(d,depth+1)<0)return -1;
            jw(d);if(d->pos>=d->length)return -1;
            char c=d->s[d->pos++];if(c==end)break;if(c!=',')return -1;
        }
    } else if(t->type=='"') {
        while(d->pos<d->length && d->s[d->pos]!='"') {
            unsigned char c=(unsigned char)d->s[d->pos++];if(c<32 || c>=127 || c=='\\')return -1;
        }
        if(d->pos>=d->length)return -1;
        d->pos++;
    } else if(t->type>='0' && t->type<='9') {
        if(t->type=='0' && d->pos<d->length && d->s[d->pos]>='0' && d->s[d->pos]<='9')return -1;
        while(d->pos<d->length && d->s[d->pos]>='0' && d->s[d->pos]<='9')d->pos++;
        t->type='n';
    } else {
        const char *v=t->type=='t'?"true":t->type=='f'?"false":t->type=='n'?"null":NULL;
        if(!v || d->length-t->start<strlen(v) || memcmp(d->s+t->start,v,strlen(v)))return -1;
        d->pos=t->start+strlen(v);t->type='b';
    }
    t->end=(unsigned)d->pos;t->next=d->count;return me;
}
static inline int jparse(struct jd *d,const char *s,size_t n) {
    memset(d,0,sizeof *d);d->s=s;d->length=n;
    int r=jparse_value(d,0);jw(d);return r==0 && d->pos==n ? 0:-1;
}
static inline int jeq(const struct jd *d,int i,const char *s) {
    return i>=0 && d->t[i].type=='"' && d->t[i].end-d->t[i].start==strlen(s)+2 &&
           !memcmp(d->s+d->t[i].start+1,s,strlen(s));
}
static inline int jget(const struct jd *d,int object,const char *key) {
    if(object<0 || d->t[object].type!='{')return -1;
    for(unsigned i=(unsigned)object+1;i<d->t[object].next;) {
        unsigned value=d->t[i].next;if(jeq(d,(int)i,key))return (int)value;
        i=d->t[value].next;
    }
    return -1;
}
static inline int jnum(const struct jd *d,int i,uint64_t *out) {
    if(i<0 || d->t[i].type!='n')return -1;
    uint64_t n=0;
    for(unsigned k=d->t[i].start;k<d->t[i].end;k++) {
        unsigned x=(unsigned)(d->s[k]-'0');if(n>UINT64_MAX/10 || (n==UINT64_MAX/10 && x>UINT64_MAX%10))return -1;n=n*10+x;
    }
    *out=n;return 0;
}
static inline int jfield(const struct jd *d,int object,const char *key,uint64_t *out){return jnum(d,jget(d,object,key),out);}
static inline int jstr(const struct jd *d,int i,char *out,size_t capacity) {
    if(i<0 || d->t[i].type!='"')return -1;
    unsigned n=d->t[i].end-d->t[i].start-2;
    if(n>=capacity)return -1;
    memcpy(out,d->s+d->t[i].start+1,n);out[n]=0;return 0;
}
#endif
