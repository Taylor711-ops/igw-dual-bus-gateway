#ifndef IGW_MANAGED_LOCK_H
#define IGW_MANAGED_LOCK_H
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef IGW_RS_LOCK
#define IGW_RS_LOCK "/tmp/igw_rs485_once.lock"
#endif
#define IGW_RS_FD_ENV "IGW_MANAGED_RS485_FD"

/* Only the file manager supplies this inherited directory descriptor.  An
 * invalid or stale capability must never turn into an unlocked operation. */
static inline int igw_managed_rs_lock(void){
 const char *s=getenv(IGW_RS_FD_ENV);if(!s)return 0;
 if(!*s){errno=EPERM;return -1;}
 unsigned long n=0;for(const char *p=s;*p;p++){
  if(*p<'0'||*p>'9'||n>(INT_MAX-(unsigned)(*p-'0'))/10u){errno=EPERM;return -1;}
  n=n*10+(unsigned)(*p-'0');
 }
 if(n<3){errno=EPERM;return -1;}
 struct stat a,b;
 if(fstat((int)n,&a)||lstat(IGW_RS_LOCK,&b)||!S_ISDIR(a.st_mode)||!S_ISDIR(b.st_mode)||
    a.st_dev!=b.st_dev||a.st_ino!=b.st_ino||a.st_uid!=geteuid()||(a.st_mode&0777)!=0700){errno=EPERM;return -1;}
 return 1;
}
#endif
