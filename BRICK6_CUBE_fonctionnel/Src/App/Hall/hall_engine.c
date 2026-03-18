#include "App/Hall/hall_engine.h"

#include <limits.h>
#include <stddef.h>

#include "App/Hall/hall_adc.h"

#define HALL_ENGINE_FILTER_SHIFT               2U
#define HALL_ENGINE_MIN_VALID_RANGE            512U
#define HALL_ENGINE_PRESS_RATIO_Q8             208U
#define HALL_ENGINE_RELEASE_RATIO_Q8           160U
#define HALL_ENGINE_VELOCITY_START_RATIO_Q8    112U
#define HALL_ENGINE_MIN_TIME_VELOCITY          20U
#define HALL_ENGINE_FAST_ATTACK_SAMPLES        2U
#define HALL_ENGINE_SLOW_ATTACK_SAMPLES        18U

typedef struct
{
    uint16_t raw;
    uint16_t filtered;
    uint16_t prev_filtered;

    uint16_t cal_min;
    uint16_t cal_max;
    uint16_t observed_min;
    uint16_t observed_max;

    uint16_t trig_press;
    uint16_t trig_release;
    uint16_t vel_start;

    uint16_t derivative_peak;
    uint16_t last_derivative_peak;
    uint16_t attack_samples;
    uint16_t last_attack_samples;
    uint16_t attack_positive_sum;

    int16_t derivative;

    uint8_t initialized;
    uint8_t calibration_valid;
    uint8_t range_valid;
    uint8_t pressed;
    uint8_t note_on_latched;
    uint8_t note_off_latched;
    uint8_t velocity;
    uint8_t velocity_armed;
} hall_engine_key_t;

static hall_engine_key_t g_hall_keys[HALL_KEY_COUNT];

static uint16_t hall_engine_clamp_u16(uint32_t value)
{
    if (value > 0xFFFFU)
    {
        return 0xFFFFU;
    }

    return (uint16_t)value;
}

static uint16_t hall_engine_range(uint16_t min_value, uint16_t max_value)
{
    if (max_value <= min_value)
    {
        return 0U;
    }

    return (uint16_t)(max_value - min_value);
}

static uint16_t hall_engine_lerp_q8(uint16_t min_value, uint16_t max_value, uint16_t ratio_q8)
{
    const uint16_t range = hall_engine_range(min_value, max_value);
    return (uint16_t)(min_value + (((uint32_t)range * ratio_q8) >> 8));
}

static uint8_t hall_engine_map_u16(uint16_t value, uint16_t in_min, uint16_t in_max,
                                   uint8_t out_min, uint8_t out_max)
{
    if (out_max <= out_min)
    {
        return out_min;
    }

    if (in_max <= in_min)
    {
        return out_min;
    }

    if (value <= in_min)
    {
        return out_min;
    }

    if (value >= in_max)
    {
        return out_max;
    }

    return (uint8_t)(out_min + ((((uint32_t)(value - in_min)) * (uint32_t)(out_max - out_min))
            / (uint32_t)(in_max - in_min)));
}

static uint8_t hall_engine_map_inverse_u16(uint16_t value, uint16_t in_fast, uint16_t in_slow,
                                           uint8_t out_fast, uint8_t out_slow)
{
    if (value <= in_fast)
    {
        return out_fast;
    }

    if (value >= in_slow)
    {
        return out_slow;
    }

    return (uint8_t)(out_fast - ((((uint32_t)(value - in_fast)) * (uint32_t)(out_fast - out_slow))
            / (uint32_t)(in_slow - in_fast)));
}

static uint8_t hall_engine_range_is_valid(uint16_t min_value, uint16_t max_value)
{
    return (hall_engine_range(min_value, max_value) >= HALL_ENGINE_MIN_VALID_RANGE) ? 1U : 0U;
}

static void hall_engine_update_thresholds(hall_engine_key_t *key)
{
    uint16_t min_value = key->observed_min;
    uint16_t max_value = key->observed_max;

    if ((key->calibration_valid != 0U) && (key->cal_min < min_value))
    {
        min_value = key->cal_min;
    }

    if ((key->calibration_valid != 0U) && (key->cal_max > max_value))
    {
        max_value = key->cal_max;
    }

    key->range_valid = hall_engine_range_is_valid(min_value, max_value);

    if (key->range_valid == 0U)
    {
        key->trig_press = max_value;
        key->trig_release = min_value;
        key->vel_start = min_value;
        return;
    }

    key->vel_start = hall_engine_lerp_q8(min_value, max_value, HALL_ENGINE_VELOCITY_START_RATIO_Q8);
    key->trig_release = hall_engine_lerp_q8(min_value, max_value, HALL_ENGINE_RELEASE_RATIO_Q8);
    key->trig_press = hall_engine_lerp_q8(min_value, max_value, HALL_ENGINE_PRESS_RATIO_Q8);

    if (key->trig_release >= key->trig_press)
    {
        key->trig_release = min_value;
    }
}

static uint8_t hall_engine_compute_velocity(const hall_engine_key_t *key)
{
    uint16_t peak_reference;
    uint8_t velocity_from_time;
    uint8_t velocity_from_peak;
    uint8_t velocity;
    uint16_t effective_attack_samples = key->attack_samples;
    uint16_t effective_peak = key->derivative_peak;

    if (effective_attack_samples == 0U)
    {
        effective_attack_samples = 1U;
    }

    if (effective_peak == 0U)
    {
        if (key->derivative > 0)
        {
            effective_peak = (uint16_t)key->derivative;
        }
        else
        {
            effective_peak = 1U;
        }
    }

    velocity_from_time = hall_engine_map_inverse_u16(effective_attack_samples,
                                                     HALL_ENGINE_FAST_ATTACK_SAMPLES,
                                                     HALL_ENGINE_SLOW_ATTACK_SAMPLES,
                                                     127U,
                                                     HALL_ENGINE_MIN_TIME_VELOCITY);

    peak_reference = hall_engine_range(key->observed_min, key->observed_max) / 10U;
    if (peak_reference < 8U)
    {
        peak_reference = 8U;
    }

    velocity_from_peak = hall_engine_map_u16(effective_peak, 1U, peak_reference, 24U, 127U);
    velocity = (uint8_t)(((uint16_t)velocity_from_time * 3U + (uint16_t)velocity_from_peak * 2U) / 5U);

    if (velocity == 0U)
    {
        velocity = 1U;
    }

    return velocity;
}

static void hall_engine_reset_attack(hall_engine_key_t *key)
{
    key->velocity_armed = 0U;
    key->attack_samples = 0U;
    key->attack_positive_sum = 0U;
    key->derivative_peak = 0U;
}

static void hall_engine_process_key(uint8_t index, uint16_t raw)
{
    hall_engine_key_t *key = &g_hall_keys[index];
    uint16_t filtered;
    int32_t delta;

    key->raw = raw;

    if (key->initialized == 0U)
    {
        key->filtered = raw;
        key->prev_filtered = raw;
        key->observed_min = raw;
        key->observed_max = raw;
        key->initialized = 1U;
    }

    filtered = (uint16_t)(((uint32_t)key->filtered * ((1U << HALL_ENGINE_FILTER_SHIFT) - 1U) + raw)
            >> HALL_ENGINE_FILTER_SHIFT);

    key->prev_filtered = key->filtered;
    key->filtered = filtered;

    if (filtered < key->observed_min)
    {
        key->observed_min = filtered;
    }

    if (filtered > key->observed_max)
    {
        key->observed_max = filtered;
    }

    hall_engine_update_thresholds(key);

    delta = (int32_t)key->filtered - (int32_t)key->prev_filtered;
    key->derivative = (int16_t)delta;

    if ((key->range_valid != 0U) && (key->pressed == 0U))
    {
        if ((key->velocity_armed == 0U) && (key->filtered >= key->vel_start))
        {
            key->velocity_armed = 1U;
            key->attack_samples = 0U;
            key->attack_positive_sum = 0U;
            key->derivative_peak = 0U;
        }

        if (key->velocity_armed != 0U)
        {
            if (key->attack_samples < USHRT_MAX)
            {
                key->attack_samples++;
            }

            if (delta > 0)
            {
                const uint16_t positive_delta = hall_engine_clamp_u16((uint32_t)delta);

                if (key->attack_positive_sum <= (uint16_t)(0xFFFFU - positive_delta))
                {
                    key->attack_positive_sum = (uint16_t)(key->attack_positive_sum + positive_delta);
                }
                else
                {
                    key->attack_positive_sum = 0xFFFFU;
                }

                if (positive_delta > key->derivative_peak)
                {
                    key->derivative_peak = positive_delta;
                }
            }

            if (key->filtered < key->vel_start)
            {
                hall_engine_reset_attack(key);
            }
        }
    }

    if ((key->range_valid != 0U) && (key->pressed == 0U) && (key->filtered >= key->trig_press))
    {
        key->last_attack_samples = key->attack_samples;
        key->last_derivative_peak = key->derivative_peak;
        key->pressed = 1U;
        key->note_on_latched = 1U;
        key->velocity = hall_engine_compute_velocity(key);
        hall_engine_reset_attack(key);
        return;
    }

    if ((key->pressed != 0U) && ((key->range_valid == 0U) || (key->filtered <= key->trig_release)))
    {
        key->pressed = 0U;
        key->note_off_latched = 1U;
        hall_engine_reset_attack(key);
    }
}

void hall_engine_init(void)
{
    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        g_hall_keys[i].raw = 0U;
        g_hall_keys[i].filtered = 0U;
        g_hall_keys[i].prev_filtered = 0U;
        g_hall_keys[i].cal_min = 0xFFFFU;
        g_hall_keys[i].cal_max = 0U;
        g_hall_keys[i].observed_min = 0xFFFFU;
        g_hall_keys[i].observed_max = 0U;
        g_hall_keys[i].trig_press = 0U;
        g_hall_keys[i].trig_release = 0U;
        g_hall_keys[i].vel_start = 0U;
        g_hall_keys[i].derivative_peak = 0U;
        g_hall_keys[i].last_derivative_peak = 0U;
        g_hall_keys[i].attack_samples = 0U;
        g_hall_keys[i].last_attack_samples = 0U;
        g_hall_keys[i].attack_positive_sum = 0U;
        g_hall_keys[i].derivative = 0;
        g_hall_keys[i].initialized = 0U;
        g_hall_keys[i].calibration_valid = 0U;
        g_hall_keys[i].range_valid = 0U;
        g_hall_keys[i].pressed = 0U;
        g_hall_keys[i].note_on_latched = 0U;
        g_hall_keys[i].note_off_latched = 0U;
        g_hall_keys[i].velocity = 0U;
        g_hall_keys[i].velocity_armed = 0U;
    }
}

void hall_engine_process(void)
{
    uint16_t raw;

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        if (hall_adc_consume_raw(i, &raw) == 0U)
        {
            continue;
        }

        hall_engine_process_key(i, raw);
    }
}

void hall_engine_set_calibration(const uint16_t *min_values, const uint16_t *max_values)
{
    if ((min_values == NULL) || (max_values == NULL))
    {
        return;
    }

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_engine_set_key_calibration(i, min_values[i], max_values[i]);
    }
}

void hall_engine_set_key_calibration(uint8_t key, uint16_t min_value, uint16_t max_value)
{
    hall_engine_key_t *state;

    if (key >= HALL_KEY_COUNT)
    {
        return;
    }

    state = &g_hall_keys[key];
    state->cal_min = min_value;
    state->cal_max = max_value;
    state->calibration_valid = (max_value > min_value) ? 1U : 0U;

    if (state->calibration_valid != 0U)
    {
        if ((state->initialized == 0U) || (state->observed_min == 0xFFFFU) || (min_value < state->observed_min))
        {
            state->observed_min = min_value;
        }

        if (max_value > state->observed_max)
        {
            state->observed_max = max_value;
        }
    }

    hall_engine_update_thresholds(state);
}

uint16_t hall_engine_get_raw(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].raw;
}

uint16_t hall_engine_get_filtered(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].filtered;
}

uint8_t hall_engine_get_pressed(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].pressed;
}

uint8_t hall_engine_consume_note_on(uint8_t key)
{
    uint8_t value;

    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    value = g_hall_keys[key].note_on_latched;
    g_hall_keys[key].note_on_latched = 0U;
    return value;
}

uint8_t hall_engine_consume_note_off(uint8_t key)
{
    uint8_t value;

    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    value = g_hall_keys[key].note_off_latched;
    g_hall_keys[key].note_off_latched = 0U;
    return value;
}

uint8_t hall_engine_get_note_on_latched(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].note_on_latched;
}

uint8_t hall_engine_get_note_off_latched(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].note_off_latched;
}

uint8_t hall_engine_get_velocity(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].velocity;
}

uint8_t hall_engine_get_range_valid(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].range_valid;
}

uint16_t hall_engine_get_cal_min(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].cal_min;
}

uint16_t hall_engine_get_cal_max(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].cal_max;
}

uint16_t hall_engine_get_observed_min(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].observed_min;
}

uint16_t hall_engine_get_observed_max(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].observed_max;
}

uint16_t hall_engine_get_trigger_press(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].trig_press;
}

uint16_t hall_engine_get_trigger_release(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].trig_release;
}

uint16_t hall_engine_get_velocity_start(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_keys[key].vel_start;
}

int16_t hall_engine_get_derivative(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0;
    }

    return g_hall_keys[key].derivative;
}

uint16_t hall_engine_get_derivative_peak(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    if (g_hall_keys[key].derivative_peak != 0U)
    {
        return g_hall_keys[key].derivative_peak;
    }

    return g_hall_keys[key].last_derivative_peak;
}

uint16_t hall_engine_get_attack_samples(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    if (g_hall_keys[key].attack_samples != 0U)
    {
        return g_hall_keys[key].attack_samples;
    }

    return g_hall_keys[key].last_attack_samples;
}

uint32_t hall_engine_get_sample_count(uint8_t key)
{
    return hall_adc_get_sample_count(key);
}
