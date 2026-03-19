#include "App/Hall/hall_engine.h"

#include "stm32h7xx_hal.h"

#define HALL_THRESHOLD_PPM          200U
#define HALL_HYST_PPM                40U
#define HALL_MIN_RANGE              500U

#define HALL_KEY_SAMPLE_PERIOD_US   800U

#define HALL_VEL_SLOW_SHIFT          12U
#define HALL_VEL_FAST_SHIFT           2U

#define HALL_VEL_TIME_START_PPM     150U
#define HALL_VEL_TIME_END_PPM         0U
#define HALL_VEL_TIME_FAST_DT         2U
#define HALL_VEL_TIME_SLOW_DT        14U

#define HALL_VEL_ENERGY_SLOW_SHIFT    6U
#define HALL_VEL_ENERGY_FAST_SHIFT    2U

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
static volatile uint8_t  hall_note_on_pending[HALL_KEY_COUNT];
static volatile uint8_t  hall_note_off_pending[HALL_KEY_COUNT];

static volatile hall_button_t hall_buttons[HALL_KEY_COUNT];
static volatile uint8_t hall_calibrated = 0U;
static volatile hall_velocity_mode_t  g_velocity_mode = HALL_VEL_MODE_TIME;
static volatile hall_velocity_curve_t g_velocity_curve = HALL_VEL_CURVE_SOFT;

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

static void hall_engine_invalidate_key_state(uint8_t key, uint8_t emit_note_off)
{
    const uint8_t was_pressed = hall_buttons[key].curr_out;

    hall_value[key] = 0U;
    hall_position[key] = 0U;
    hall_pressed[key] = 0U;
    hall_note_on_pending[key] = 0U;

    if ((emit_note_off != 0U) && (was_pressed != 0U))
    {
        hall_note_off_pending[key] = 1U;
    }
    else
    {
        hall_note_off_pending[key] = 0U;
    }

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

    hall_buttons[key].trig_lo = (uint16_t)(hall_min[key] + ((range * lo_ppm) / 1000U));
    hall_buttons[key].trig_hi = (uint16_t)(hall_min[key] + ((range * hi_ppm) / 1000U));
    hall_buttons[key].vel_start_th = (uint16_t)(hall_min[key] +
                                  ((range * HALL_VEL_TIME_START_PPM) / 1000U));

    if (HALL_VEL_TIME_END_PPM == 0U)
    {
        hall_buttons[key].vel_end_th = hall_buttons[key].trig_hi;
    }
    else
    {
        hall_buttons[key].vel_end_th = (uint16_t)(hall_min[key] +
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

static uint8_t hall_velocity_compute(uint8_t key, uint16_t range)
{
    uint8_t velocity_value = 1U;
    const hall_velocity_mode_t mode = g_velocity_mode;
    const hall_velocity_curve_t curve = g_velocity_curve;

    switch (mode)
    {
        case HALL_VEL_MODE_TIME:
            velocity_value = hall_velocity_from_time(hall_buttons[key].time_count);
            break;

        case HALL_VEL_MODE_ENERGY:
            velocity_value = hall_velocity_from_energy(range, hall_buttons[key].sum_dv);
            break;

        case HALL_VEL_MODE_DV_PEAK:
        default:
            velocity_value = hall_velocity_from_dv(range, hall_buttons[key].dv_peak);
            break;
    }

    return hall_apply_curve(velocity_value, curve);
}

void hall_engine_init(void)
{
    const uint32_t primask = hall_enter_critical();

    hall_calibrated = 0U;

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

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_min[i] = min_values[i];
        hall_max[i] = max_values[i];
        hall_engine_invalidate_key_state(i, 1U);
        hall_update_triggers(i);
    }

    hall_calibrated = 1U;
    hall_exit_critical(primask);
}

void hall_engine_process_sample(uint8_t key, uint16_t raw, uint32_t sample_count)
{
    uint16_t range;
    uint32_t delta;
    uint32_t limited_delta;
    uint16_t dv = 0U;

    if (key >= HALL_KEY_COUNT)
    {
        return;
    }

    hall_raw_current[key] = raw;
    hall_sample_count_current[key] = sample_count;

    if (hall_calibrated == 0U)
    {
        hall_engine_invalidate_key_state(key, 1U);
        hall_buttons[key].prev_raw = raw;
        return;
    }

    hall_update_triggers(key);

    if (hall_range_is_valid(hall_min[key], hall_max[key]) == 0U)
    {
        hall_engine_invalidate_key_state(key, 1U);
        hall_buttons[key].prev_raw = raw;
        return;
    }

    range = (uint16_t)(hall_max[key] - hall_min[key]);
    delta = (raw > hall_min[key]) ? (uint32_t)(raw - hall_min[key]) : 0U;
    limited_delta = (delta > range) ? range : delta;
    hall_position[key] = (uint16_t)((limited_delta * 100U) / range);
    if (hall_position[key] > 100U)
    {
        hall_position[key] = 100U;
    }
    hall_value[key] = hall_position[key];

    hall_buttons[key].prev_out = hall_buttons[key].curr_out;

    if (raw > hall_buttons[key].prev_raw)
    {
        dv = (uint16_t)(raw - hall_buttons[key].prev_raw);
    }
    hall_buttons[key].prev_raw = raw;

    if ((hall_buttons[key].curr_out == 0U) && (raw <= hall_buttons[key].trig_lo))
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

        if ((hall_buttons[key].time_active == 0U) && (raw >= hall_buttons[key].vel_start_th))
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

            if (raw >= hall_buttons[key].vel_end_th)
            {
                hall_buttons[key].time_active = 0U;
            }
        }
    }

    if ((hall_buttons[key].curr_out == 0U) && (raw >= hall_buttons[key].trig_hi))
    {
        hall_buttons[key].curr_out = 1U;
        hall_velocity[key] = hall_velocity_compute(key, range);
        hall_velocity_valid[key] = 1U;
    }
    else if ((hall_buttons[key].curr_out != 0U) && (raw <= hall_buttons[key].trig_lo))
    {
        hall_buttons[key].curr_out = 0U;
    }

    hall_pressed[key] = hall_buttons[key].curr_out;

    if ((hall_buttons[key].prev_out == 0U) && (hall_buttons[key].curr_out == 1U))
    {
        hall_note_on_pending[key] = 1U;
    }
    else if ((hall_buttons[key].prev_out != 0U) && (hall_buttons[key].curr_out == 0U))
    {
        hall_note_off_pending[key] = 1U;
        hall_clear_velocity_state(key);
        hall_buttons[key].dv_peak = 0U;
        hall_buttons[key].sum_dv = 0U;
        hall_buttons[key].time_count = 0U;
        hall_buttons[key].time_active = 0U;
    }
}

void hall_engine_process(void)
{
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

void hall_set_velocity_mode(uint8_t mode)
{
    if (mode < (uint8_t)HALL_VEL_MODE_COUNT)
    {
        const uint32_t primask = hall_enter_critical();
        g_velocity_mode = (hall_velocity_mode_t)mode;
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

uint8_t hall_get_velocity_curve(void)
{
    return (uint8_t)g_velocity_curve;
}

void hall_engine_get_velocity_debug(uint8_t key, hall_velocity_debug_t *debug)
{
    const uint32_t primask = hall_enter_critical();

    if ((key >= HALL_KEY_COUNT) || (debug == 0))
    {
        hall_exit_critical(primask);
        return;
    }

    debug->raw_current = hall_raw_current[key];
    debug->min_current = hall_min[key];
    debug->max_current = hall_max[key];
    debug->range_current = (hall_max[key] > hall_min[key]) ?
                           (uint16_t)(hall_max[key] - hall_min[key]) : 0U;
    debug->position_percent = hall_position[key];
    debug->trig_lo = hall_trig_lo[key];
    debug->trig_hi = hall_trig_hi[key];
    debug->prev_raw = hall_buttons[key].prev_raw;
    debug->dv_peak = hall_buttons[key].dv_peak;
    debug->sum_dv = hall_buttons[key].sum_dv;
    debug->vel_start_th = hall_buttons[key].vel_start_th;
    debug->vel_end_th = hall_buttons[key].vel_end_th;
    debug->time_count = hall_buttons[key].time_count;
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
    debug->note_on_pending = hall_note_on_pending[key];
    debug->note_off_pending = hall_note_off_pending[key];

    hall_exit_critical(primask);
}
