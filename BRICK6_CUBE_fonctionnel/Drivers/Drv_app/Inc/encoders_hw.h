#ifndef ENCODERS_HW_H
#define ENCODERS_HW_H

#include <stdint.h>

#include "encoders.h"

void encoders_hw_init(void);
void encoders_hw_read(void);
int8_t encoders_hw_get_delta(uint8_t encoder);

#endif
