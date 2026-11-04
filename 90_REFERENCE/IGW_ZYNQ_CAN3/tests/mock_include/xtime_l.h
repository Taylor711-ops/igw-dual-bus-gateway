#ifndef MOCK_XTIME_H
#define MOCK_XTIME_H
#include <stdint.h>
typedef uint64_t XTime;
#define COUNTS_PER_SECOND 333333343u
void XTime_GetTime(XTime*);
#endif
