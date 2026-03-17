#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_adc.h"

#include <limits.h>
#include <stdint.h>
#include <stdbool.h>

/* -------------------- Paramètres -------------------- */

#define HALL_SENSOR_COUNT    HALL_KEY_COUNT

#define HALL_THRESHOLD_PPM_DEFAULT   200U
#define HALL_HYST_PPM_DEFAULT         40U
#define HALL_MIN_RANGE_DEFAULT       500U

/* -------------------- Vélocité -------------------- */

static volatile hall_vel_mode_t  g_vel_mode  = HALL_VEL_MODE_TIME;
static volatile hall_vel_curve_t g_vel_curve = HALL_VEL_CURVE_SOFT;

#define HALL_VEL_SLOW_SHIFT          12U
#define HALL_VEL_FAST_SHIFT           2U

#define HALL_VEL_TIME_START_PPM_DEFAULT     150U
#define HALL_VEL_TIME_END_PPM              0U
#define HALL_VEL_TIME_FAST_DT_DEFAULT       2U
#define HALL_VEL_TIME_SLOW_DT_DEFAULT      14U

static volatile uint16_t g_threshold_ppm = HALL_THRESHOLD_PPM_DEFAULT;
static volatile uint16_t g_hyst_ppm = HALL_HYST_PPM_DEFAULT;
static volatile uint16_t g_min_range = HALL_MIN_RANGE_DEFAULT;

static volatile uint16_t g_time_start_ppm = HALL_VEL_TIME_START_PPM_DEFAULT;
static volatile uint16_t g_time_fast_dt = HALL_VEL_TIME_FAST_DT_DEFAULT;
static volatile uint16_t g_time_slow_dt = HALL_VEL_TIME_SLOW_DT_DEFAULT;

#define HALL_VEL_ENERGY_SLOW_SHIFT   6U
#define HALL_VEL_ENERGY_FAST_SHIFT   2U

/* -------------------- État capteurs -------------------- */

static uint16_t hall_values[HALL_SENSOR_COUNT];
static uint8_t hall_note_on[HALL_SENSOR_COUNT];
static uint8_t hall_note_off[HALL_SENSOR_COUNT];

static uint8_t hall_velocity[HALL_SENSOR_COUNT];
static uint8_t hall_pressure[HALL_SENSOR_COUNT];
static uint8_t hall_midi_value[HALL_SENSOR_COUNT];

static uint8_t hall_calibrated = 0U;

/* -------------------- Button state -------------------- */

typedef struct
{
    uint16_t min;
    uint16_t max;
    uint16_t trig_lo;
    uint16_t trig_hi;

    uint8_t prev_out;
    uint8_t curr_out;

    uint16_t prev_raw;
    uint16_t dv_peak;
    uint16_t sum_dv;

    uint16_t vel_start_th;
    uint16_t vel_end_th;
    uint16_t time_count;
    uint8_t  time_active;
    uint8_t  vel_latched;

} hall_button_t;

static hall_button_t hall_btn[HALL_SENSOR_COUNT];

/* -------------------- Helpers -------------------- */

static bool hall_range_valid(const hall_button_t *b)
{
    if (b->max <= b->min)
    {
        return false;
    }

    return ((uint16_t)(b->max - b->min) >= (uint16_t)g_min_range);
}

static void hall_update_triggers(hall_button_t *b)
{
    if (!hall_range_valid(b))
    {
        b->trig_lo = b->min;
        b->trig_hi = b->max;
        b->vel_start_th = b->min;
        b->vel_end_th = b->max;
        return;
    }

    uint32_t range = (uint32_t)(b->max - b->min);

    uint32_t half_hyst = (uint32_t)g_hyst_ppm / 2U;
    uint32_t lo_ppm = (uint32_t)g_threshold_ppm;
    uint32_t hi_ppm = (uint32_t)g_threshold_ppm;

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

    b->trig_lo = (uint16_t)(b->min + (range * lo_ppm) / 1000U);
    b->trig_hi = (uint16_t)(b->min + (range * hi_ppm) / 1000U);

    b->vel_start_th = (uint16_t)(b->min + (range * (uint32_t)g_time_start_ppm) / 1000U);

    if (HALL_VEL_TIME_END_PPM == 0U)
    {
        b->vel_end_th = b->trig_hi;
    }
    else
    {
        b->vel_end_th = (uint16_t)(b->min + (range * (uint32_t)HALL_VEL_TIME_END_PPM) / 1000U);
    }
}

static uint16_t isqrt_u32(uint32_t x)
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
        if (op >= res + one)
        {
            op -= res + one;
            res = res + 2U * one;
        }

        res >>= 1;
        one >>= 2;
    }

    return (uint16_t)res;
}

static uint8_t hall_apply_curve(uint8_t v, hall_vel_curve_t curve)
{
    if (v < 1U) v = 1U;
    if (v > 127U) v = 127U;

    switch (curve)
    {
        case HALL_VEL_CURVE_LINEAR:
            return v;

        case HALL_VEL_CURVE_SOFT:
        {
            uint32_t vv = (uint32_t)v * (uint32_t)v;
            uint32_t out = vv / 127U;
            if (out < 1U) out = 1U;
            if (out > 127U) out = 127U;
            return (uint8_t)out;
        }

        case HALL_VEL_CURVE_HARD:
        {
            uint32_t x = (uint32_t)v * 127U;
            uint16_t out = isqrt_u32(x);
            if (out < 1U) out = 1U;
            if (out > 127U) out = 127U;
            return (uint8_t)out;
        }

        case HALL_VEL_CURVE_LOG:
        {
            uint32_t d = (uint32_t)(127U - v);
            uint32_t dd = d * d;
            uint32_t out = 127U - (dd / 127U);
            if (out < 1U) out = 1U;
            if (out > 127U) out = 127U;
            return (uint8_t)out;
        }

        case HALL_VEL_CURVE_EXP:
        {
            uint32_t vv = (uint32_t)v * (uint32_t)v * (uint32_t)v;
            uint32_t out = vv / (127U * 127U);
            if (out < 1U) out = 1U;
            if (out > 127U) out = 127U;
            return (uint8_t)out;
        }

        default:
            return v;
    }
}

static uint8_t hall_velocity_from_dv(uint16_t range, uint16_t dv_peak)
{
    if (range == 0U) return 1U;

    uint16_t dv_slow = (uint16_t)(range >> HALL_VEL_SLOW_SHIFT);
    uint16_t dv_fast = (uint16_t)(range >> HALL_VEL_FAST_SHIFT);

    if (dv_slow < 1U) dv_slow = 1U;
    if (dv_fast <= (uint16_t)(dv_slow + 1U)) dv_fast = (uint16_t)(dv_slow + 2U);

    if (dv_peak <= dv_slow) return 1U;
    if (dv_peak >= dv_fast) return 127U;

    uint32_t num = (uint32_t)(dv_peak - dv_slow) * 126U;
    uint32_t den = (uint32_t)(dv_fast - dv_slow);
    uint32_t v = 1U + (num / den);

    if (v < 1U) v = 1U;
    if (v > 127U) v = 127U;
    return (uint8_t)v;
}

static uint8_t hall_velocity_from_time(uint16_t dt_count)
{
    if (dt_count <= g_time_fast_dt) return 127U;
    if (dt_count >= g_time_slow_dt) return 1U;

    uint32_t num = (uint32_t)(g_time_slow_dt - dt_count) * 126U;
    uint32_t den = (uint32_t)(g_time_slow_dt - g_time_fast_dt);
    uint32_t v = 1U + (num / den);

    if (v < 1U) v = 1U;
    if (v > 127U) v = 127U;
    return (uint8_t)v;
}

static uint8_t hall_velocity_from_energy(uint16_t range, uint16_t sum_dv)
{
    if (range == 0U) return 1U;

    uint16_t e_slow = (uint16_t)(range >> HALL_VEL_ENERGY_SLOW_SHIFT);
    uint16_t e_fast = (uint16_t)(range >> HALL_VEL_ENERGY_FAST_SHIFT);

    if (e_slow < 1U) e_slow = 1U;
    if (e_fast <= (uint16_t)(e_slow + 1U)) e_fast = (uint16_t)(e_slow + 2U);

    if (sum_dv <= e_slow) return 1U;
    if (sum_dv >= e_fast) return 127U;

    uint32_t num = (uint32_t)(sum_dv - e_slow) * 126U;
    uint32_t den = (uint32_t)(e_fast - e_slow);
    uint32_t v = 1U + (num / den);

    if (v < 1U) v = 1U;
    if (v > 127U) v = 127U;
    return (uint8_t)v;
}

static uint8_t hall_velocity_compute(hall_button_t *b, uint16_t range)
{
    uint8_t vel_raw = 1U;

    hall_vel_mode_t mode = (hall_vel_mode_t)g_vel_mode;

    switch (mode)
    {
        case HALL_VEL_MODE_TIME:
            vel_raw = hall_velocity_from_time(b->time_count);
            break;

        case HALL_VEL_MODE_ENERGY:
            vel_raw = hall_velocity_from_energy(range, b->sum_dv);
            break;

        case HALL_VEL_MODE_DV_PEAK:
        default:
            vel_raw = hall_velocity_from_dv(range, b->dv_peak);
            break;
    }

    return hall_apply_curve(vel_raw, (hall_vel_curve_t)g_vel_curve);
}

static void hall_process_channel(uint8_t index, uint16_t raw)
{
    hall_button_t *b = &hall_btn[index];

    if (!hall_calibrated)
    {
        if (raw < b->min) b->min = raw;
        if (raw > b->max) b->max = raw;
    }

    hall_values[index] = raw;

    hall_update_triggers(b);

    b->prev_out = b->curr_out;

    if (!hall_range_valid(b))
    {
        b->curr_out = 0U;
        b->prev_raw = raw;
        b->dv_peak = 0U;
        b->sum_dv = 0U;
        b->time_count = 0U;
        b->time_active = 0U;
        b->vel_latched = 0U;
        hall_velocity[index] = 0U;
        return;
    }

    uint16_t dv = 0U;
    if (raw > b->prev_raw)
    {
        dv = (uint16_t)(raw - b->prev_raw);
    }
    b->prev_raw = raw;

    if ((b->curr_out == 0U) && (raw <= b->trig_lo))
    {
        b->dv_peak = 0U;
        b->sum_dv = 0U;
        b->time_count = 0U;
        b->time_active = 0U;
    }

    if (b->curr_out == 0U)
    {
        if (dv > b->dv_peak) b->dv_peak = dv;

        uint32_t s = (uint32_t)b->sum_dv + (uint32_t)dv;
        if (s > 65535U) s = 65535U;
        b->sum_dv = (uint16_t)s;

        if ((!b->time_active) && (raw >= b->vel_start_th))
        {
            b->time_active = 1U;
            b->time_count = 0U;
        }

        if (b->time_active)
        {
            if (b->time_count < 65535U)
            {
                b->time_count++;
            }

            if (raw >= b->vel_end_th)
            {
                b->time_active = 0U;
            }
        }
    }

    if ((b->curr_out == 0U) && (raw >= b->trig_hi))
    {
        b->curr_out = 1U;

        uint16_t range = (uint16_t)(b->max - b->min);
        b->vel_latched = hall_velocity_compute(b, range);
        hall_velocity[index] = b->vel_latched;
    }
    else if ((b->curr_out != 0U) && (raw <= b->trig_lo))
    {
        b->curr_out = 0U;
    }

    if ((b->prev_out == 0U) && (b->curr_out == 1U))
    {
        hall_note_on[index] = 1U;
    }
    else if ((b->prev_out == 1U) && (b->curr_out == 0U))
    {
        hall_note_off[index] = 1U;
    }
}

void hall_engine_init(void)
{
    hall_calibrated = 0U;

    g_threshold_ppm = HALL_THRESHOLD_PPM_DEFAULT;
    g_hyst_ppm = HALL_HYST_PPM_DEFAULT;
    g_min_range = HALL_MIN_RANGE_DEFAULT;

    g_time_start_ppm = HALL_VEL_TIME_START_PPM_DEFAULT;
    g_time_fast_dt = HALL_VEL_TIME_FAST_DT_DEFAULT;
    g_time_slow_dt = HALL_VEL_TIME_SLOW_DT_DEFAULT;

    for (uint8_t i = 0U; i < HALL_SENSOR_COUNT; i++)
    {
        hall_values[i] = 0U;
        hall_note_on[i] = 0U;
        hall_note_off[i] = 0U;

        hall_velocity[i] = 0U;
        hall_pressure[i] = 0U;
        hall_midi_value[i] = 0U;

        hall_btn[i].min = UINT16_MAX;
        hall_btn[i].max = 0U;
        hall_btn[i].trig_lo = UINT16_MAX;
        hall_btn[i].trig_hi = 0U;

        hall_btn[i].prev_out = 0U;
        hall_btn[i].curr_out = 0U;

        hall_btn[i].prev_raw = 0U;
        hall_btn[i].dv_peak = 0U;
        hall_btn[i].sum_dv = 0U;

        hall_btn[i].vel_start_th = 0U;
        hall_btn[i].vel_end_th = 0U;
        hall_btn[i].time_count = 0U;
        hall_btn[i].time_active = 0U;
        hall_btn[i].vel_latched = 0U;
    }
}

void hall_engine_set_calibration(const uint16_t *min_values,
                                 const uint16_t *max_values)
{
    if ((min_values == 0) || (max_values == 0))
    {
        return;
    }

    hall_calibrated = 1U;

    for (uint8_t i = 0U; i < HALL_SENSOR_COUNT; i++)
    {
        hall_btn[i].min = min_values[i];
        hall_btn[i].max = max_values[i];
        hall_update_triggers(&hall_btn[i]);

        hall_btn[i].prev_out = 0U;
        hall_btn[i].curr_out = 0U;

        hall_btn[i].prev_raw = 0U;
        hall_btn[i].dv_peak = 0U;
        hall_btn[i].sum_dv = 0U;

        hall_btn[i].time_count = 0U;
        hall_btn[i].time_active = 0U;
        hall_btn[i].vel_latched = 0U;

        hall_note_on[i] = 0U;
        hall_note_off[i] = 0U;
        hall_velocity[i] = 0U;
        hall_pressure[i] = 0U;
        hall_midi_value[i] = 0U;
    }
}

void hall_engine_process(void)
{
    for (uint8_t i = 0U; i < HALL_SENSOR_COUNT; i++)
    {
        hall_process_channel(i, hall_adc_get_raw(i));
    }
}

uint16_t hall_engine_get_value(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_values[key];
}

uint8_t hall_engine_is_pressed(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_btn[key].curr_out;
}

bool hall_engine_get_note_on(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return false;

    bool v = (hall_note_on[key] != 0U);
    hall_note_on[key] = 0U;
    return v;
}

bool hall_engine_get_note_off(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return false;

    bool v = (hall_note_off[key] != 0U);
    hall_note_off[key] = 0U;
    return v;
}

uint8_t hall_engine_get_velocity(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_velocity[key];
}

uint8_t hall_engine_get_pressure(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_pressure[key];
}

uint8_t hall_engine_get_midi_value(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_midi_value[key];
}

void hall_engine_set_velocity_mode(uint8_t mode)
{
    if (mode < (uint8_t)HALL_VEL_MODE_COUNT)
    {
        g_vel_mode = (hall_vel_mode_t)mode;
    }
}

void hall_engine_set_velocity_curve(uint8_t curve)
{
    if (curve < (uint8_t)HALL_VEL_CURVE_COUNT)
    {
        g_vel_curve = (hall_vel_curve_t)curve;
    }
}

uint8_t hall_engine_get_velocity_mode(void)
{
    return (uint8_t)g_vel_mode;
}

uint8_t hall_engine_get_velocity_curve(void)
{
    return (uint8_t)g_vel_curve;
}

uint16_t hall_engine_get_min(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_btn[key].min;
}

uint16_t hall_engine_get_max(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_btn[key].max;
}

uint16_t hall_engine_get_trig_lo(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_btn[key].trig_lo;
}

uint16_t hall_engine_get_trig_hi(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_btn[key].trig_hi;
}

uint8_t hall_engine_get_state(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_btn[key].curr_out;
}

uint8_t hall_engine_get_velocity_latched(uint8_t key)
{
    if (key >= HALL_SENSOR_COUNT) return 0U;
    return hall_btn[key].vel_latched;
}


void hall_engine_set_threshold_ppm(uint16_t v)
{
    if (v > 1000U)
    {
        v = 1000U;
    }

    g_threshold_ppm = v;
}

uint16_t hall_engine_get_threshold_ppm(void)
{
    return g_threshold_ppm;
}

void hall_engine_set_hyst_ppm(uint16_t v)
{
    if (v > 1000U)
    {
        v = 1000U;
    }

    g_hyst_ppm = v;
}

uint16_t hall_engine_get_hyst_ppm(void)
{
    return g_hyst_ppm;
}

void hall_engine_set_min_range(uint16_t v)
{
    g_min_range = v;
}

uint16_t hall_engine_get_min_range(void)
{
    return g_min_range;
}

void hall_engine_set_time_start_ppm(uint16_t v)
{
    if (v > 1000U)
    {
        v = 1000U;
    }

    g_time_start_ppm = v;
}

uint16_t hall_engine_get_time_start_ppm(void)
{
    return g_time_start_ppm;
}

void hall_engine_set_time_fast_dt(uint16_t v)
{
    if (v < 1U)
    {
        v = 1U;
    }

    g_time_fast_dt = v;

    if (g_time_fast_dt >= g_time_slow_dt)
    {
        g_time_slow_dt = (uint16_t)(g_time_fast_dt + 1U);
    }
}

uint16_t hall_engine_get_time_fast_dt(void)
{
    return g_time_fast_dt;
}

void hall_engine_set_time_slow_dt(uint16_t v)
{
    if (v < 2U)
    {
        v = 2U;
    }

    g_time_slow_dt = v;

    if (g_time_slow_dt <= g_time_fast_dt)
    {
        g_time_fast_dt = (uint16_t)(g_time_slow_dt - 1U);
    }
}

uint16_t hall_engine_get_time_slow_dt(void)
{
    return g_time_slow_dt;
}
