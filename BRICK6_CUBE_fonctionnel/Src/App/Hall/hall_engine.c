#include "App/Hall/hall_engine.h"
#include "stm32h7xx_hal.h"

/*
===============================================================================
Port logique Hall ChibiOS -> CubeIDE
===============================================================================

- Acquisition conservée côté CubeIDE: timer -> ADC -> DMA -> callback Hall.
- Le callback ADC Hall reste le point d'entrée critique sample par sample.
- La logique métier portée ici reprend la philosophie du driver ChibiOS:
  * runtime par capteur,
  * min/max dynamiques,
  * seuils RAW dynamiques avec Schmitt,
  * vélocité calculée pendant l'attaque,
  * flags NOTE ON / NOTE OFF consommables hors IRQ.
*/

#define HALL_KEY_SAMPLE_PERIOD_US   800U

#define HALL_THRESHOLD_PPM          200U
#define HALL_HYST_PPM                40U
#define HALL_MIN_RANGE              500U

#define HALL_VALUE_MAX              127U

#define HALL_VEL_TIME_START_PPM     150U
#define HALL_VEL_TIME_END_PPM         0U
#define HALL_VEL_TIME_FAST_DT         2U
#define HALL_VEL_TIME_SLOW_DT        14U

#define HALL_VEL_ENERGY_SLOW_SHIFT    6U
#define HALL_VEL_ENERGY_FAST_SHIFT    2U
#define HALL_VEL_SLOW_SHIFT          12U
#define HALL_VEL_FAST_SHIFT           2U

typedef enum
{
    HALL_VEL_MODE_DV_PEAK = 0,
    HALL_VEL_MODE_TIME,
    HALL_VEL_MODE_ENERGY
} hall_velocity_mode_t;

typedef enum
{
    HALL_VEL_CURVE_LINEAR = 0,
    HALL_VEL_CURVE_SOFT,
    HALL_VEL_CURVE_HARD,
    HALL_VEL_CURVE_LOG,
    HALL_VEL_CURVE_EXP
} hall_velocity_curve_t;

typedef struct
{
    uint16_t min;
    uint16_t max;
    uint16_t trig_lo;
    uint16_t trig_hi;
    uint16_t vel_start_th;
    uint16_t vel_end_th;
    uint16_t prev_raw;
    uint16_t dv_peak;
    uint16_t sum_dv;
    uint16_t time_count;
    uint8_t prev_out;
    uint8_t curr_out;
    uint8_t time_active;
    uint8_t vel_latched;
    uint8_t range_valid;
} hall_button_t;

static volatile uint16_t hall_value[HALL_KEY_COUNT];
static volatile uint16_t hall_position[HALL_KEY_COUNT];
static volatile uint16_t hall_raw_current[HALL_KEY_COUNT];
static volatile uint32_t hall_sample_count_current[HALL_KEY_COUNT];
static volatile uint8_t hall_pressed[HALL_KEY_COUNT];
static volatile uint8_t hall_velocity[HALL_KEY_COUNT];
static volatile uint8_t hall_velocity_valid[HALL_KEY_COUNT];
static volatile uint8_t hall_note_on_pending[HALL_KEY_COUNT];
static volatile uint8_t hall_note_off_pending[HALL_KEY_COUNT];

static volatile hall_button_t hall_button[HALL_KEY_COUNT];

static volatile hall_velocity_mode_t g_velocity_mode = HALL_VEL_MODE_TIME;
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

static uint16_t hall_isqrt_u32(uint32_t x)
{
    uint32_t op = x;
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
            res += (one << 1);
        }

        res >>= 1;
        one >>= 2;
    }

    return (uint16_t)res;
}

static uint8_t hall_apply_curve(uint8_t velocity, hall_velocity_curve_t curve)
{
    uint32_t out = velocity;

    if (out < 1U)
    {
        out = 1U;
    }
    if (out > HALL_VALUE_MAX)
    {
        out = HALL_VALUE_MAX;
    }

    switch (curve)
    {
        case HALL_VEL_CURVE_SOFT:
            out = (out * out) / HALL_VALUE_MAX;
        break;

        case HALL_VEL_CURVE_HARD:
            out = hall_isqrt_u32(out * HALL_VALUE_MAX);
        break;

        case HALL_VEL_CURVE_LOG:
        {
            const uint32_t delta = HALL_VALUE_MAX - out;
            out = HALL_VALUE_MAX - ((delta * delta) / HALL_VALUE_MAX);
        }
        break;

        case HALL_VEL_CURVE_EXP:
            out = (out * out * out) / (HALL_VALUE_MAX * HALL_VALUE_MAX);
        break;

        case HALL_VEL_CURVE_LINEAR:
        default:
        break;
    }

    if (out < 1U)
    {
        out = 1U;
    }
    if (out > HALL_VALUE_MAX)
    {
        out = HALL_VALUE_MAX;
    }

    return (uint8_t)out;
}

static uint8_t hall_velocity_from_dv(uint16_t range, uint16_t dv_peak)
{
    uint16_t dv_slow;
    uint16_t dv_fast;
    uint32_t velocity;

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
        return HALL_VALUE_MAX;
    }

    velocity = 1U + (((uint32_t)(dv_peak - dv_slow) * 126U) / (uint32_t)(dv_fast - dv_slow));
    return (velocity > HALL_VALUE_MAX) ? HALL_VALUE_MAX : (uint8_t)velocity;
}

static uint8_t hall_velocity_from_time(uint16_t dt_count)
{
    uint32_t velocity;

    if (dt_count <= HALL_VEL_TIME_FAST_DT)
    {
        return HALL_VALUE_MAX;
    }
    if (dt_count >= HALL_VEL_TIME_SLOW_DT)
    {
        return 1U;
    }

    velocity = 1U + (((uint32_t)(HALL_VEL_TIME_SLOW_DT - dt_count) * 126U) /
                     (uint32_t)(HALL_VEL_TIME_SLOW_DT - HALL_VEL_TIME_FAST_DT));
    return (velocity > HALL_VALUE_MAX) ? HALL_VALUE_MAX : (uint8_t)velocity;
}

static uint8_t hall_velocity_from_energy(uint16_t range, uint16_t sum_dv)
{
    uint16_t energy_slow;
    uint16_t energy_fast;
    uint32_t velocity;

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
        return HALL_VALUE_MAX;
    }

    velocity = 1U + (((uint32_t)(sum_dv - energy_slow) * 126U) /
                     (uint32_t)(energy_fast - energy_slow));
    return (velocity > HALL_VALUE_MAX) ? HALL_VALUE_MAX : (uint8_t)velocity;
}

static uint8_t hall_velocity_compute(const volatile hall_button_t *button,
                                     uint16_t range)
{
    uint8_t raw_velocity;

    switch (g_velocity_mode)
    {
        case HALL_VEL_MODE_DV_PEAK:
            raw_velocity = hall_velocity_from_dv(range, button->dv_peak);
        break;

        case HALL_VEL_MODE_ENERGY:
            raw_velocity = hall_velocity_from_energy(range, button->sum_dv);
        break;

        case HALL_VEL_MODE_TIME:
        default:
            raw_velocity = hall_velocity_from_time(button->time_count);
        break;
    }

    return hall_apply_curve(raw_velocity, g_velocity_curve);
}

static void hall_button_reset_runtime(volatile hall_button_t *button)
{
    if (button == 0)
    {
        return;
    }

    button->prev_raw = 0U;
    button->dv_peak = 0U;
    button->sum_dv = 0U;
    button->time_count = 0U;
    button->prev_out = 0U;
    button->curr_out = 0U;
    button->time_active = 0U;
    button->vel_latched = 0U;
    button->range_valid = 0U;
}

static void hall_key_reset_outputs(uint8_t key)
{
    hall_value[key] = 0U;
    hall_position[key] = 0U;
    hall_pressed[key] = 0U;
    hall_velocity[key] = 0U;
    hall_velocity_valid[key] = 0U;
    hall_note_on_pending[key] = 0U;
    hall_note_off_pending[key] = 0U;
}

static uint8_t hall_button_range_valid(const volatile hall_button_t *button)
{
    if (button->max <= button->min)
    {
        return 0U;
    }

    return (((uint16_t)(button->max - button->min)) >= HALL_MIN_RANGE) ? 1U : 0U;
}

static void hall_button_update_thresholds(volatile hall_button_t *button)
{
    uint32_t range;
    uint32_t half_hyst;
    uint32_t lo_ppm;
    uint32_t hi_ppm;

    if (hall_button_range_valid(button) == 0U)
    {
        button->trig_lo = button->min;
        button->trig_hi = button->max;
        button->vel_start_th = button->min;
        button->vel_end_th = button->max;
        button->range_valid = 0U;
        return;
    }

    range = (uint32_t)(button->max - button->min);
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

    button->trig_lo = (uint16_t)(button->min + ((range * lo_ppm) / 1000U));
    button->trig_hi = (uint16_t)(button->min + ((range * hi_ppm) / 1000U));
    button->vel_start_th = (uint16_t)(button->min + ((range * HALL_VEL_TIME_START_PPM) / 1000U));
    button->vel_end_th = (HALL_VEL_TIME_END_PPM == 0U)
                       ? button->trig_hi
                       : (uint16_t)(button->min + ((range * HALL_VEL_TIME_END_PPM) / 1000U));
    button->range_valid = 1U;
}

void hall_engine_init(void)
{
    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_button[i].min = 0xFFFFU;
        hall_button[i].max = 0U;
        hall_button[i].trig_lo = 0U;
        hall_button[i].trig_hi = 0U;
        hall_button[i].vel_start_th = 0U;
        hall_button[i].vel_end_th = 0U;

        hall_raw_current[i] = 0U;
        hall_sample_count_current[i] = 0U;
        hall_button_reset_runtime(&hall_button[i]);
        hall_key_reset_outputs(i);
    }
}

void hall_engine_set_calibration(const uint16_t *min_values,
                                 const uint16_t *max_values)
{
    if ((min_values == 0) || (max_values == 0))
    {
        return;
    }

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_button[i].min = min_values[i];
        hall_button[i].max = max_values[i];
        hall_button_update_thresholds(&hall_button[i]);
        hall_button_reset_runtime(&hall_button[i]);
        hall_key_reset_outputs(i);
    }
}

void hall_engine_process_sample(uint8_t key, uint16_t raw, uint32_t sample_count)
{
    volatile hall_button_t *button;
    uint16_t range;
    uint32_t delta;
    uint32_t limited_delta;
    uint16_t position_percent;
    uint16_t value;
    uint16_t dv;

    if (key >= HALL_KEY_COUNT)
    {
        return;
    }

    button = &hall_button[key];
    hall_raw_current[key] = raw;
    hall_sample_count_current[key] = sample_count;

    if (raw < button->min)
    {
        button->min = raw;
    }
    if (raw > button->max)
    {
        button->max = raw;
    }

    hall_button_update_thresholds(button);

    if (button->range_valid == 0U)
    {
        hall_button_reset_runtime(button);
        button->prev_raw = raw;
        hall_key_reset_outputs(key);
        return;
    }

    range = (uint16_t)(button->max - button->min);
    delta = (raw > button->min) ? (uint32_t)(raw - button->min) : 0U;
    limited_delta = (delta > range) ? range : delta;
    position_percent = (uint16_t)((limited_delta * 100U) / range);
    value = (uint16_t)((limited_delta * HALL_VALUE_MAX) / range);

    hall_position[key] = position_percent;
    hall_value[key] = value;

    button->prev_out = button->curr_out;

    dv = 0U;
    if (raw > button->prev_raw)
    {
        dv = (uint16_t)(raw - button->prev_raw);
    }
    button->prev_raw = raw;

    if ((button->curr_out == 0U) && (raw <= button->trig_lo))
    {
        button->dv_peak = 0U;
        button->sum_dv = 0U;
        button->time_count = 0U;
        button->time_active = 0U;
    }

    if (button->curr_out == 0U)
    {
        uint32_t sum_dv;

        if (dv > button->dv_peak)
        {
            button->dv_peak = dv;
        }

        sum_dv = (uint32_t)button->sum_dv + dv;
        if (sum_dv > 0xFFFFU)
        {
            sum_dv = 0xFFFFU;
        }
        button->sum_dv = (uint16_t)sum_dv;

        if ((button->time_active == 0U) && (raw >= button->vel_start_th))
        {
            button->time_active = 1U;
            button->time_count = 0U;
        }

        if (button->time_active != 0U)
        {
            if (button->time_count < 0xFFFFU)
            {
                button->time_count++;
            }

            if (raw >= button->vel_end_th)
            {
                button->time_active = 0U;
            }
        }
    }

    if ((button->curr_out == 0U) && (raw >= button->trig_hi))
    {
        button->curr_out = 1U;
        button->vel_latched = hall_velocity_compute(button, range);
        hall_velocity[key] = button->vel_latched;
        hall_velocity_valid[key] = 1U;
        hall_pressed[key] = 1U;
    }
    else if ((button->curr_out != 0U) && (raw <= button->trig_lo))
    {
        button->curr_out = 0U;
        hall_pressed[key] = 0U;
        hall_velocity_valid[key] = 0U;
    }

    if ((button->prev_out == 0U) && (button->curr_out == 1U))
    {
        hall_note_on_pending[key] = 1U;
    }
    else if ((button->prev_out == 1U) && (button->curr_out == 0U))
    {
        hall_note_off_pending[key] = 1U;
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

    return hall_button[key].min;
}

uint16_t hall_engine_get_max(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_button[key].max;
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

void hall_engine_get_velocity_debug(uint8_t key, hall_velocity_debug_t *debug)
{
    const uint32_t primask = hall_enter_critical();
    const volatile hall_button_t *button = 0;

    if ((key >= HALL_KEY_COUNT) || (debug == 0))
    {
        hall_exit_critical(primask);
        return;
    }

    button = &hall_button[key];

    debug->raw_current = hall_raw_current[key];
    debug->min_current = button->min;
    debug->max_current = button->max;
    debug->position_percent = hall_position[key];
    debug->velocity1_arm_threshold = button->vel_start_th;
    debug->trigger1_threshold = button->vel_end_th;
    debug->velocity2_arm_threshold = button->trig_lo;
    debug->trigger2_threshold = button->trig_hi;
    debug->velocity1_raw_latched = button->dv_peak;
    debug->velocity2_raw_latched = button->sum_dv;
    debug->velocity1_elapsed_samples = button->time_count;
    debug->velocity2_elapsed_samples = hall_sample_count_current[key];
    debug->sample_count = hall_sample_count_current[key];
    debug->sample_period_us = HALL_KEY_SAMPLE_PERIOD_US;
    debug->velocity_latched = button->vel_latched;
    debug->velocity2_latched = hall_velocity[key];
    debug->velocity_ready = hall_velocity_valid[key];
    debug->velocity2_ready = button->range_valid;
    debug->velocity1_armed = button->time_active;
    debug->velocity2_armed = button->curr_out;
    debug->velocity1_fallback = 0U;
    debug->velocity2_fallback = 0U;
    debug->note_on_pending = hall_note_on_pending[key];
    debug->note_off_pending = hall_note_off_pending[key];
    debug->state = button->curr_out;

    hall_exit_critical(primask);
}
