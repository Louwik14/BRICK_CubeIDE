#ifndef APP_HALL_HALL_ENGINE_H
#define APP_HALL_HALL_ENGINE_H

#include <stdint.h>
#include "App/Hall/hall_adc.h"

typedef struct
{
    uint16_t raw_current;
    uint16_t min_current;
    uint16_t max_current;
    uint16_t position_percent;
    uint16_t velocity1_arm_threshold;
    uint16_t trigger1_threshold;
    uint16_t velocity2_arm_threshold;
    uint16_t trigger2_threshold;
    uint16_t velocity1_raw_latched;
    uint16_t velocity2_raw_latched;
    uint32_t velocity1_elapsed_samples;
    uint32_t velocity2_elapsed_samples;
    uint32_t sample_count;
    uint32_t sample_period_us;
    uint8_t velocity_latched;
    uint8_t velocity2_latched;
    uint8_t velocity_ready;
    uint8_t velocity2_ready;
    uint8_t velocity1_armed;
    uint8_t velocity2_armed;
    uint8_t velocity1_fallback;
    uint8_t velocity2_fallback;
    uint8_t note_on_pending;
    uint8_t note_off_pending;
    uint8_t state;
} hall_velocity_debug_t;

void hall_engine_init(void);

void hall_engine_set_calibration(const uint16_t *min_values,
                                 const uint16_t *max_values);

void hall_engine_process_sample(uint8_t key, uint16_t raw, uint32_t sample_count);
void hall_engine_process(void);

uint16_t hall_engine_get_raw(uint8_t key);
uint16_t hall_engine_get_value(uint8_t key);
uint8_t hall_engine_is_pressed(uint8_t key);
uint16_t hall_engine_get_min(uint8_t key);
uint16_t hall_engine_get_max(uint8_t key);
uint8_t hall_engine_get_velocity(uint8_t key);
/* état durable : 1 tant que l'appui courant possède une vélocité valide */
uint8_t hall_engine_get_velocity_valid(uint8_t key);
uint16_t hall_engine_get_velocity_position(uint8_t key);
uint32_t hall_engine_get_sample_count(uint8_t key);
uint8_t hall_engine_consume_note_on(uint8_t key);
uint8_t hall_engine_consume_note_off(uint8_t key);
void hall_engine_get_velocity_debug(uint8_t key, hall_velocity_debug_t *debug);

#endif /* APP_HALL_HALL_ENGINE_H */
