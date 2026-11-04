#ifndef TEST_CACHE_H
#define TEST_CACHE_H
#include <stdint.h>
#define CACHE_LINE_ALIGN __attribute__((aligned(64)))
typedef uintptr_t AL_UINTPTR;
static inline void AlCache_FlushDcacheRange(AL_UINTPTR a,AL_UINTPTR b){(void)a;(void)b;}
#endif
