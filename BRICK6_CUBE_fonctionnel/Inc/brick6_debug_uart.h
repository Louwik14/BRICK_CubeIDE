#pragma once

#include <stdint.h>
#include "stm32h7xx_hal.h"

#ifndef BRICK6_STREAM_DEBUG
#define BRICK6_STREAM_DEBUG 1
#endif

#if BRICK6_STREAM_DEBUG

void brick6_debug_init(UART_HandleTypeDef *huart);
void brick6_debug_poll(void);
void brick6_debug_log(const char *fmt, ...);
void brick6_debug_hex(const void *data, uint32_t len);
void brick6_debug_dump_audio(float *buf, uint32_t frames);

#define STREAM_LOG(...) brick6_debug_log(__VA_ARGS__)

#else

#define brick6_debug_init(...) ((void)0)
#define brick6_debug_poll(...) ((void)0)
#define brick6_debug_log(...) ((void)0)
#define brick6_debug_hex(...) ((void)0)
#define brick6_debug_dump_audio(...) ((void)0)
#define STREAM_LOG(...) ((void)0)

#endif
