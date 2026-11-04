#ifndef IGWF_SHA256_H
#define IGWF_SHA256_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct igwf_sha {uint32_t h[8];uint64_t bytes;uint8_t buf[64];unsigned used;};
static inline uint32_t igwf_rotr(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static inline void igwf_sha_block(struct igwf_sha *s,const uint8_t b[64]){
 static const uint32_t k[64]={
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint32_t w[64];for(unsigned i=0;i<16;i++)w[i]=(uint32_t)b[i*4]<<24|(uint32_t)b[i*4+1]<<16|(uint32_t)b[i*4+2]<<8|b[i*4+3];
 for(unsigned i=16;i<64;i++){uint32_t a=w[i-15],z=w[i-2];w[i]=w[i-16]+(igwf_rotr(a,7)^igwf_rotr(a,18)^(a>>3))+w[i-7]+(igwf_rotr(z,17)^igwf_rotr(z,19)^(z>>10));}
 uint32_t a=s->h[0],b0=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
 for(unsigned i=0;i<64;i++){uint32_t t=h+(igwf_rotr(e,6)^igwf_rotr(e,11)^igwf_rotr(e,25))+((e&f)^((~e)&g))+k[i]+w[i];uint32_t u=(igwf_rotr(a,2)^igwf_rotr(a,13)^igwf_rotr(a,22))+((a&b0)^(a&c)^(b0&c));h=g;g=f;f=e;e=d+t;d=c;c=b0;b0=a;a=t+u;}
 s->h[0]+=a;s->h[1]+=b0;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static inline void igwf_sha_init(struct igwf_sha *s){
 static const uint32_t init[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 memset(s,0,sizeof *s);memcpy(s->h,init,sizeof init);
}
static inline void igwf_sha_update(struct igwf_sha *s,const uint8_t *p,size_t n){
 s->bytes+=n;while(n){unsigned take=64-s->used;if(take>n)take=(unsigned)n;memcpy(s->buf+s->used,p,take);s->used+=take;p+=take;n-=take;if(s->used==64){igwf_sha_block(s,s->buf);s->used=0;}}
}
static inline void igwf_sha_final(struct igwf_sha *s,uint8_t out[32]){
 uint64_t bits=s->bytes*8;uint8_t end=0x80,zero=0;igwf_sha_update(s,&end,1);while(s->used!=56)igwf_sha_update(s,&zero,1);
 uint8_t tail[8];for(unsigned i=0;i<8;i++)tail[7-i]=(uint8_t)(bits>>(i*8));igwf_sha_update(s,tail,8);
 for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++)out[i*4+j]=(uint8_t)(s->h[i]>>(24-j*8));
}
static inline void igwf_digest(const uint8_t *p,size_t n,uint8_t out[32]){struct igwf_sha s;igwf_sha_init(&s);igwf_sha_update(&s,p,n);igwf_sha_final(&s,out);}
#endif
