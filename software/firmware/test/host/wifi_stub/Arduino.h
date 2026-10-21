#pragma once
#include <stdint.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
extern int g_configTimeCalls;
inline void configTime(long, int, const char *) { ++g_configTimeCalls; }
inline uint32_t millis() { return 1000; }
