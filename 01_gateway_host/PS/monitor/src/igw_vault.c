#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <termios.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
/* Public opaque OpenSSL 1.1/3 EVP ABI; crypto operations remain in libcrypto.
 * No OpenSSL development headers or -lcrypto are needed in the SDK sysroot.
 * Constants are the public EVP_CTRL_AEAD_* values in openssl/evp.h. */
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;
typedef struct evp_cipher_st EVP_CIPHER;
typedef struct evp_md_st EVP_MD;
typedef struct engine_st ENGINE;
static struct {
 void *handle;
 EVP_CIPHER_CTX *(*EVP_CIPHER_CTX_new)(void);
 void (*EVP_CIPHER_CTX_free)(EVP_CIPHER_CTX *);
 const EVP_CIPHER *(*EVP_aes_256_gcm)(void);
 const EVP_MD *(*EVP_sha256)(void);
 int (*EVP_EncryptInit_ex)(EVP_CIPHER_CTX *,const EVP_CIPHER *,ENGINE *,const unsigned char *,const unsigned char *);
 int (*EVP_DecryptInit_ex)(EVP_CIPHER_CTX *,const EVP_CIPHER *,ENGINE *,const unsigned char *,const unsigned char *);
 int (*EVP_EncryptUpdate)(EVP_CIPHER_CTX *,unsigned char *,int *,const unsigned char *,int);
 int (*EVP_DecryptUpdate)(EVP_CIPHER_CTX *,unsigned char *,int *,const unsigned char *,int);
 int (*EVP_EncryptFinal_ex)(EVP_CIPHER_CTX *,unsigned char *,int *);
 int (*EVP_DecryptFinal_ex)(EVP_CIPHER_CTX *,unsigned char *,int *);
 int (*EVP_CIPHER_CTX_ctrl)(EVP_CIPHER_CTX *,int,int,void *);
 int (*PKCS5_PBKDF2_HMAC)(const char *,int,const unsigned char *,int,int,const EVP_MD *,int,unsigned char *);
 int (*RAND_bytes)(unsigned char *,int);
 void (*OPENSSL_cleanse)(void *,size_t);
 const char *(*OpenSSL_version)(int);
 unsigned char *(*SHA256)(const unsigned char *,size_t,unsigned char *);
} crypto;
#define MAX_PLAIN (16U*1024U*1024U)
#define HEAD 64U
#define TAG 16U
#define ITERATIONS 600000U
#define PASS_CAP 130
static volatile sig_atomic_t stopping;
static char secret[PASS_CAP];
static const unsigned char check_text[]="IGW_VAULT_R11_PASSWORD_CHECK_V1";
static uint64_t ms_now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))abort();return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;}
static void interrupted(int sig){(void)sig;stopping=1;}
static void wipe(void *p,size_t n){if(p&&n){if(crypto.OPENSSL_cleanse)crypto.OPENSSL_cleanse(p,n);else{volatile unsigned char *q=p;while(n--)*q++=0;}}}
static int crypto_load(void){
 const char *names[]={"libcrypto.so.1.1","libcrypto.so.3"};
 for(unsigned i=0;i<2&&!crypto.handle;i++)crypto.handle=dlopen(names[i],RTLD_NOW|RTLD_LOCAL);
 if(!crypto.handle){fputs("ERROR=libcrypto_not_available\n",stderr);return -1;}
#define LOAD(name) do {*(void **)(&crypto.name)=dlsym(crypto.handle,#name);if(!crypto.name){fprintf(stderr,"ERROR=crypto_symbol_missing:%s\n",#name);return -1;}}while(0)
 LOAD(EVP_CIPHER_CTX_new);LOAD(EVP_CIPHER_CTX_free);LOAD(EVP_aes_256_gcm);LOAD(EVP_sha256);
 LOAD(EVP_EncryptInit_ex);LOAD(EVP_DecryptInit_ex);LOAD(EVP_EncryptUpdate);LOAD(EVP_DecryptUpdate);
 LOAD(EVP_EncryptFinal_ex);LOAD(EVP_DecryptFinal_ex);LOAD(EVP_CIPHER_CTX_ctrl);
 LOAD(PKCS5_PBKDF2_HMAC);LOAD(RAND_bytes);LOAD(OPENSSL_cleanse);LOAD(OpenSSL_version);LOAD(SHA256);
#undef LOAD
 printf("CRYPTO_RUNTIME=%s\n",crypto.OpenSSL_version(0));return 0;
}
static int transfer_all(int fd,unsigned char *p,size_t n,bool writing){
 while(n){ssize_t k=writing?write(fd,p,n):read(fd,p,n);if(k<0&&errno==EINTR&&!stopping)continue;if(k<=0)return -1;p+=(size_t)k;n-=(size_t)k;}return 0;
}
static int read_pass(int pass_fd,bool confirm){
 char again[PASS_CAP]={0};int fd=pass_fd;struct termios old={0};bool tty=false;size_t length[2]={0,0};
 if(fd<0){fd=open("/dev/tty",O_RDWR|O_CLOEXEC);if(fd<0)return -1;tty=true;struct termios off;
  if(tcgetattr(fd,&old)){close(fd);return -1;}off=old;off.c_lflag&=(tcflag_t)~ECHO;
  if(tcsetattr(fd,TCSAFLUSH,&off)){close(fd);return -1;}
 }
 int result=-1;
 for(unsigned round=0;round<(confirm?2U:1U);round++){
  char *out=round?again:secret;if(tty)dprintf(fd,"%s (12-128 bytes): ",round?"Confirm passphrase":"Passphrase");
  while(length[round]<PASS_CAP-1&&!stopping){unsigned char c;ssize_t n=read(fd,&c,1);if(n<0&&errno==EINTR)continue;if(n!=1)goto done;if(c=='\n')break;if(c=='\r'||c==0||c<32||c==127)goto done;out[length[round]++]=(char)c;}
  if(tty)dprintf(fd,"\n");
  if(length[round]<12||length[round]>=PASS_CAP-1||stopping)goto done;
 }
 if(confirm&&(length[0]!=length[1]||memcmp(secret,again,length[0])))goto done;
 result=0;
done:
 if(tty){tcsetattr(fd,TCSAFLUSH,&old);close(fd);}wipe(again,sizeof again);
 if(result)fputs("ERROR=passphrase_input_or_confirmation\n",stderr);
 return result;
}
static void put32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(24-8*i));}
static uint32_t get32(const unsigned char *p){uint32_t n=0;for(unsigned i=0;i<4;i++)n=(n<<8)|p[i];return n;}
static void put64(unsigned char *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(n>>(56-8*i));}
static uint64_t get64(const unsigned char *p){uint64_t n=0;for(unsigned i=0;i<8;i++)n=(n<<8)|p[i];return n;}
static int crypt_buffer(bool encrypt,const unsigned char key[32],const unsigned char nonce[12],const unsigned char *aad,size_t aad_n,const unsigned char *input,size_t n,unsigned char *output,unsigned char tag[TAG]){
 EVP_CIPHER_CTX *ctx=crypto.EVP_CIPHER_CTX_new();if(!ctx)return -1;int result=-1,l=0,final=0;
 if(encrypt){
  if(crypto.EVP_EncryptInit_ex(ctx,crypto.EVP_aes_256_gcm(),NULL,NULL,NULL)!=1||crypto.EVP_CIPHER_CTX_ctrl(ctx,0x9,12,NULL)!=1||crypto.EVP_EncryptInit_ex(ctx,NULL,NULL,key,nonce)!=1)goto done;
  if(aad_n&&crypto.EVP_EncryptUpdate(ctx,NULL,&l,aad,(int)aad_n)!=1)goto done;
  if(crypto.EVP_EncryptUpdate(ctx,output,&l,input,(int)n)!=1||l!=(int)n||crypto.EVP_EncryptFinal_ex(ctx,output+n,&final)!=1||final!=0||crypto.EVP_CIPHER_CTX_ctrl(ctx,0x10,TAG,tag)!=1)goto done;
 }else{
  if(crypto.EVP_DecryptInit_ex(ctx,crypto.EVP_aes_256_gcm(),NULL,NULL,NULL)!=1||crypto.EVP_CIPHER_CTX_ctrl(ctx,0x9,12,NULL)!=1||crypto.EVP_DecryptInit_ex(ctx,NULL,NULL,key,nonce)!=1)goto done;
  if(aad_n&&crypto.EVP_DecryptUpdate(ctx,NULL,&l,aad,(int)aad_n)!=1)goto done;
  if(crypto.EVP_DecryptUpdate(ctx,output,&l,input,(int)n)!=1||l!=(int)n||crypto.EVP_CIPHER_CTX_ctrl(ctx,0x11,TAG,tag)!=1||crypto.EVP_DecryptFinal_ex(ctx,output+n,&final)!=1||final!=0)goto done;
 }
 result=0;
done:crypto.EVP_CIPHER_CTX_free(ctx);return result;
}
static int self_test(void){
 unsigned char key[32]={0},nonce[12]={0},plain[16]={0},cipher[32]={0},out[32]={0},tag[16]={0};
 static const unsigned char wanted[16]={0xce,0xa7,0x40,0x3d,0x4d,0x60,0x6b,0x6e,0x07,0x4e,0xc5,0xd3,0xba,0xf3,0x9d,0x18};
 static const unsigned char wanted_tag[16]={0xd0,0xd1,0xc8,0xa7,0x99,0x99,0x6b,0xf0,0x26,0x5b,0x98,0xb5,0xd4,0x8a,0xb9,0x19};
 if(crypt_buffer(true,key,nonce,NULL,0,plain,16,cipher,tag)||memcmp(cipher,wanted,16)||memcmp(tag,wanted_tag,16)||crypt_buffer(false,key,nonce,NULL,0,cipher,16,out,tag)||memcmp(out,plain,16))return -1;
 tag[0]^=1;if(!crypt_buffer(false,key,nonce,NULL,0,cipher,16,out,tag))return -1;
 puts("AES256_GCM_KNOWN_VECTOR=PASS TAMPER_REJECTION=PASS");return 0;
}
static int pack(const unsigned char *plain,size_t n,unsigned char **sealed,size_t *sealed_n){
 if(n>MAX_PLAIN)return -1;
 unsigned char *out=calloc(1,n+HEAD+TAG+16),key[32]={0};if(!out)return -1;
 memcpy(out,"IGWVLT11",8);out[8]=1;out[9]=1;put32(out+12,ITERATIONS);put64(out+44,n);
 int ok=-1;
 if(crypto.RAND_bytes(out+16,16)!=1||crypto.RAND_bytes(out+32,12)!=1||crypto.PKCS5_PBKDF2_HMAC(secret,(int)strlen(secret),out+16,16,ITERATIONS,crypto.EVP_sha256(),32,key)!=1)goto done;
 if(crypt_buffer(true,key,out+32,out,HEAD,plain,n,out+HEAD,out+HEAD+n))goto done;
 *sealed=out;*sealed_n=n+HEAD+TAG;ok=0;
done:wipe(key,sizeof key);if(ok){wipe(out,n+HEAD+TAG+16);free(out);}return ok;
}
static int unpack(const unsigned char *sealed,size_t n,unsigned char **plain,size_t *plain_n){
 if(n<HEAD+TAG||memcmp(sealed,"IGWVLT11",8)||sealed[8]!=1||sealed[9]!=1||sealed[10]||sealed[11]||get32(sealed+12)!=ITERATIONS)return -1;
 for(unsigned i=52;i<HEAD;i++)if(sealed[i])return -1;
 uint64_t count=get64(sealed+44);if(count>MAX_PLAIN||count+HEAD+TAG!=n)return -1;
 unsigned char *out=calloc(1,(size_t)count+16),key[32]={0},tag[TAG];if(!out)return -1;int ok=-1;memcpy(tag,sealed+HEAD+count,TAG);
 if(crypto.PKCS5_PBKDF2_HMAC(secret,(int)strlen(secret),sealed+16,16,ITERATIONS,crypto.EVP_sha256(),32,key)!=1||crypt_buffer(false,key,sealed+32,sealed,HEAD,sealed+HEAD,(size_t)count,out,tag))goto done;
 *plain=out;*plain_n=(size_t)count;ok=0;
done:wipe(key,sizeof key);if(ok){wipe(out,(size_t)count+16);free(out);}return ok;
}
static int verify_archive(unsigned char *sealed,size_t n){
 unsigned char *plain=NULL;size_t pn=0;
 if(unpack(sealed,n,&plain,&pn)){fputs("RESULT=REJECTED authentication_or_format\n",stderr);return 3;}
 unsigned char digest[32];char hex[65];
 if(!crypto.SHA256(plain,pn,digest)){wipe(plain,pn);free(plain);return 2;}
 for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",digest[i]);
 printf("AUTHENTICATED_OPEN=PASS PLAINTEXT_BYTES=%zu SHA256=%s\n",pn,hex);wipe(plain,pn);free(plain);plain=NULL;
 secret[0]^=1;int wrong=unpack(sealed,n,&plain,&pn);secret[0]^=1;
 if(!wrong){wipe(plain,pn);free(plain);return 3;}puts("WRONG_PASSPHRASE_REJECT=PASS");
 const size_t positions[]={16,HEAD,n-1};const char *names[]={"HEADER_TAMPER_REJECT","CIPHERTEXT_TAMPER_REJECT","TAG_TAMPER_REJECT"};
 for(unsigned i=0;i<3;i++){
  sealed[positions[i]]^=1;int ret=unpack(sealed,n,&plain,&pn);sealed[positions[i]]^=1;
  if(!ret){wipe(plain,pn);free(plain);return 3;}printf("%s=PASS\n",names[i]);
 }
 puts("RESULT=PASS_ARCHIVE_VERIFY; no plaintext file written; original archive unchanged");return 0;
}
static int read_file_at(int dirfd,const char *path,unsigned char **data,size_t *n,size_t cap){
 int fd=openat(dirfd,path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return -1;struct stat s={0},after={0};int result=-1;
 if(fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_size<0||(uint64_t)s.st_size>cap)goto done;
 unsigned char *p=malloc((size_t)s.st_size+1);if(!p)goto done;
 if(transfer_all(fd,p,(size_t)s.st_size,false)||fstat(fd,&after)||after.st_size!=s.st_size||after.st_mtim.tv_sec!=s.st_mtim.tv_sec||after.st_mtim.tv_nsec!=s.st_mtim.tv_nsec){free(p);goto done;}
 p[s.st_size]=0;*data=p;*n=(size_t)s.st_size;result=0;
done:close(fd);return result;
}
static void hex_string(char *out,const unsigned char *in,size_t n){for(size_t i=0;i<n;i++)snprintf(out+2*i,3,"%02x",in[i]);}
/* Publish without replacing any existing path. Temporary files contain only
 * ciphertext except on explicit authenticated `open` to an operator path. */
static int publish(int directory,const char *name,const unsigned char *p,size_t n){
 if(!*name||strchr(name,'/')||!strcmp(name,".")||!strcmp(name,".."))return -1;
 unsigned char random[12];char temp[64],hex[25];if(crypto.RAND_bytes(random,sizeof random)!=1)return -1;hex_string(hex,random,sizeof random);snprintf(temp,sizeof temp,".igw-%s.tmp",hex);
 int fd=openat(directory,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return -1;int result=-1;
 if(transfer_all(fd,(unsigned char *)p,n,true)||fsync(fd)){close(fd);goto done;}if(close(fd))goto done;
 /* RENAME_NOREPLACE provides atomic publication on FAT as well as ext4.
  * No unsafe check-then-rename fallback: unsupported systems fail closed. */
#ifdef SYS_renameat2
 if(syscall(SYS_renameat2,directory,temp,directory,name,1U)||fsync(directory))goto done;
#else
 if(linkat(directory,temp,directory,name,0)||unlinkat(directory,temp,0)||fsync(directory))goto done;
#endif
 result=0;
done:unlinkat(directory,temp,0);return result;
}
static int output_path(const char *path,const unsigned char *data,size_t n){
 char copy[PATH_MAX];if(strlen(path)>=sizeof copy)return -1;strcpy(copy,path);char *slash=strrchr(copy,'/');const char *name=copy,*parent=".";
 if(slash){*slash=0;name=slash+1;parent=*copy?copy:"/";}
 int dir=open(parent,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return -1;int result=publish(dir,name,data,n);close(dir);return result;
}
static int receive_deadline(int fd,unsigned char *out,size_t n,uint64_t end){
 size_t used=0;
 while(used<n&&!stopping&&ms_now()<end){
  struct pollfd p={fd,POLLIN,0};int ret=poll(&p,1,100);if(ret<0&&errno==EINTR)continue;if(ret<0)return -1;if(!ret)continue;
  ssize_t k=recv(fd,out+used,n-used,0);if(k<0&&(errno==EAGAIN||errno==EINTR))continue;if(k<=0)return -1;used+=(size_t)k;
 }
 return used==n?0:-1;
}
static int archive_socket(int fd,unsigned char **data,size_t *size,bool startup){
 uint64_t end=ms_now()+(startup?15000U:3000U);unsigned char header[8];
 if(send(fd,"R",1,MSG_NOSIGNAL)!=1||receive_deadline(fd,header,sizeof header,end))return -1;
 uint64_t n=get64(header);if(n>MAX_PLAIN||n<64)return -1;unsigned char *out=malloc((size_t)n+1);if(!out)return -1;
 const char prefix[]="{\"schema\":\"IGW_WEB_R10\",\"session\":\"";
 if(receive_deadline(fd,out,(size_t)n,end)||memcmp(out,prefix,sizeof prefix-1)||out[n-1]!='}'||memchr(out,0,(size_t)n)){wipe(out,(size_t)n);free(out);return -1;}
 out[n]=0;*data=out;*size=(size_t)n;return 0;
}
static int save_snapshot(int vault,int ipc,unsigned count){
 struct statvfs fs;if(fstatvfs(vault,&fs)||(uint64_t)fs.f_bavail*fs.f_frsize<64U*1024U*1024U){fputs("ARCHIVE_ERROR=free_space_below_64MiB\n",stderr);return -1;}
 unsigned char *plain=NULL,*sealed=NULL;size_t n=0,sealed_n=0;uint64_t began=ms_now();if(archive_socket(ipc,&plain,&n,count==1))return -1;
 int result=-1;if(pack(plain,n,&sealed,&sealed_n))goto done;
 char nonce[25],name[80];hex_string(nonce,sealed+32,12);snprintf(name,sizeof name,"igw-%s.igwe",nonce);
 if(publish(vault,name,sealed,sealed_n))goto done;
 printf("ARCHIVE_SAVED count=%u file=%s plaintext_bytes=%zu ciphertext_bytes=%zu duration_ms=%"PRIu64"\n",count,name,n,sealed_n,ms_now()-began);result=0;
done:wipe(plain,n);free(plain);free(sealed);return result;
}
static int open_vault(const char *path){
 if(mkdir(path,0700)&&errno!=EEXIST)return -1;
 int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
 /* A held advisory file lock is also available on FAT; no PID guessing or
  * stale lock deletion is needed. */
 int lock=openat(fd,"vault.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);if(lock<0){close(fd);return -1;}
 struct flock fl={0};fl.l_type=F_WRLCK;fl.l_whence=SEEK_SET;
 if(fcntl(lock,F_SETLK,&fl)){close(lock);close(fd);return -1;}
 /* Keep lock fd live until process exit. FD_CLOEXEC prevents child inheritance. */
 return fd;
}
static int vault_unlock(int vault,int pass_fd){
 struct stat s;bool exists=fstatat(vault,"vault-check.igwe",&s,AT_SYMLINK_NOFOLLOW)==0;
 if(!exists&&errno!=ENOENT)return -1;
 if(read_pass(pass_fd,!exists))return -1;
 unsigned char *data=NULL,*plain=NULL;size_t n=0,pn=0;int result=-1;
 if(exists){if(read_file_at(vault,"vault-check.igwe",&data,&n,MAX_PLAIN+HEAD+TAG)||unpack(data,n,&plain,&pn)||pn!=sizeof check_text-1||memcmp(plain,check_text,pn))goto done;}
 else{if(pack(check_text,sizeof check_text-1,&data,&n)||publish(vault,"vault-check.igwe",data,n))goto done;}
 puts(exists?"VAULT_UNLOCK=PASS":"VAULT_INITIALIZED=PASS; remember your passphrase; it is not saved");result=0;
done:free(data);wipe(plain,pn);free(plain);if(result)fputs("ERROR=vault_wrong_passphrase_or_corrupt_check\n",stderr);return result;
}
static int supervise(int vault,const char *script,unsigned interval){
 int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0,pair)){perror("socketpair");return 2;}
 pid_t pid=fork();if(pid<0){close(pair[0]);close(pair[1]);return 2;}if(!pid){
  close(pair[0]);if(dup2(pair[1],3)<0)_exit(127);if(pair[1]!=3)close(pair[1]);fcntl(3,F_SETFD,0);setenv("IGW_ARCHIVE_FD","3",1);
  wipe(secret,sizeof secret);setpgid(0,0);signal(SIGINT,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGHUP,SIG_DFL);
  int zero=open("/dev/null",O_RDONLY);if(zero>=0){dup2(zero,STDIN_FILENO);close(zero);}execl("/bin/sh","sh",script,(char *)NULL);_exit(127);
 }
 close(pair[1]);int ipc=pair[0];setpgid(pid,pid);uint64_t next=0;unsigned saved=0;int status=0,result=0;bool ready=false,reaped=false;
 printf("SECURE_R11_STARTED; archive interval=%us; Ctrl+C stops owned services and seals final evidence\n",interval);
 while(!stopping){
  pid_t ended=waitpid(pid,&status,WNOHANG);if(ended==pid){reaped=true;result=WIFEXITED(status)?WEXITSTATUS(status):2;if(!result)result=2;break;}
  uint64_t now=ms_now();
  if(now>=next){if(save_snapshot(vault,ipc,saved+1)==0){saved++;ready=true;next=ms_now()+(uint64_t)interval*1000;}
   else {fputs("RESULT=REVIEW reason=archive_failed_stopping_services\n",stderr);result=3;break;}
  }
  struct timespec pause={0,100000000};nanosleep(&pause,NULL);
 }
 /* Seal while the exporter is still running. This snapshot precedes shutdown;
  * pending jobs may still be pending. It is not a persistent write replay log. */
 sig_atomic_t interrupted_stop=stopping;stopping=0;
 if(ready&&save_snapshot(vault,ipc,saved+1)==0)saved++;
 else if(ready)result=3;
 stopping=interrupted_stop;
 kill(-pid,SIGTERM);
 if(!reaped){uint64_t until=ms_now()+12000;while(ms_now()<until){pid_t w=waitpid(pid,&status,WNOHANG);if(w==pid){reaped=true;break;}struct timespec pause={0,100000000};nanosleep(&pause,NULL);}}
 if(!reaped){fputs("REVIEW=launcher_did_not_exit; ownership locks retained\n",stderr);result=3;}
 close(ipc);printf("ARCHIVE_TOTAL=%u RESULT=%s\n",saved,result?"REVIEW":"SECURE_SERVICE_STOPPED");return result;
}
static unsigned number(const char *s,unsigned low,unsigned high){char *end;errno=0;unsigned long n=strtoul(s,&end,10);if(errno||!*s||*end||n<low||n>high)return UINT_MAX;return (unsigned)n;}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);umask(077);struct rlimit core={0,0};setrlimit(RLIMIT_CORE,&core);
 struct sigaction action={0};action.sa_handler=interrupted;sigemptyset(&action.sa_mask);sigaction(SIGINT,&action,NULL);sigaction(SIGTERM,&action,NULL);sigaction(SIGHUP,&action,NULL);signal(SIGPIPE,SIG_IGN);
 if(argc<2){fputs("Usage: igw_vault probe | seal --input FILE --output FILE | open --input FILE --output FILE | verify --input FILE | serve --vault DIR --run-script FILE [--interval 30]\nPassphrase is read from terminal, never argv or environment. --pass-fd N accepts a private inherited FD for tests/automation.\n",stderr);return 2;}
 const char *input=NULL,*output=NULL,*vault_path=NULL,*script=NULL;unsigned interval=30;int pass_fd=-1;
 for(int i=2;i<argc;i++){if(i+1>=argc)return 2;const char *k=argv[i],*v=argv[++i];
  if(!strcmp(k,"--input"))input=v;else if(!strcmp(k,"--output"))output=v;else if(!strcmp(k,"--vault"))vault_path=v;else if(!strcmp(k,"--run-script"))script=v;
  else if(!strcmp(k,"--interval")){interval=number(v,1,3600);if(interval==UINT_MAX)return 2;}
  else if(!strcmp(k,"--pass-fd")){unsigned n=number(v,0,1024);if(n==UINT_MAX)return 2;pass_fd=(int)n;}else return 2;
 }
 if(crypto_load()||self_test())return 2;
 if(!strcmp(argv[1],"probe")){puts("RESULT=PASS_CRYPTO_PROBE; no bus access or persistence test");return 0;}
 int result=2;bool memory_locked=mlock(secret,sizeof secret)==0;printf("PASSPHRASE_MEMORY_LOCKED=%u\n",memory_locked?1U:0U);
 if(!strcmp(argv[1],"serve")){
  if(!vault_path||!script)goto done;
  int vault=open_vault(vault_path);if(vault<0){fputs("ERROR=vault_directory_or_lock\n",stderr);goto done;}
  if(vault_unlock(vault,pass_fd)){close(vault);goto done;}if(pass_fd>2){close(pass_fd);pass_fd=-1;}
  result=supervise(vault,script,interval);close(vault);
 }else if(!strcmp(argv[1],"verify")){
  if(!input||read_pass(pass_fd,false))goto done;
  unsigned char *data=NULL;size_t n=0;
  if(read_file_at(AT_FDCWD,input,&data,&n,MAX_PLAIN+HEAD+TAG))goto done;
  result=verify_archive(data,n);free(data);
 }else if(!strcmp(argv[1],"seal")||!strcmp(argv[1],"open")){
  if(!input||!output||read_pass(pass_fd,!strcmp(argv[1],"seal")))goto done;
  unsigned char *data=NULL,*converted=NULL;size_t n=0,cn=0;
  if(read_file_at(AT_FDCWD,input,&data,&n,MAX_PLAIN+HEAD+TAG)){fputs("ERROR=input_not_regular_or_too_large\n",stderr);goto done;}
  int ok=!strcmp(argv[1],"seal")?pack(data,n,&converted,&cn):unpack(data,n,&converted,&cn);
  if(ok){fputs("RESULT=REJECTED authentication_or_format; no output published\n",stderr);result=3;}
  else if(output_path(output,converted,cn)){fputs("ERROR=output_exists_or_io; never overwrite existing files\n",stderr);result=2;}
  else {printf("RESULT=%s INPUT_BYTES=%zu OUTPUT_BYTES=%zu\n",!strcmp(argv[1],"seal")?"SEALED":"AUTHENTICATED_OPEN",n,cn);result=0;}
  wipe(data,n);free(data);wipe(converted,cn);free(converted);
 }
done:wipe(secret,sizeof secret);if(memory_locked)munlock(secret,sizeof secret);if(crypto.handle)dlclose(crypto.handle);return result;
}
