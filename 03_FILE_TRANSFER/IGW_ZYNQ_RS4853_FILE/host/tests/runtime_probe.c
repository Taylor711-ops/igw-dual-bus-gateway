/* Actual Web runtime creation + actual launcher archive contract. No TCP/bus. */
#define main original_web_main
#include "../src/igw_file_web.c"
#undef main
int main(void){
 if(initialize_runtime())return 2;
 struct sigaction action;memset(&action,0,sizeof action);action.sa_handler=stop;sigemptyset(&action.sa_mask);sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);
 char path[512];snprintf(path,sizeof path,"%s/probe.output",runtime);if(save_input(path,(const uint8_t *)"verified fixture",16))return 2;
 setvbuf(stdout,NULL,_IOLBF,0);puts("R23 FILE WEB READY runtime-contract probe only");printf("EVIDENCE_DIRECTORY=%s\n",runtime);
 while(!stopping){struct timespec delay={0,10000000};nanosleep(&delay,NULL);}
 struct timespec cleanup={0,250000000};nanosleep(&cleanup,NULL);puts("R23_STOP jobs=0 blocked=0 runtime-contract probe");return 0;
}
