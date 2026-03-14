#ifndef ENCODERS_H
#define ENCODERS_H

#include <stdint.h>

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

#endif
