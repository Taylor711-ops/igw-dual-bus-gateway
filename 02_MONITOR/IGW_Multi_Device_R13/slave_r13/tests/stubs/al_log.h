#ifndef IGW_TEST_LOG_H
#define IGW_TEST_LOG_H
#define AL_LOG_LEVEL_NOTICE 3
#define AL_LOG(level,...) do {(void)(level);printf(__VA_ARGS__);}while(0)
static inline void AlLog_SetLevel(int level){(void)level;}
#endif
