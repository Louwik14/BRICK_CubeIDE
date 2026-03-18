#ifndef APP_HALL_HALL_ENGINE_H
#define APP_HALL_HALL_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

#define HALL_KEY_COUNT 16U

typedef enum
{
    HALL_VEL_MODE_DV_PEAK = 0U,
    HALL_VEL_MODE_TIME,
    HALL_VEL_MODE_ENERGY,
    HALL_VEL_MODE_COUNT
} hall_vel_mode_t;

typedef enum
{
    HALL_VEL_CURVE_LINEAR = 0U,
    HALL_VEL_CURVE_SOFT,
    HALL_VEL_CURVE_HARD,
    HALL_VEL_CURVE_LOG,
    HALL_VEL_CURVE_EXP,
    HALL_VEL_CURVE_COUNT
} hall_vel_curve_t;

void hall_engine_init(void);
void hall_engine_process(void);

void hall_engine_set_calibration(const uint16_t *min_values,
                                 const uint16_t *max_values);

uint16_t hall_engine_get_value(uint8_t key);

uint8_t hall_engine_is_pressed(uint8_t key);

bool hall_engine_get_note_on(uint8_t key);
bool hall_engine_get_note_off(uint8_t key);

uint8_t hall_engine_get_velocity(uint8_t key);
uint8_t hall_engine_get_pressure(uint8_t key);
uint8_t hall_engine_get_midi_value(uint8_t key);

void hall_engine_set_velocity_mode(uint8_t mode);
void hall_engine_set_velocity_curve(uint8_t curve);

uint8_t hall_engine_get_velocity_mode(void);
uint8_t hall_engine_get_velocity_curve(void);

uint16_t hall_engine_get_min(uint8_t key);
uint16_t hall_engine_get_max(uint8_t key);
uint16_t hall_engine_get_trig_lo(uint8_t key);
uint16_t hall_engine_get_trig_hi(uint8_t key);
uint8_t hall_engine_get_state(uint8_t key);
uint8_t hall_engine_get_velocity_latched(uint8_t key);
uint16_t hall_engine_get_debug_latched_raw(uint8_t key);
uint16_t hall_engine_get_debug_latched_prev_raw(uint8_t key);
uint16_t hall_engine_get_debug_latched_dv_peak(uint8_t key);
uint16_t hall_engine_get_debug_latched_sum_dv(uint8_t key);
uint16_t hall_engine_get_debug_latched_time_count(uint8_t key);
uint16_t hall_engine_get_debug_latched_trig_lo(uint8_t key);
uint16_t hall_engine_get_debug_latched_trig_hi(uint8_t key);
uint16_t hall_engine_get_debug_latched_vel_start_th(uint8_t key);
uint32_t hall_engine_get_debug_latched_sample_count(uint8_t key);

#endif
