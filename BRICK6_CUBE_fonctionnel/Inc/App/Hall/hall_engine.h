#ifndef APP_HALL_HALL_ENGINE_H
#define APP_HALL_HALL_ENGINE_H

#include <stdint.h>

#include "App/Hall/hall_adc.h"

void hall_engine_init(void);
void hall_engine_process(void);

void hall_engine_set_calibration(const uint16_t *min_values, const uint16_t *max_values);
void hall_engine_set_key_calibration(uint8_t key, uint16_t min_value, uint16_t max_value);

uint16_t hall_engine_get_raw(uint8_t key);
uint16_t hall_engine_get_filtered(uint8_t key);

uint8_t hall_engine_get_pressed(uint8_t key);
uint8_t hall_engine_consume_note_on(uint8_t key);
uint8_t hall_engine_consume_note_off(uint8_t key);
uint8_t hall_engine_get_note_on_latched(uint8_t key);
uint8_t hall_engine_get_note_off_latched(uint8_t key);
uint8_t hall_engine_get_velocity(uint8_t key);

uint8_t hall_engine_get_range_valid(uint8_t key);
uint16_t hall_engine_get_cal_min(uint8_t key);
uint16_t hall_engine_get_cal_max(uint8_t key);
uint16_t hall_engine_get_observed_min(uint8_t key);
uint16_t hall_engine_get_observed_max(uint8_t key);
uint16_t hall_engine_get_trigger_press(uint8_t key);
uint16_t hall_engine_get_trigger_release(uint8_t key);
uint16_t hall_engine_get_velocity_start(uint8_t key);
int16_t hall_engine_get_derivative(uint8_t key);
uint16_t hall_engine_get_derivative_peak(uint8_t key);
uint16_t hall_engine_get_attack_samples(uint8_t key);
uint32_t hall_engine_get_sample_count(uint8_t key);

#endif
