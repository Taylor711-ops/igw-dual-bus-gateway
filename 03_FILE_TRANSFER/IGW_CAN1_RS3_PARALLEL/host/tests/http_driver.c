#define main file_web_main
#include "../src/igw_file_web.c"
#undef main
int main(int argc,char **argv){
 if(argc!=3||strlen(argv[1])>=sizeof runtime||strlen(argv[2])>=sizeof backend)return 2;
 strcpy(runtime,argv[1]);strcpy(backend,argv[2]);strcpy(rs_backend,argv[2]);strcpy(control_token,"0123456789abcdef0123456789abcdef");
 struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_handler=stop;sigemptyset(&sa.sa_mask);sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);signal(SIGPIPE,SIG_IGN);
 setvbuf(stdout,NULL,_IOLBF,0);puts("READY_NATIVE_HANDLER_NO_TCP");char line[2200],input[1024],output[1024];
 while(fgets(line,sizeof line,stdin)){
  if(sscanf(line,"%1023s %1023s",input,output)!=2)break;
  int in=open(input,O_RDONLY|O_CLOEXEC),out=open(output,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);if(in<0||out<0)return 2;
  handle(in,out);close(in);close(out);puts("DONE");
 }
 if(active_count()){stopping=1;for(unsigned i=0;i<job_count;i++)if(jobs[i].worker&&!jobs[i].done)kill(jobs[i].worker,SIGTERM);while(active_count()){harvest();struct timespec t={0,20000000};nanosleep(&t,NULL);}}
 return 0;
}
