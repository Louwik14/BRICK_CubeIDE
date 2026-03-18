#ifndef APP_HALL_HALL_ENGINE_H
#define APP_HALL_HALL_ENGINE_H

#include <stdint.h>
#include "App/Hall/hall_adc.h"

void hall_engine_init(void);

void hall_engine_set_calibration(const uint16_t *min_values,
                                 const uint16_t *max_values);

void hall_engine_process(void);

uint16_t hall_engine_get_value(uint8_t key);
uint8_t hall_engine_is_pressed(uint8_t key);
uint16_t hall_engine_get_min(uint8_t key);
uint16_t hall_engine_get_max(uint8_t key);

#endif /* APP_HALL_HALL_ENGINE_H */
