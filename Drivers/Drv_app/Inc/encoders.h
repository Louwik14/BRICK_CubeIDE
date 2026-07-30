#ifndef ENCODERS_H
#define ENCODERS_H

#include <stdint.h>

#include "Core/brick_build_config.h"

typedef enum
{
    ENC_PAGE = 0,
    ENC_PARAM_A,
    ENC_PARAM_B,
    ENC_PARAM_C,
    ENC_COUNT
} encoder_id_t;

void encoders_init(void);
void encoders_update(uint32_t dt_ms);

int16_t encoder_get_delta(uint8_t encoder);
int16_t encoder_consume_delta(uint8_t encoder);
void encoder_reset_delta(uint8_t encoder);

#if BRICK_TEST_BUILD
uint8_t encoder_test_inject_delta(uint8_t encoder, int16_t delta);
#endif

#endif
