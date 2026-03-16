#ifndef HALL_ON_OFF_H
#define HALL_ON_OFF_H

#include <stdint.h>
#include "App/Hall/hall_engine.h"   // pour HALL_KEY_COUNT

void hall_on_off_init(void);
void hall_on_off_process(void);

uint8_t hall_on_off_pressed(uint8_t key);
uint8_t hall_on_off_event(uint8_t key);

#endif
