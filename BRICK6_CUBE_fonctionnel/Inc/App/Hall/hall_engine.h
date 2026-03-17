#ifndef HALL_ENGINE_H
#define HALL_ENGINE_H

#include <stdint.h>

#ifndef HALL_KEY_COUNT
#define HALL_KEY_COUNT 16U
#endif

void hall_engine_init(void);

void hall_engine_set_calibration(const uint16_t *min_values,
                                 const uint16_t *max_values);

void hall_engine_process(void);

uint16_t hall_engine_get_value(uint8_t key);

uint8_t hall_engine_is_pressed(uint8_t key);

uint16_t hall_engine_get_min(uint8_t key);

uint16_t hall_engine_get_max(uint8_t key);

#endif
