#ifndef APP_HALL_HALL_ENGINE_H
#define APP_HALL_HALL_ENGINE_H

#include <stdint.h>
#include "App/Hall/hall_adc.h"

#define HALL_UI_LANE_COUNT 16U
#define HALL_KEY_COUNT 24U

typedef enum
{
    HALL_VEL_MODE_DV_PEAK = 0,
    HALL_VEL_MODE_TIME    = 1,
    HALL_VEL_MODE_ENERGY  = 2,
    HALL_VEL_MODE_USER    = 3,

    HALL_VEL_MODE_COUNT
} hall_velocity_mode_t;

typedef enum
{
    HALL_VEL_PROFILE_DEFAULT = 0,
    HALL_VEL_PROFILE_USER    = 1,

    HALL_VEL_PROFILE_COUNT
} hall_velocity_profile_t;

typedef enum
{
    HALL_VEL_CURVE_LINEAR = 0,
    HALL_VEL_CURVE_SOFT   = 1,
    HALL_VEL_CURVE_HARD   = 2,
    HALL_VEL_CURVE_LOG    = 3,
    HALL_VEL_CURVE_EXP    = 4,

    HALL_VEL_CURVE_COUNT
} hall_velocity_curve_t;

typedef struct
{
    uint16_t q1;
    uint16_t median;
    uint16_t q3;
} hall_user_velocity_zone_t;

typedef struct
{
    hall_user_velocity_zone_t soft;
    hall_user_velocity_zone_t mid;
    hall_user_velocity_zone_t fort;
    uint8_t valid;
    uint8_t reserved0;
    uint16_t reserved1[2];
} hall_user_velocity_profile_t;

typedef struct
{
    uint8_t key;
    uint8_t reserved0;
    uint16_t metric;
    uint32_t tick_ms;
} hall_velocity_capture_t;

typedef struct
{
    uint16_t raw_current;
    uint16_t min_current;
    uint16_t max_current;
    uint16_t range_current;
    uint16_t position_percent;
    uint16_t trig_lo;
    uint16_t trig_hi;
    uint16_t prev_raw;
    uint16_t dv_peak;
    uint16_t sum_dv;
    uint16_t vel_start_th;
    uint16_t vel_end_th;
    uint16_t time_count;
    uint16_t last_metric;
    uint32_t sample_count;
    uint32_t sample_period_us;
    uint8_t  calibrated;
    uint8_t  range_valid;
    uint8_t  state;
    uint8_t  velocity;
    uint8_t  velocity_valid;
    uint8_t  time_active;
    uint8_t  velocity_mode;
    uint8_t  velocity_curve;
    uint8_t  user_profile_valid;
    uint8_t  user_mode_fallback;
    uint8_t  note_on_pending;
    uint8_t  note_off_pending;
} hall_velocity_debug_t;

void hall_engine_init(void);

void hall_engine_set_calibration(const uint16_t *min_values,
                                 const uint16_t *max_values);
void hall_engine_set_user_velocity_profile(const hall_user_velocity_profile_t *profile);
void hall_engine_get_user_velocity_profile(hall_user_velocity_profile_t *profile);
uint8_t hall_engine_user_velocity_profile_is_valid(void);

void hall_engine_process_sample(uint8_t key, uint16_t raw, uint32_t sample_count,
                                uint32_t tim5_tick);

uint16_t hall_engine_get_raw(uint8_t key);
uint16_t hall_engine_get_value(uint8_t key);
uint8_t hall_engine_is_pressed(uint8_t key);
uint16_t hall_engine_get_min(uint8_t key);
uint16_t hall_engine_get_max(uint8_t key);
uint16_t hall_engine_get_trig_lo(uint8_t key);
uint16_t hall_engine_get_trig_hi(uint8_t key);
uint8_t hall_engine_get_velocity(uint8_t key);
uint8_t hall_engine_get_velocity_valid(uint8_t key);
uint16_t hall_engine_get_velocity_position(uint8_t key);
uint32_t hall_engine_get_sample_count(uint8_t key);
uint8_t hall_engine_consume_note_on(uint8_t key);
uint8_t hall_engine_consume_note_off(uint8_t key);
void hall_engine_acknowledge_edge(uint8_t key, uint8_t pressed);
uint8_t hall_engine_pop_velocity_capture(hall_velocity_capture_t *capture);
void hall_engine_get_velocity_debug(uint8_t key, hall_velocity_debug_t *debug);

void hall_set_velocity_mode(uint8_t mode);
void hall_set_velocity_profile(uint8_t profile);
void hall_set_velocity_curve(uint8_t curve);
uint8_t hall_get_velocity_mode(void);
uint8_t hall_get_velocity_profile(void);
uint8_t hall_get_velocity_curve(void);

#endif /* APP_HALL_HALL_ENGINE_H */
