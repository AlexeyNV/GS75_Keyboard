#ifndef RUNTIME_TIMING_H
#define RUNTIME_TIMING_H
#include <stdint.h>
void RuntimeTiming_Init(void);
uint32_t RuntimeTiming_Begin(void);
void RuntimeTiming_End(uint32_t start);
uint32_t RuntimeTiming_MaxUs(void);
#endif
