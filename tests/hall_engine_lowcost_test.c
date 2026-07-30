#include <stdint.h>
#include <stdio.h>

#include "App/Hall/hall_engine.h"

static uint32_t g_tick_ms;
static uint32_t g_sample_count;
static int g_failures;

uint32_t HAL_GetTick(void)
{
    return g_tick_ms;
}

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        (void)fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

static void sample_key(uint16_t raw)
{
    g_tick_ms += 3U;
    g_sample_count++;
    hall_engine_process_sample(0U, raw, g_sample_count);
}

static uint8_t strike(const uint16_t *raws, uint8_t count)
{
    sample_key(5000U);
    (void)hall_engine_consume_note_on(0U);
    (void)hall_engine_consume_note_off(0U);

    for (uint8_t i = 0U; i < count; ++i)
    {
        sample_key(raws[i]);
    }

    expect_true(hall_engine_consume_note_on(0U) == 1U, "strike must emit one note-on");
    expect_true(hall_engine_consume_note_on(0U) == 0U, "note-on must not duplicate");
    const uint8_t velocity = hall_engine_get_velocity(0U);

    sample_key(2500U);
    sample_key(2495U);
    sample_key(2505U);
    expect_true(hall_engine_consume_note_on(0U) == 0U, "held raw samples must not retrigger");

    sample_key(5000U);
    expect_true(hall_engine_consume_note_off(0U) == 1U, "release must emit one note-off");
    expect_true(hall_engine_consume_note_off(0U) == 0U, "note-off must not duplicate");
    return velocity;
}

int main(void)
{
    uint16_t min_values[HALL_KEY_COUNT];
    uint16_t max_values[HALL_KEY_COUNT];
    for (uint8_t key = 0U; key < HALL_KEY_COUNT; ++key)
    {
        min_values[key] = 1000U;
        max_values[key] = 5000U;
    }

    hall_engine_init();
    hall_engine_set_calibration(min_values, max_values);
    hall_set_velocity_profile((uint8_t)HALL_VEL_PROFILE_DEFAULT);
    hall_set_velocity_mode((uint8_t)HALL_VEL_MODE_DV_PEAK);
    hall_set_velocity_curve((uint8_t)HALL_VEL_CURVE_LINEAR);

    {
        static const uint16_t idle_raw[] = {5000U, 4992U, 5006U, 4988U, 5004U, 4997U};
        for (uint8_t i = 0U; i < (uint8_t)(sizeof(idle_raw) / sizeof(idle_raw[0])); ++i)
        {
            sample_key(idle_raw[i]);
        }
        expect_true(hall_engine_consume_note_on(0U) == 0U, "idle raw noise must not emit note-on");
        expect_true(hall_engine_consume_note_off(0U) == 0U, "idle raw noise must not emit note-off");
    }

    {
        static const uint16_t slow_raw[] = {4600U, 4200U, 3800U, 3400U, 3000U};
        static const uint16_t fast_raw[] = {3500U, 2500U};
        const uint8_t slow_velocity = strike(slow_raw, (uint8_t)(sizeof(slow_raw) / sizeof(slow_raw[0])));
        const uint8_t fast_velocity = strike(fast_raw, (uint8_t)(sizeof(fast_raw) / sizeof(fast_raw[0])));
        expect_true(fast_velocity > slow_velocity, "2.8 ms raw DV must preserve velocity detail");
    }

    {
        hall_velocity_debug_t debug = {0};
        hall_engine_get_velocity_debug(0U, &debug);
        expect_true(debug.sample_period_us == 2800U, "low-cost debug cadence must report 2.8 ms");
        expect_true(debug.sample_count == g_sample_count, "engine must consume every raw sample");
    }

    hall_engine_set_user_velocity_profile(0);
    hall_set_velocity_profile((uint8_t)HALL_VEL_PROFILE_USER);
    {
        static const uint16_t raw[] = {3500U, 2500U};
        (void)strike(raw, (uint8_t)(sizeof(raw) / sizeof(raw[0])));
        hall_velocity_debug_t debug = {0};
        hall_engine_get_velocity_debug(0U, &debug);
        expect_true(debug.user_mode_fallback != 0U, "invalid USER profile must be explicit");
    }

    {
        const hall_user_velocity_profile_t profile = {
            .soft = {100U, 200U, 300U},
            .mid = {400U, 500U, 600U},
            .fort = {700U, 800U, 900U},
            .valid = 1U
        };
        static const uint16_t raw[] = {3500U, 2500U};
        hall_engine_set_user_velocity_profile(&profile);
        hall_set_velocity_profile((uint8_t)HALL_VEL_PROFILE_USER);
        const uint8_t user_velocity = strike(raw, (uint8_t)(sizeof(raw) / sizeof(raw[0])));
        expect_true(user_velocity == 127U, "valid USER profile must drive velocity");
    }

    return (g_failures == 0) ? 0 : 1;
}
