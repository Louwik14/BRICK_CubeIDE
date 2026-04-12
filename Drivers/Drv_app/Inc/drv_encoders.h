#ifndef DRV_ENCODERS_H
#define DRV_ENCODERS_H

#include <stdint.h>

#include "encoders.h"

#define ENCODER_COUNT ENC_COUNT

void drv_encoders_init(void);
void drv_encoders_poll(void);
int16_t drv_encoder_get_delta(uint8_t id);
void drv_encoder_reset(uint8_t id);

#endif
