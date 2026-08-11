#pragma once

#include <stdint.h>

#define BRICK6_PICO_LOG_SINE_SIZE (1U << 12)
#define BRICK6_PICO_EXP14_SIZE    (1U << 14)
#define BRICK6_PICO_EXP32_SIZE    (1U << 14)
#define BRICK6_PICO_EXP19_SIZE    (1U << 6)

extern const uint16_t table_dx_log_sine_14[BRICK6_PICO_LOG_SINE_SIZE];
extern const uint16_t table_dx_exp_14[BRICK6_PICO_EXP14_SIZE];
extern const uint32_t table_dx_exp_32[BRICK6_PICO_EXP32_SIZE];
extern const uint32_t table_dx_exp_19[BRICK6_PICO_EXP19_SIZE];

void brick6_fm_pico_tables_init(void);
