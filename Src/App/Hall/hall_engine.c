#include "App/Hall/hall_engine.h"

#include "IPC/live_clock_control.h"
#include "IPC/live_event.h"
#include "stm32h7xx_hal.h"

#define HALL_THRESHOLD_PPM                 200U
#define HALL_HYST_PPM                      100U
#define HALL_MIN_RANGE                     400U

#define HALL_KEY_SAMPLE_PERIOD_US         2800U
#define HALL_VEL_TIME_FAST_DT                4U
#define HALL_VEL_TIME_SLOW_DT               56U

#define HALL_VEL_SLOW_SHIFT                5U
#define HALL_VEL_FAST_SHIFT                1U

#define HALL_VEL_TIME_START_PPM            150U
#define HALL_VEL_TIME_END_PPM              0U

#define HALL_VEL_ENERGY_SLOW_SHIFT         6U
#define HALL_VEL_ENERGY_FAST_SHIFT         2U

#define HALL_USER_CAPTURE_FIFO_LEN         16U
#define HALL_USER_VEL_POINT_COUNT          10U
#define HALL_PRESS_DECREASES_RAW           1U

typedef struct
{
    uint16_t min;
    uint16_t max;
    uint16_t trig_lo;
    uint16_t trig_hi;
    uint8_t  prev_out;
    uint8_t  curr_out;
    uint16_t prev_raw;
    uint16_t dv_peak;
    uint16_t sum_dv;
    uint16_t vel_start_th;
    uint16_t vel_end_th;
    uint16_t time_count;
    uint8_t  time_active;
} hall_button_t;

typedef struct
{
    uint16_t x;
    uint8_t y;
} hall_user_curve_point_t;

static volatile uint16_t hall_min[HALL_KEY_COUNT];
static volatile uint16_t hall_max[HALL_KEY_COUNT];
static volatile uint16_t hall_trig_lo[HALL_KEY_COUNT];
static volatile uint16_t hall_trig_hi[HALL_KEY_COUNT];
static volatile uint16_t hall_value[HALL_KEY_COUNT];
static volatile uint16_t hall_position[HALL_KEY_COUNT];
static volatile uint16_t hall_raw_current[HALL_KEY_COUNT];
static volatile uint32_t hall_sample_count_current[HALL_KEY_COUNT];
static volatile uint8_t  hall_pressed[HALL_KEY_COUNT];
static volatile uint8_t  hall_velocity[HALL_KEY_COUNT];
static volatile uint8_t  hall_velocity_valid[HALL_KEY_COUNT];
static volatile uint16_t hall_last_metric[HALL_KEY_COUNT];
static volatile uint8_t  hall_note_on_pending[HALL_KEY_COUNT];
static volatile uint8_t  hall_note_off_pending[HALL_KEY_COUNT];

static volatile hall_button_t hall_buttons[HALL_KEY_COUNT];
static volatile uint8_t hall_calibrated = 0U;
static volatile hall_velocity_mode_t  g_velocity_mode = HALL_VEL_MODE_DV_PEAK;
static volatile hall_velocity_profile_t g_velocity_profile = HALL_VEL_PROFILE_USER;
static volatile hall_velocity_curve_t g_velocity_curve = HALL_VEL_CURVE_LOG;
static volatile hall_user_velocity_profile_t g_user_velocity_profile = {0};
static volatile uint8_t g_user_mode_fallback = 0U;
static volatile hall_velocity_capture_t g_velocity_capture_fifo[HALL_USER_CAPTURE_FIFO_LEN];
static volatile uint8_t g_velocity_capture_w = 0U;
static volatile uint8_t g_velocity_capture_r = 0U;

static uint32_t hall_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void hall_exit_critical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

static uint8_t hall_consume_flag(volatile uint8_t *flag)
{
    uint8_t pending;
    const uint32_t primask = hall_enter_critical();

    pending = *flag;
    *flag = 0U;

    hall_exit_critical(primask);
    return pending;
}

static void hall_publish_edge(uint8_t key, uint8_t pressed,
                              uint8_t velocity, uint32_t tim5_tick)
{
    if (live_event_submit_from_hall(key, pressed != 0U, velocity, tim5_tick))
    {
        if (pressed != 0U)
        {
            hall_note_on_pending[key] = 1U;
        }
        else
        {
            hall_note_off_pending[key] = 1U;
        }
    }
}

static void hall_velocity_capture_flush(void)
{
    const uint32_t primask = hall_enter_critical();

    g_velocity_capture_w = 0U;
    g_velocity_capture_r = 0U;

    hall_exit_critical(primask);
}

static void hall_velocity_capture_push(uint8_t key, uint16_t metric)
{
    const uint8_t next = (uint8_t)((g_velocity_capture_w + 1U) & (HALL_USER_CAPTURE_FIFO_LEN - 1U));

    if (next == g_velocity_capture_r)
    {
        g_velocity_capture_r = (uint8_t)((g_velocity_capture_r + 1U) & (HALL_USER_CAPTURE_FIFO_LEN - 1U));
    }

    g_velocity_capture_fifo[g_velocity_capture_w].key = key;
    g_velocity_capture_fifo[g_velocity_capture_w].reserved0 = 0U;
    g_velocity_capture_fifo[g_velocity_capture_w].metric = metric;
    g_velocity_capture_fifo[g_velocity_capture_w].tick_ms = HAL_GetTick();
    g_velocity_capture_w = next;
}

static uint16_t hall_isqrt_u32(uint32_t value)
{
    uint32_t op = value;
    uint32_t res = 0U;
    uint32_t one = 1UL << 30;

    while (one > op)
    {
        one >>= 2;
    }

    while (one != 0U)
    {
        if (op >= (res + one))
        {
            op -= (res + one);
            res += (2U * one);
        }

        res >>= 1;
        one >>= 2;
    }

    return (uint16_t)res;
}

static uint8_t hall_apply_curve(uint8_t velocity, hall_velocity_curve_t curve)
{
    uint32_t out;

    if (velocity < 1U)
    {
        velocity = 1U;
    }
    if (velocity > 127U)
    {
        velocity = 127U;
    }

    switch (curve)
    {
        case HALL_VEL_CURVE_LINEAR:
            return velocity;

        case HALL_VEL_CURVE_SOFT:
            out = ((uint32_t)velocity * (uint32_t)velocity) / 127U;
            break;

        case HALL_VEL_CURVE_HARD:
            out = hall_isqrt_u32((uint32_t)velocity * 127U);
            break;

        case HALL_VEL_CURVE_LOG:
        {
            const uint32_t delta = (uint32_t)(127U - velocity);
            out = 127U - ((delta * delta) / 127U);
        }
            break;

        case HALL_VEL_CURVE_EXP:
            out = ((uint32_t)velocity * (uint32_t)velocity * (uint32_t)velocity) /
                  (127U * 127U);
            break;

        default:
            out = velocity;
            break;
    }

    if (out < 1U)
    {
        out = 1U;
    }
    if (out > 127U)
    {
        out = 127U;
    }

    return (uint8_t)out;
}

static uint8_t hall_range_is_valid(uint16_t min_value, uint16_t max_value)
{
    if (max_value <= min_value)
    {
        return 0U;
    }

    return (((uint16_t)(max_value - min_value)) >= HALL_MIN_RANGE) ? 1U : 0U;
}

static uint16_t hall_compute_position_percent(uint16_t raw,
                                              uint16_t min_value,
                                              uint16_t max_value)
{
    uint16_t range;
    uint32_t delta;
    uint32_t limited_delta;
    uint16_t position_percent;

    if (hall_range_is_valid(min_value, max_value) == 0U)
    {
        return 0U;
    }

    range = (uint16_t)(max_value - min_value);
    delta = (raw < max_value) ? (uint32_t)(max_value - raw) : 0U;
    limited_delta = (delta > range) ? range : delta;
    position_percent = (uint16_t)((limited_delta * 100U) / range);

    if (position_percent > 100U)
    {
        position_percent = 100U;
    }

    return position_percent;
}

static void hall_reset_attack_runtime(uint8_t key)
{
    hall_buttons[key].prev_raw = hall_raw_current[key];
    hall_buttons[key].dv_peak = 0U;
    hall_buttons[key].sum_dv = 0U;
    hall_buttons[key].vel_start_th = hall_min[key];
    hall_buttons[key].vel_end_th = hall_max[key];
    hall_buttons[key].time_count = 0U;
    hall_buttons[key].time_active = 0U;
}

static void hall_clear_velocity_state(uint8_t key)
{
    hall_velocity[key] = 0U;
    hall_velocity_valid[key] = 0U;
    hall_last_metric[key] = 0U;
}

static void hall_engine_reset_key_runtime(uint8_t key)
{
    hall_trig_lo[key] = hall_min[key];
    hall_trig_hi[key] = hall_max[key];
    hall_value[key] = 0U;
    hall_position[key] = 0U;
    hall_pressed[key] = 0U;
    hall_clear_velocity_state(key);
    hall_note_on_pending[key] = 0U;
    hall_note_off_pending[key] = 0U;

    hall_buttons[key].min = hall_min[key];
    hall_buttons[key].max = hall_max[key];
    hall_buttons[key].trig_lo = hall_trig_lo[key];
    hall_buttons[key].trig_hi = hall_trig_hi[key];
    hall_buttons[key].prev_out = 0U;
    hall_buttons[key].curr_out = 0U;
    hall_reset_attack_runtime(key);
}

static void hall_engine_invalidate_key_state(uint8_t key, uint8_t emit_note_off,
                                             uint32_t tim5_tick)
{
    const uint8_t was_pressed = hall_buttons[key].curr_out;

    hall_trig_lo[key] = hall_min[key];
    hall_trig_hi[key] = hall_max[key];
    hall_value[key] = 0U;
    hall_position[key] = 0U;
    hall_pressed[key] = 0U;
    if ((emit_note_off != 0U) && (was_pressed != 0U))
        hall_publish_edge(key, 0U, 0U, tim5_tick);

    hall_buttons[key].min = hall_min[key];
    hall_buttons[key].max = hall_max[key];
    hall_buttons[key].trig_lo = hall_trig_lo[key];
    hall_buttons[key].trig_hi = hall_trig_hi[key];
    hall_buttons[key].prev_out = was_pressed;
    hall_buttons[key].curr_out = 0U;
    hall_clear_velocity_state(key);
    hall_reset_attack_runtime(key);
}

static void hall_update_triggers(uint8_t key)
{
    uint32_t range;
    uint32_t half_hyst;
    uint32_t lo_ppm;
    uint32_t hi_ppm;

    hall_buttons[key].min = hall_min[key];
    hall_buttons[key].max = hall_max[key];

    if (hall_range_is_valid(hall_min[key], hall_max[key]) == 0U)
    {
        hall_buttons[key].trig_lo = hall_min[key];
        hall_buttons[key].trig_hi = hall_max[key];
        hall_buttons[key].vel_start_th = hall_min[key];
        hall_buttons[key].vel_end_th = hall_max[key];
        hall_trig_lo[key] = hall_buttons[key].trig_lo;
        hall_trig_hi[key] = hall_buttons[key].trig_hi;
        return;
    }

    range = (uint32_t)(hall_max[key] - hall_min[key]);
    half_hyst = HALL_HYST_PPM / 2U;
    lo_ppm = HALL_THRESHOLD_PPM;
    hi_ppm = HALL_THRESHOLD_PPM;

    if (lo_ppm > half_hyst)
    {
        lo_ppm -= half_hyst;
    }
    else
    {
        lo_ppm = 0U;
    }

    hi_ppm += half_hyst;
    if (hi_ppm > 1000U)
    {
        hi_ppm = 1000U;
    }

    hall_buttons[key].trig_lo = (uint16_t)(hall_max[key] - ((range * hi_ppm) / 1000U));
    hall_buttons[key].trig_hi = (uint16_t)(hall_max[key] - ((range * lo_ppm) / 1000U));
    hall_buttons[key].vel_start_th = (uint16_t)(hall_max[key] -
                                  ((range * HALL_VEL_TIME_START_PPM) / 1000U));

    if (HALL_VEL_TIME_END_PPM == 0U)
    {
        hall_buttons[key].vel_end_th =
            hall_buttons[key].trig_lo;
    }
    else
    {
        hall_buttons[key].vel_end_th = (uint16_t)(hall_max[key] -
                                    ((range * HALL_VEL_TIME_END_PPM) / 1000U));
    }

    hall_trig_lo[key] = hall_buttons[key].trig_lo;
    hall_trig_hi[key] = hall_buttons[key].trig_hi;
}

static uint8_t hall_velocity_from_dv(uint16_t range, uint16_t dv_peak)
{
    uint16_t dv_slow;
    uint16_t dv_fast;
    uint32_t velocity_value;

    if (range == 0U)
    {
        return 1U;
    }

    dv_slow = (uint16_t)(range >> HALL_VEL_SLOW_SHIFT);
    dv_fast = (uint16_t)(range >> HALL_VEL_FAST_SHIFT);

    if (dv_slow < 1U)
    {
        dv_slow = 1U;
    }
    if (dv_fast <= (uint16_t)(dv_slow + 1U))
    {
        dv_fast = (uint16_t)(dv_slow + 2U);
    }

    if (dv_peak <= dv_slow)
    {
        return 1U;
    }
    if (dv_peak >= dv_fast)
    {
        return 127U;
    }

    velocity_value = 1U + (((uint32_t)(dv_peak - dv_slow) * 126U) /
                           (uint32_t)(dv_fast - dv_slow));

    if (velocity_value > 127U)
    {
        velocity_value = 127U;
    }

    return (uint8_t)velocity_value;
}

static uint8_t hall_velocity_from_time(uint16_t dt_count)
{
    uint32_t velocity_value;

    if (dt_count <= HALL_VEL_TIME_FAST_DT)
    {
        return 127U;
    }
    if (dt_count >= HALL_VEL_TIME_SLOW_DT)
    {
        return 1U;
    }

    velocity_value = 1U + (((uint32_t)(HALL_VEL_TIME_SLOW_DT - dt_count) * 126U) /
                           (uint32_t)(HALL_VEL_TIME_SLOW_DT - HALL_VEL_TIME_FAST_DT));

    if (velocity_value > 127U)
    {
        velocity_value = 127U;
    }

    return (uint8_t)velocity_value;
}

static uint8_t hall_velocity_from_energy(uint16_t range, uint16_t sum_dv)
{
    uint16_t energy_slow;
    uint16_t energy_fast;
    uint32_t velocity_value;

    if (range == 0U)
    {
        return 1U;
    }

    energy_slow = (uint16_t)(range >> HALL_VEL_ENERGY_SLOW_SHIFT);
    energy_fast = (uint16_t)(range >> HALL_VEL_ENERGY_FAST_SHIFT);

    if (energy_slow < 1U)
    {
        energy_slow = 1U;
    }
    if (energy_fast <= (uint16_t)(energy_slow + 1U))
    {
        energy_fast = (uint16_t)(energy_slow + 2U);
    }

    if (sum_dv <= energy_slow)
    {
        return 1U;
    }
    if (sum_dv >= energy_fast)
    {
        return 127U;
    }

    velocity_value = 1U + (((uint32_t)(sum_dv - energy_slow) * 126U) /
                           (uint32_t)(energy_fast - energy_slow));

    if (velocity_value > 127U)
    {
        velocity_value = 127U;
    }

    return (uint8_t)velocity_value;
}

static uint16_t hall_user_velocity_zone_last(const hall_user_velocity_zone_t *zone)
{
    if (zone == 0)
    {
        return 0U;
    }

    if (zone->q3 >= zone->median)
    {
        return zone->q3;
    }

    return zone->median;
}

static uint8_t hall_user_velocity_profile_is_usable(const hall_user_velocity_profile_t *profile)
{
    if ((profile == 0) || (profile->valid == 0U))
    {
        return 0U;
    }

    if ((profile->soft.q1 == 0U) || (profile->mid.q1 == 0U) || (profile->fort.q1 == 0U))
    {
        return 0U;
    }

    if ((profile->soft.q1 > profile->soft.median) || (profile->soft.median > profile->soft.q3))
    {
        return 0U;
    }
    if ((profile->mid.q1 > profile->mid.median) || (profile->mid.median > profile->mid.q3))
    {
        return 0U;
    }
    if ((profile->fort.q1 > profile->fort.median) || (profile->fort.median > profile->fort.q3))
    {
        return 0U;
    }

    if ((profile->soft.median + 4U) >= profile->mid.median)
    {
        return 0U;
    }
    if ((profile->mid.median + 4U) >= profile->fort.median)
    {
        return 0U;
    }

    if (hall_user_velocity_zone_last(&profile->soft) >= profile->fort.q1)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t hall_user_velocity_interpolate(uint16_t metric,
                                              const hall_user_curve_point_t *points,
                                              uint8_t count)
{
    uint8_t idx;

    if ((points == 0) || (count == 0U))
    {
        return 1U;
    }

    if (metric <= points[0].x)
    {
        return points[0].y;
    }

    for (idx = 1U; idx < count; idx++)
    {
        if (metric <= points[idx].x)
        {
            const uint16_t x0 = points[(uint8_t)(idx - 1U)].x;
            const uint16_t x1 = points[idx].x;
            const uint8_t y0 = points[(uint8_t)(idx - 1U)].y;
            const uint8_t y1 = points[idx].y;

            if (x1 <= x0)
            {
                return y1;
            }

            return (uint8_t)(y0 + (((uint32_t)(metric - x0) * (uint32_t)(y1 - y0)) /
                                   (uint32_t)(x1 - x0)));
        }
    }

    return 127U;
}

static uint8_t hall_velocity_from_user_profile(uint16_t dv_peak)
{
    static const uint8_t point_y[HALL_USER_VEL_POINT_COUNT] = { 1U, 14U, 26U, 38U, 54U,
                                                                72U, 90U, 104U, 116U, 127U };
    hall_user_curve_point_t points[HALL_USER_VEL_POINT_COUNT];
    hall_user_velocity_profile_t profile;

    hall_engine_get_user_velocity_profile(&profile);

    points[0].x = 0U;
    points[0].y = point_y[0];
    points[1].x = profile.soft.q1;
    points[1].y = point_y[1];
    points[2].x = profile.soft.median;
    points[2].y = point_y[2];
    points[3].x = profile.soft.q3;
    points[3].y = point_y[3];
    points[4].x = profile.mid.q1;
    points[4].y = point_y[4];
    points[5].x = profile.mid.median;
    points[5].y = point_y[5];
    points[6].x = profile.mid.q3;
    points[6].y = point_y[6];
    points[7].x = profile.fort.q1;
    points[7].y = point_y[7];
    points[8].x = profile.fort.median;
    points[8].y = point_y[8];
    points[9].x = profile.fort.q3;
    points[9].y = point_y[9];

    return hall_user_velocity_interpolate(dv_peak, points, HALL_USER_VEL_POINT_COUNT);
}

static uint8_t hall_velocity_compute(uint8_t key, uint16_t range)
{
    uint8_t velocity_value = 1U;
    const hall_velocity_mode_t mode = g_velocity_mode;
    const hall_velocity_curve_t curve = g_velocity_curve;

    g_user_mode_fallback = 0U;

    if (g_velocity_profile == HALL_VEL_PROFILE_USER)
    {
        if (hall_engine_user_velocity_profile_is_valid() != 0U)
        {
            return hall_velocity_from_user_profile(hall_buttons[key].dv_peak);
        }

        g_user_mode_fallback = 1U;
    }

    switch (mode)
    {
        case HALL_VEL_MODE_TIME:
            velocity_value = hall_velocity_from_time(hall_buttons[key].time_count);
            return hall_apply_curve(velocity_value, curve);

        case HALL_VEL_MODE_ENERGY:
            velocity_value = hall_velocity_from_energy(range, hall_buttons[key].sum_dv);
            return hall_apply_curve(velocity_value, curve);

        case HALL_VEL_MODE_USER:
            if (hall_engine_user_velocity_profile_is_valid() != 0U)
            {
                return hall_velocity_from_user_profile(hall_buttons[key].dv_peak);
            }

            g_user_mode_fallback = 1U;
            velocity_value = hall_velocity_from_dv(range, hall_buttons[key].dv_peak);
            return hall_apply_curve(velocity_value, curve);

        case HALL_VEL_MODE_DV_PEAK:
        default:
            velocity_value = hall_velocity_from_dv(range, hall_buttons[key].dv_peak);
            return hall_apply_curve(velocity_value, curve);
    }
}

void hall_engine_init(void)
{
    const uint32_t primask = hall_enter_critical();

    hall_calibrated = 0U;
    g_user_mode_fallback = 0U;
    g_user_velocity_profile.valid = 0U;
    g_user_velocity_profile.reserved0 = 0U;
    g_user_velocity_profile.reserved1[0] = 0U;
    g_user_velocity_profile.reserved1[1] = 0U;
    g_velocity_capture_w = 0U;
    g_velocity_capture_r = 0U;

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_min[i] = 0xFFFFU;
        hall_max[i] = 0U;
        hall_raw_current[i] = 0U;
        hall_sample_count_current[i] = 0U;
        hall_engine_reset_key_runtime(i);
    }

    hall_exit_critical(primask);
}

void hall_engine_set_calibration(const uint16_t *min_values,
                                 const uint16_t *max_values)
{
    const uint32_t primask = hall_enter_critical();

    if ((min_values == 0) || (max_values == 0))
    {
        hall_exit_critical(primask);
        return;
    }

    hall_calibrated = 0U;
    const uint32_t tim5_tick = live_clock_capture_tick();

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_min[i] = min_values[i];
        hall_max[i] = max_values[i];
        hall_engine_invalidate_key_state(i, 1U, tim5_tick);
        hall_update_triggers(i);
    }

    hall_velocity_capture_flush();
    hall_calibrated = 1U;
    hall_exit_critical(primask);
}

void hall_engine_set_user_velocity_profile(const hall_user_velocity_profile_t *profile)
{
    const uint32_t primask = hall_enter_critical();

    if (profile == 0)
    {
        g_user_velocity_profile.valid = 0U;
        g_user_velocity_profile.reserved0 = 0U;
        g_user_velocity_profile.reserved1[0] = 0U;
        g_user_velocity_profile.reserved1[1] = 0U;
        hall_exit_critical(primask);
        return;
    }

    if (hall_user_velocity_profile_is_usable(profile) != 0U)
    {
        g_user_velocity_profile = *profile;
        g_user_velocity_profile.valid = 1U;
    }
    else
    {
        g_user_velocity_profile.valid = 0U;
        g_user_velocity_profile.reserved0 = 0U;
        g_user_velocity_profile.reserved1[0] = 0U;
        g_user_velocity_profile.reserved1[1] = 0U;
    }

    hall_exit_critical(primask);
}

void hall_engine_get_user_velocity_profile(hall_user_velocity_profile_t *profile)
{
    const uint32_t primask = hall_enter_critical();

    if (profile == 0)
    {
        hall_exit_critical(primask);
        return;
    }

    *profile = g_user_velocity_profile;
    hall_exit_critical(primask);
}

uint8_t hall_engine_user_velocity_profile_is_valid(void)
{
    hall_user_velocity_profile_t profile;

    hall_engine_get_user_velocity_profile(&profile);
    return hall_user_velocity_profile_is_usable(&profile);
}

void hall_engine_process_sample(uint8_t key, uint16_t raw, uint32_t sample_count,
                                uint32_t tim5_tick)
{
    uint16_t range;
    uint16_t dv = 0U;

    if (key >= HALL_KEY_COUNT)
    {
        return;
    }

    hall_raw_current[key] = raw;
    hall_sample_count_current[key] = sample_count;

    if (hall_calibrated == 0U)
    {
        hall_engine_invalidate_key_state(key, 1U, tim5_tick);
        hall_buttons[key].prev_raw = raw;
        return;
    }

    hall_update_triggers(key);

    if (hall_range_is_valid(hall_min[key], hall_max[key]) == 0U)
    {
        hall_engine_invalidate_key_state(key, 1U, tim5_tick);
        hall_buttons[key].prev_raw = raw;
        return;
    }

    range = (uint16_t)(hall_max[key] - hall_min[key]);
    hall_position[key] = hall_compute_position_percent(raw, hall_min[key], hall_max[key]);
    hall_value[key] = hall_position[key];

    hall_buttons[key].prev_out = hall_buttons[key].curr_out;

    if (HALL_PRESS_DECREASES_RAW != 0U)
    {
        if (raw < hall_buttons[key].prev_raw)
        {
            dv = (uint16_t)(hall_buttons[key].prev_raw - raw);
        }
    }
    else if (raw > hall_buttons[key].prev_raw)
    {
        dv = (uint16_t)(raw - hall_buttons[key].prev_raw);
    }
    hall_buttons[key].prev_raw = raw;

    if ((hall_buttons[key].curr_out == 0U)
            &&
            (raw >= hall_buttons[key].trig_hi)
       )
    {
        hall_buttons[key].dv_peak = 0U;
        hall_buttons[key].sum_dv = 0U;
        hall_buttons[key].time_count = 0U;
        hall_buttons[key].time_active = 0U;
    }

    if (hall_buttons[key].curr_out == 0U)
    {
        uint32_t sum;

        if (dv > hall_buttons[key].dv_peak)
        {
            hall_buttons[key].dv_peak = dv;
        }

        sum = (uint32_t)hall_buttons[key].sum_dv + (uint32_t)dv;
        if (sum > 65535U)
        {
            sum = 65535U;
        }
        hall_buttons[key].sum_dv = (uint16_t)sum;

        if ((hall_buttons[key].time_active == 0U)
                &&
                (raw <= hall_buttons[key].vel_start_th)
           )
        {
            hall_buttons[key].time_active = 1U;
            hall_buttons[key].time_count = 0U;
        }

        if (hall_buttons[key].time_active != 0U)
        {
            if (hall_buttons[key].time_count < 65535U)
            {
                hall_buttons[key].time_count++;
            }

            if (
                    raw <= hall_buttons[key].vel_end_th
               )
            {
                hall_buttons[key].time_active = 0U;
            }
        }
    }

    if ((hall_buttons[key].curr_out == 0U)
            &&
            (raw <= hall_buttons[key].trig_lo)
       )
    {
        hall_buttons[key].curr_out = 1U;
        hall_last_metric[key] = hall_buttons[key].dv_peak;
        hall_velocity[key] = hall_velocity_compute(key, range);
    hall_velocity_valid[key] = 1U;
    }
    else if ((hall_buttons[key].curr_out != 0U)
             &&
             (raw >= hall_buttons[key].trig_hi)
            )
    {
        hall_buttons[key].curr_out = 0U;
    }

    hall_pressed[key] = hall_buttons[key].curr_out;

    if ((hall_buttons[key].prev_out == 0U) && (hall_buttons[key].curr_out == 1U))
    {
        hall_publish_edge(key, 1U, hall_velocity[key], tim5_tick);
        hall_velocity_capture_push(key, hall_last_metric[key]);
    }
    else if ((hall_buttons[key].prev_out != 0U) && (hall_buttons[key].curr_out == 0U))
    {
        hall_publish_edge(key, 0U, 0U, tim5_tick);
        hall_clear_velocity_state(key);
        hall_buttons[key].dv_peak = 0U;
        hall_buttons[key].sum_dv = 0U;
        hall_buttons[key].time_count = 0U;
        hall_buttons[key].time_active = 0U;
    }
}

uint16_t hall_engine_get_raw(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_raw_current[key];
}

uint16_t hall_engine_get_value(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_value[key];
}

uint8_t hall_engine_is_pressed(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_pressed[key];
}

uint16_t hall_engine_get_min(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_min[key];
}

uint16_t hall_engine_get_max(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_max[key];
}

uint16_t hall_engine_get_trig_lo(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_trig_lo[key];
}

uint16_t hall_engine_get_trig_hi(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_trig_hi[key];
}

uint8_t hall_engine_get_velocity(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_velocity[key];
}

uint8_t hall_engine_get_velocity_valid(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_velocity_valid[key];
}

uint16_t hall_engine_get_velocity_position(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_position[key];
}

uint32_t hall_engine_get_sample_count(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_sample_count_current[key];
}

uint8_t hall_engine_consume_note_on(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_consume_flag(&hall_note_on_pending[key]);
}

uint8_t hall_engine_consume_note_off(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_consume_flag(&hall_note_off_pending[key]);
}

void hall_engine_acknowledge_edge(uint8_t key, uint8_t pressed)
{
    if (key >= HALL_KEY_COUNT)
    {
        return;
    }

    const uint32_t primask = hall_enter_critical();
    if (pressed != 0U)
    {
        hall_note_on_pending[key] = 0U;
    }
    else
    {
        hall_note_off_pending[key] = 0U;
    }
    hall_exit_critical(primask);
}

uint8_t hall_engine_pop_velocity_capture(hall_velocity_capture_t *capture)
{
    const uint32_t primask = hall_enter_critical();

    if (capture == 0)
    {
        hall_exit_critical(primask);
        return 0U;
    }

    if (g_velocity_capture_r == g_velocity_capture_w)
    {
        hall_exit_critical(primask);
        return 0U;
    }

    *capture = g_velocity_capture_fifo[g_velocity_capture_r];
    g_velocity_capture_r = (uint8_t)((g_velocity_capture_r + 1U) & (HALL_USER_CAPTURE_FIFO_LEN - 1U));
    hall_exit_critical(primask);
    return 1U;
}

void hall_set_velocity_mode(uint8_t mode)
{
    if (mode < (uint8_t)HALL_VEL_MODE_USER)
    {
        const uint32_t primask = hall_enter_critical();
        g_velocity_mode = (hall_velocity_mode_t)mode;
        hall_exit_critical(primask);
    }
}

void hall_set_velocity_profile(uint8_t profile)
{
    if (profile < (uint8_t)HALL_VEL_PROFILE_COUNT)
    {
        const uint32_t primask = hall_enter_critical();
        g_velocity_profile = (hall_velocity_profile_t)profile;
        hall_exit_critical(primask);
    }
}

void hall_set_velocity_curve(uint8_t curve)
{
    if (curve < (uint8_t)HALL_VEL_CURVE_COUNT)
    {
        const uint32_t primask = hall_enter_critical();
        g_velocity_curve = (hall_velocity_curve_t)curve;
        hall_exit_critical(primask);
    }
}

uint8_t hall_get_velocity_mode(void)
{
    return (uint8_t)g_velocity_mode;
}

uint8_t hall_get_velocity_profile(void)
{
    return (uint8_t)g_velocity_profile;
}

uint8_t hall_get_velocity_curve(void)
{
    return (uint8_t)g_velocity_curve;
}

void hall_engine_get_velocity_debug(uint8_t key, hall_velocity_debug_t *debug)
{
    const uint32_t primask = hall_enter_critical();
    uint16_t raw_current;
    uint16_t min_current;
    uint16_t max_current;

    if ((key >= HALL_KEY_COUNT) || (debug == 0))
    {
        hall_exit_critical(primask);
        return;
    }

    raw_current = hall_raw_current[key];
    min_current = hall_min[key];
    max_current = hall_max[key];

    debug->raw_current = raw_current;
    debug->min_current = min_current;
    debug->max_current = max_current;
    debug->range_current = (max_current > min_current) ?
                           (uint16_t)(max_current - min_current) : 0U;
    debug->position_percent = hall_compute_position_percent(raw_current, min_current, max_current);
    debug->trig_lo = hall_trig_lo[key];
    debug->trig_hi = hall_trig_hi[key];
    debug->prev_raw = hall_buttons[key].prev_raw;
    debug->dv_peak = hall_buttons[key].dv_peak;
    debug->sum_dv = hall_buttons[key].sum_dv;
    debug->vel_start_th = hall_buttons[key].vel_start_th;
    debug->vel_end_th = hall_buttons[key].vel_end_th;
    debug->time_count = hall_buttons[key].time_count;
    debug->last_metric = hall_last_metric[key];
    debug->sample_count = hall_sample_count_current[key];
    debug->sample_period_us = HALL_KEY_SAMPLE_PERIOD_US;
    debug->calibrated = hall_calibrated;
    debug->range_valid = hall_range_is_valid(hall_min[key], hall_max[key]);
    debug->state = hall_buttons[key].curr_out;
    debug->velocity = hall_velocity[key];
    debug->velocity_valid = hall_velocity_valid[key];
    debug->time_active = hall_buttons[key].time_active;
    debug->velocity_mode = (uint8_t)g_velocity_mode;
    debug->velocity_curve = (uint8_t)g_velocity_curve;
    debug->user_profile_valid = g_user_velocity_profile.valid;
    debug->user_mode_fallback = g_user_mode_fallback;
    debug->note_on_pending = hall_note_on_pending[key];
    debug->note_off_pending = hall_note_off_pending[key];

    hall_exit_critical(primask);
}
