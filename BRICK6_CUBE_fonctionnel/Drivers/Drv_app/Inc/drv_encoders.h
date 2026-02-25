#pragma once
#include <stdint.h>

#define ENCODER_COUNT 4

void drv_encoders_init(void);
void drv_encoders_poll(void);
int16_t drv_encoder_get_delta(uint8_t id);
void drv_encoder_reset(uint8_t id);
