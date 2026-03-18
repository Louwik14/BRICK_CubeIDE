#ifndef APP_HALL_HALL_ENGINE_H
#define APP_HALL_HALL_ENGINE_H

#include <stdint.h>
#include "App/Hall/hall_adc.h"

typedef struct
{
    uint16_t velocity_arm_threshold;
    uint16_t trigger_threshold;
    uint16_t raw_latched;
    uint32_t elapsed_samples_latched;
    uint32_t sample_count;
    uint16_t position_percent;
    uint8_t velocity_latched;
    uint8_t velocity_ready;
    uint8_t velocity_armed;
    uint8_t state;
} hall_velocity_debug_t;

void hall_engine_init(void);

void hall_engine_set_calibration(const uint16_t *min_values,
                                 const uint16_t *max_values);

void hall_engine_process(void);

uint16_t hall_engine_get_value(uint8_t key);
uint8_t hall_engine_is_pressed(uint8_t key);
uint16_t hall_engine_get_min(uint8_t key);
uint16_t hall_engine_get_max(uint8_t key);
uint8_t hall_engine_get_velocity(uint8_t key);
uint8_t hall_engine_get_velocity_valid(uint8_t key);
uint16_t hall_engine_get_velocity_position(uint8_t key);
void hall_engine_get_velocity_debug(uint8_t key, hall_velocity_debug_t *debug);

#endif /* APP_HALL_HALL_ENGINE_H */
