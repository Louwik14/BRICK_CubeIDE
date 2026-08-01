#include <stdint.h>
#include <stdio.h>

#include "App/Hall/hall_engine.h"

static uint32_t g_tick_ms;
static uint32_t g_sample_count;
static int g_failures;

#if defined(BRICK6_VARIANT_LOWCOST)
#define TEST_REST_RAW       5000U
#define TEST_RELEASE_RAW    5000U
#define TEST_HELD_RAW       2500U
#define TEST_SLOW_RAW       {4600U, 4200U, 3800U, 3400U, 3000U}
#define TEST_FAST_RAW       {3500U, 2500U}
#define TEST_TRIG_LO        3600U
#define TEST_TRIG_HI        4000U
#else
#define TEST_REST_RAW       1000U
#define TEST_RELEASE_RAW    1000U
#define TEST_HELD_RAW       3500U
#define TEST_SLOW_RAW       {1400U, 1800U, 2200U, 2600U, 3000U}
#define TEST_FAST_RAW       {1500U, 2500U}
#define TEST_TRIG_LO        2000U
#define TEST_TRIG_HI        2400U
#endif

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
    sample_key(TEST_REST_RAW);
    (void)hall_engine_consume_note_on(0U);
    (void)hall_engine_consume_note_off(0U);

    for (uint8_t i = 0U; i < count; ++i)
    {
        sample_key(raws[i]);
    }

    expect_true(hall_engine_consume_note_on(0U) == 1U, "strike must emit one note-on");
    expect_true(hall_engine_consume_note_on(0U) == 0U, "note-on must not duplicate");
    const uint8_t velocity = hall_engine_get_velocity(0U);

    sample_key(TEST_HELD_RAW);
    sample_key((uint16_t)(TEST_HELD_RAW - 5U));
    sample_key((uint16_t)(TEST_HELD_RAW + 5U));
    expect_true(hall_engine_consume_note_on(0U) == 0U, "held raw samples must not retrigger");

    sample_key(TEST_RELEASE_RAW);
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
    expect_true(hall_engine_get_trig_lo(0U) == TEST_TRIG_LO,
                "calibrated release threshold must use the variant ppm contract");
    expect_true(hall_engine_get_trig_hi(0U) == TEST_TRIG_HI,
                "calibrated press threshold must use the variant ppm contract");
    hall_set_velocity_profile((uint8_t)HALL_VEL_PROFILE_DEFAULT);
    hall_set_velocity_mode((uint8_t)HALL_VEL_MODE_DV_PEAK);
    hall_set_velocity_curve((uint8_t)HALL_VEL_CURVE_LINEAR);

    {
        static const uint16_t idle_raw[] = {
            TEST_REST_RAW,
#if defined(BRICK6_VARIANT_LOWCOST)
            4992U, 5006U, 4988U, 5004U, 4997U
#else
            1008U, 994U, 1012U, 996U, 1003U
#endif
        };
        for (uint8_t i = 0U; i < (uint8_t)(sizeof(idle_raw) / sizeof(idle_raw[0])); ++i)
        {
            sample_key(idle_raw[i]);
        }
        expect_true(hall_engine_consume_note_on(0U) == 0U, "idle raw noise must not emit note-on");
        expect_true(hall_engine_consume_note_off(0U) == 0U, "idle raw noise must not emit note-off");
    }

    {
        static const uint16_t slow_raw[] = TEST_SLOW_RAW;
        static const uint16_t fast_raw[] = TEST_FAST_RAW;
        const uint8_t slow_velocity = strike(slow_raw, (uint8_t)(sizeof(slow_raw) / sizeof(slow_raw[0])));
        const uint8_t fast_velocity = strike(fast_raw, (uint8_t)(sizeof(fast_raw) / sizeof(fast_raw[0])));
        expect_true(fast_velocity > slow_velocity, "2.8 ms raw DV must preserve velocity detail");
    }

    {
        hall_velocity_debug_t debug = {0};
        hall_engine_get_velocity_debug(0U, &debug);
#if defined(BRICK6_VARIANT_LOWCOST)
        expect_true(debug.sample_period_us == 2800U, "low-cost debug cadence must report 2.8 ms");
#else
        expect_true(debug.sample_period_us == 800U, "premium debug cadence must report 0.8 ms");
#endif
        expect_true(debug.sample_count == g_sample_count, "engine must consume every raw sample");
    }

    hall_engine_set_user_velocity_profile(0);
    hall_set_velocity_profile((uint8_t)HALL_VEL_PROFILE_USER);
    {
        static const uint16_t raw[] = TEST_FAST_RAW;
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
        static const uint16_t raw[] = TEST_FAST_RAW;
        hall_engine_set_user_velocity_profile(&profile);
        hall_set_velocity_profile((uint8_t)HALL_VEL_PROFILE_USER);
        const uint8_t user_velocity = strike(raw, (uint8_t)(sizeof(raw) / sizeof(raw[0])));
        expect_true(user_velocity == 127U, "valid USER profile must drive velocity");
    }

    return (g_failures == 0) ? 0 : 1;
}
