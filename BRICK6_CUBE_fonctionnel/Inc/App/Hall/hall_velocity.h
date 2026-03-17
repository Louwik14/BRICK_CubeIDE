#ifndef HALL_VELOCITY_H
#define HALL_VELOCITY_H

#include <stdint.h>

void hall_velocity_init(void);
void hall_velocity_process(void);

uint8_t hall_velocity_get(uint8_t key);

#endif
