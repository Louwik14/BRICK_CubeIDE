#ifndef DRV_ENCODERS_H
#define DRV_ENCODERS_H

#include "encoders.h"

#define ENCODER_COUNT ENC_COUNT

void drv_encoders_init(void);
void drv_encoders_poll(void);

#endif
