#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../src/json_min.h"
int main(void){
 struct jd d;uint64_t n;
 const char *good="{\"a\":[1,2,65535],\"b\":{\"v\":18446744073709551615},\"c\":true}";
 assert(jparse(&d,good,strlen(good))==0);
 assert(jfield(&d,jget(&d,0,"b"),"v",&n)==0&&n==UINT64_MAX);
 const char *bad[]={"{\"a\":1,\"a\":2}","{\"a\":{\"b\":1,\"b\":2}}","{\"a\":01}","{\"a\":-1}","{\"a\":1.2}","{\"a\":1e2}","[1,]","{\"a\":}","{\"a\":1}junk","{\"a\":\"bad\\n\"}","[[[[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]]]]"};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)assert(jparse(&d,bad[i],strlen(bad[i]))==-1);
 const char *overflow="18446744073709551616";assert(jparse(&d,overflow,strlen(overflow))==0&&jnum(&d,0,&n)==-1);
 unsigned random=1;char text[300];
 for(unsigned i=0;i<100000;i++){
  random=random*1664525u+1013904223u;unsigned len=random%sizeof text;
  for(unsigned k=0;k<len;k++){random=random*1664525u+1013904223u;text[k]=(char)(random%128);}
  (void)jparse(&d,text,len);
 }
 return 0;
}
