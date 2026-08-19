#include "App/Hall/hall_calibration.h"
#include "stm32h7xx_hal.h"

#include <string.h>

#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_adc.h"
#include "Storage/memory_layout.h"

#define CALIBRATION_PRESS_ADC                 50000U
#define CALIBRATION_RELEASE_ADC               48000U

#define CALIBRATION_HOLD_TIME_MS              3000U
#define CALIBRATION_PROGRESS_MAX              100U

#define CALIBRATION_REST_SAMPLES_MAX          255U
#define CALIBRATION_HOLD_SAMPLES_MAX          255U
#define LOWCOST_CALIBRATION_STAGE_KEY_COUNT    12U
#define LOWCOST_CALIBRATION_PRESS_DELTA      1000U
#define LOWCOST_CALIBRATION_RELEASE_DELTA     500U
#define LOWCOST_CALIBRATION_REST_PRIME          8U

#define HALL_CAL_FLASH_ADDRESS                0x081E0000U
#define HALL_CAL_FLASH_BANK                   FLASH_BANK_2
#define HALL_CAL_FLASH_SECTOR                 FLASH_SECTOR_7

#define HALL_CAL_STORAGE_MAGIC                0x48435550UL
#if defined(BRICK6_VARIANT_LOWCOST)
#define HALL_CAL_STORAGE_VERSION              2U
#else
#define HALL_CAL_STORAGE_VERSION              1U
#endif
#define HALL_CAL_FLASH_CHUNKS                 ((sizeof(hall_calibration_storage_blob_t) + 31U) / 32U)

#define HALL_USER_STAGE_COUNT                 3U
#define HALL_USER_STAGE_SAMPLES               10U
#define HALL_USER_TRIAD_WINDOW_MS             35U
#define HALL_USER_TRIAD_LOCKOUT_MS            120U
#define HALL_USER_MIN_STAGE_GAP               4U

#if defined(BRICK6_VARIANT_LOWCOST)
_Static_assert(HALL_KEY_COUNT == (2U * LOWCOST_CALIBRATION_STAGE_KEY_COUNT),
               "Low-cost Hall calibration requires two complete 12-key stages");
#endif

typedef enum
{
    KEY_STATE_RELEASED = 0U,
    KEY_STATE_PRESSED
} hall_key_state_t;

typedef struct
{
    uint16_t samples[CALIBRATION_REST_SAMPLES_MAX];
    uint16_t count;
} hall_median_buffer_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    hall_calibration_blob_t hall;
    hall_user_velocity_profile_t user;
} hall_calibration_storage_v1_blob_t;

_Static_assert(sizeof(hall_calibration_storage_v1_blob_t) == 128U,
               "Hall calibration v1 storage layout changed");

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    hall_calibration_blob_t hall;
    hall_user_velocity_profile_t user;
#if defined(BRICK6_VARIANT_LOWCOST)
    uint8_t velocity_profile;
    uint8_t velocity_mode;
    uint8_t velocity_curve;
    uint8_t reserved0;
    uint8_t reserved1[28];
#endif
} hall_calibration_storage_blob_t;

#if defined(BRICK6_VARIANT_LOWCOST)
_Static_assert(sizeof(hall_calibration_storage_blob_t) == 160U,
               "Low-cost Hall calibration storage must fill five flash words");
#else
_Static_assert(sizeof(hall_calibration_storage_blob_t) == 128U,
               "Premium Hall calibration storage format must remain unchanged");
#endif

typedef struct
{
    hall_user_calibration_stage_t stage;
    uint8_t done;
    uint8_t success;
    uint8_t counts[HALL_USER_STAGE_COUNT];
    uint16_t samples[HALL_USER_STAGE_COUNT][HALL_USER_STAGE_SAMPLES];
    hall_velocity_capture_t pending[3];
    uint8_t pending_count;
    uint32_t lockout_until_ms;
} hall_user_calibration_state_t;

static hall_calibration_blob_t g_cal_blob;
static hall_user_velocity_profile_t g_user_profile;
static uint8_t g_press_count[HALL_KEY_COUNT];
static uint8_t g_key_done[HALL_KEY_COUNT];
static hall_key_state_t g_key_state[HALL_KEY_COUNT];
static uint8_t g_calibration_done = 0U;
static uint8_t g_calibration_stage = 0U;

static uint32_t g_hold_start_tick[HALL_KEY_COUNT];
CONTROL_STATE_SDRAM static hall_median_buffer_t g_min_buffer[HALL_KEY_COUNT];
CONTROL_STATE_SDRAM static hall_median_buffer_t g_max_buffer[HALL_KEY_COUNT];
static hall_user_calibration_state_t g_user_calibration;

static void hall_median_buffer_reset(hall_median_buffer_t *buffer)
{
    if (buffer == 0)
    {
        return;
    }

    buffer->count = 0U;
}

static void hall_median_buffer_push(hall_median_buffer_t *buffer,
                                    uint16_t value,
                                    uint16_t max_count)
{
    if ((buffer == 0) || (max_count == 0U))
    {
        return;
    }

    if (buffer->count < max_count)
    {
        buffer->samples[buffer->count] = value;
        buffer->count++;
        return;
    }

    for (uint16_t i = 1U; i < max_count; i++)
    {
        buffer->samples[(uint16_t)(i - 1U)] = buffer->samples[i];
    }

    buffer->samples[(uint16_t)(max_count - 1U)] = value;
}

static uint16_t hall_median_buffer_compute(const hall_median_buffer_t *buffer)
{
    uint16_t work[CALIBRATION_REST_SAMPLES_MAX];
    uint16_t count;

    if ((buffer == 0) || (buffer->count == 0U))
    {
        return 0U;
    }

    count = buffer->count;

    for (uint16_t i = 0U; i < count; i++)
    {
        work[i] = buffer->samples[i];
    }

    for (uint16_t i = 1U; i < count; i++)
    {
        const uint16_t key = work[i];
        uint16_t j = i;

        while ((j > 0U) && (work[(uint16_t)(j - 1U)] > key))
        {
            work[j] = work[(uint16_t)(j - 1U)];
            j--;
        }

        work[j] = key;
    }

    return work[count / 2U];
}

static void hall_sort_u16(uint16_t *values, uint8_t count)
{
    if (values == 0)
    {
        return;
    }

    for (uint8_t i = 1U; i < count; i++)
    {
        const uint16_t key = values[i];
        uint8_t j = i;

        while ((j > 0U) && (values[(uint8_t)(j - 1U)] > key))
        {
            values[j] = values[(uint8_t)(j - 1U)];
            j--;
        }

        values[j] = key;
    }
}

static uint8_t hall_calibration_blob_is_valid(const hall_calibration_blob_t *blob)
{
    uint8_t any_written = 0U;

    if (blob == 0)
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        if ((blob->min[i] != 0xFFFFU) || (blob->max[i] != 0xFFFFU))
        {
            any_written = 1U;
        }

        if (blob->min[i] >= blob->max[i])
        {
            return 0U;
        }
    }

    return any_written;
}

static uint8_t hall_user_profile_is_valid_local(const hall_user_velocity_profile_t *profile)
{
    if ((profile == 0) || (profile->valid == 0U))
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

    if ((profile->soft.median + HALL_USER_MIN_STAGE_GAP) >= profile->mid.median)
    {
        return 0U;
    }
    if ((profile->mid.median + HALL_USER_MIN_STAGE_GAP) >= profile->fort.median)
    {
        return 0U;
    }

    if (profile->soft.q3 >= profile->fort.q1)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t hall_storage_blob_is_valid(const hall_calibration_storage_blob_t *blob)
{
    if (blob == 0)
    {
        return 0U;
    }

    if ((blob->magic != HALL_CAL_STORAGE_MAGIC) ||
        (blob->version != HALL_CAL_STORAGE_VERSION) ||
        (blob->size != sizeof(hall_calibration_storage_blob_t)))
    {
        return 0U;
    }

    return hall_calibration_blob_is_valid(&blob->hall);
}

#if defined(BRICK6_VARIANT_LOWCOST)
static uint8_t hall_storage_v1_blob_is_valid(const hall_calibration_storage_v1_blob_t *blob)
{
    if (blob == 0)
    {
        return 0U;
    }

    if ((blob->magic != HALL_CAL_STORAGE_MAGIC) ||
        (blob->version != 1U) ||
        (blob->size != sizeof(hall_calibration_storage_v1_blob_t)))
    {
        return 0U;
    }

    return hall_calibration_blob_is_valid(&blob->hall);
}

static void hall_velocity_settings_apply(uint8_t profile, uint8_t mode, uint8_t curve)
{
    hall_set_velocity_profile((profile < (uint8_t)HALL_VEL_PROFILE_COUNT)
                                  ? profile
                                  : (uint8_t)HALL_VEL_PROFILE_DEFAULT);
    hall_set_velocity_mode((mode < (uint8_t)HALL_VEL_MODE_USER)
                               ? mode
                               : (uint8_t)HALL_VEL_MODE_DV_PEAK);
    hall_set_velocity_curve((curve < (uint8_t)HALL_VEL_CURVE_COUNT)
                                ? curve
                                : (uint8_t)HALL_VEL_CURVE_LINEAR);
}
#endif

static uint8_t hall_user_stage_to_index(hall_user_calibration_stage_t stage)
{
    switch (stage)
    {
        case HALL_USER_CAL_STAGE_FORT:
            return 0U;

        case HALL_USER_CAL_STAGE_MID:
            return 1U;

        case HALL_USER_CAL_STAGE_SOFT:
            return 2U;

        case HALL_USER_CAL_STAGE_DONE:
        case HALL_USER_CAL_STAGE_IDLE:
        default:
            return 0xFFU;
    }
}

static hall_user_calibration_stage_t hall_user_stage_advance(hall_user_calibration_stage_t stage)
{
    switch (stage)
    {
        case HALL_USER_CAL_STAGE_FORT:
            return HALL_USER_CAL_STAGE_MID;

        case HALL_USER_CAL_STAGE_MID:
            return HALL_USER_CAL_STAGE_SOFT;

        case HALL_USER_CAL_STAGE_SOFT:
            return HALL_USER_CAL_STAGE_DONE;

        case HALL_USER_CAL_STAGE_DONE:
        case HALL_USER_CAL_STAGE_IDLE:
        default:
            return HALL_USER_CAL_STAGE_DONE;
    }
}

static uint16_t hall_user_median3(uint16_t a, uint16_t b, uint16_t c)
{
    if (a > b)
    {
        const uint16_t tmp = a;
        a = b;
        b = tmp;
    }
    if (b > c)
    {
        const uint16_t tmp = b;
        b = c;
        c = tmp;
    }
    if (a > b)
    {
        b = a;
    }

    return b;
}

static uint8_t hall_user_build_zone(const uint16_t *samples, hall_user_velocity_zone_t *zone)
{
    uint16_t work[HALL_USER_STAGE_SAMPLES];

    if ((samples == 0) || (zone == 0))
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < HALL_USER_STAGE_SAMPLES; i++)
    {
        work[i] = samples[i];
    }

    hall_sort_u16(work, HALL_USER_STAGE_SAMPLES);

    zone->q1 = work[2];
    zone->median = (uint16_t)(((uint32_t)work[4] + (uint32_t)work[5]) / 2U);
    zone->q3 = work[7];

    return (zone->q1 <= zone->median) && (zone->median <= zone->q3);
}

static uint8_t hall_user_build_profile(hall_user_velocity_profile_t *profile)
{
    if (profile == 0)
    {
        return 0U;
    }

    memset(profile, 0, sizeof(*profile));

    if (hall_user_build_zone(g_user_calibration.samples[2], &profile->soft) == 0U)
    {
        return 0U;
    }
    if (hall_user_build_zone(g_user_calibration.samples[1], &profile->mid) == 0U)
    {
        return 0U;
    }
    if (hall_user_build_zone(g_user_calibration.samples[0], &profile->fort) == 0U)
    {
        return 0U;
    }

    profile->valid = 1U;
    return hall_user_profile_is_valid_local(profile);
}

#if defined(BRICK6_VARIANT_LOWCOST)
static uint8_t hall_calibration_lowcost_pressed(uint8_t key, uint16_t value)
{
    if ((key >= HALL_KEY_COUNT) || (g_min_buffer[key].count < LOWCOST_CALIBRATION_REST_PRIME))
    {
        return 0U;
    }

    const uint16_t rest = hall_median_buffer_compute(&g_min_buffer[key]);
    return ((uint32_t)rest > ((uint32_t)value + LOWCOST_CALIBRATION_PRESS_DELTA)) ? 1U : 0U;
}

static uint8_t hall_calibration_lowcost_released(uint8_t key, uint16_t value)
{
    if ((key >= HALL_KEY_COUNT) || (g_min_buffer[key].count == 0U))
    {
        return 0U;
    }

    const uint16_t rest = hall_median_buffer_compute(&g_min_buffer[key]);
    return (((uint32_t)value + LOWCOST_CALIBRATION_RELEASE_DELTA) >= (uint32_t)rest) ? 1U : 0U;
}

static void hall_calibration_lowcost_store_raw_span(uint8_t key,
                                                    uint16_t a,
                                                    uint16_t b)
{
    if (key >= HALL_KEY_COUNT)
    {
        return;
    }

    if (a <= b)
    {
        g_cal_blob.min[key] = a;
        g_cal_blob.max[key] = b;
    }
    else
    {
        g_cal_blob.min[key] = b;
        g_cal_blob.max[key] = a;
    }
}
#endif

static void hall_user_profile_apply(const hall_user_velocity_profile_t *profile)
{
    if ((profile != 0) && (hall_user_profile_is_valid_local(profile) != 0U))
    {
        g_user_profile = *profile;
        g_user_profile.valid = 1U;
        hall_engine_set_user_velocity_profile(&g_user_profile);
    }
    else
    {
        memset(&g_user_profile, 0, sizeof(g_user_profile));
        hall_engine_set_user_velocity_profile(0);
    }
}

static void hall_user_calibration_reset_pending(void)
{
    g_user_calibration.pending_count = 0U;
    g_user_calibration.lockout_until_ms = 0U;
}

static void hall_user_calibration_finish(uint8_t success)
{
    g_user_calibration.done = 1U;
    g_user_calibration.success = success;
    g_user_calibration.stage = HALL_USER_CAL_STAGE_DONE;
    hall_user_calibration_reset_pending();
}

static void hall_user_calibration_store_sample(uint16_t metric)
{
    const uint8_t stage_idx = hall_user_stage_to_index(g_user_calibration.stage);

    if (stage_idx >= HALL_USER_STAGE_COUNT)
    {
        return;
    }

    if (g_user_calibration.counts[stage_idx] < HALL_USER_STAGE_SAMPLES)
    {
        const uint8_t sample_idx = g_user_calibration.counts[stage_idx];

        g_user_calibration.samples[stage_idx][sample_idx] = metric;
        g_user_calibration.counts[stage_idx]++;
    }

    if (g_user_calibration.counts[stage_idx] >= HALL_USER_STAGE_SAMPLES)
    {
        if (g_user_calibration.stage == HALL_USER_CAL_STAGE_SOFT)
        {
            hall_user_velocity_profile_t profile;

            if (hall_user_build_profile(&profile) != 0U)
            {
                hall_user_profile_apply(&profile);
                hall_user_calibration_finish(1U);
            }
            else
            {
                hall_user_calibration_finish(0U);
            }
        }
        else
        {
            g_user_calibration.stage = hall_user_stage_advance(g_user_calibration.stage);
        }
    }
}

void hall_calibration_start(void)
{
    g_calibration_done = 0U;
    g_calibration_stage = 0U;

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        g_cal_blob.min[i] = 0xFFFFU;
        g_cal_blob.max[i] = 0U;
        g_press_count[i] = 0U;
        g_key_done[i] = 0U;
        g_key_state[i] = KEY_STATE_RELEASED;
        g_hold_start_tick[i] = 0U;
        hall_median_buffer_reset(&g_min_buffer[i]);
        hall_median_buffer_reset(&g_max_buffer[i]);
    }
}

void hall_calibration_process(void)
{
    uint8_t first_key = 0U;
    uint8_t key_count = HALL_KEY_COUNT;

    if (g_calibration_done != 0U)
    {
        return;
    }

#if defined(BRICK6_VARIANT_LOWCOST)
    first_key = (uint8_t)(g_calibration_stage * LOWCOST_CALIBRATION_STAGE_KEY_COUNT);
    key_count = LOWCOST_CALIBRATION_STAGE_KEY_COUNT;
#endif

    uint8_t done_count = 0U;

    for (uint8_t key_index = 0U; key_index < key_count; key_index++)
    {
        const uint8_t i = (uint8_t)(first_key + key_index);
        const uint16_t v = hall_adc_get_raw(i);

        if (g_key_done[i] == 0U)
        {
            if (g_key_state[i] == KEY_STATE_RELEASED)
            {
                hall_median_buffer_push(&g_min_buffer[i], v, CALIBRATION_REST_SAMPLES_MAX);

#if defined(BRICK6_VARIANT_LOWCOST)
                if (hall_calibration_lowcost_pressed(i, v) != 0U)
#else
                if (v > CALIBRATION_PRESS_ADC)
#endif
                {
                    g_key_state[i] = KEY_STATE_PRESSED;
                    g_hold_start_tick[i] = HAL_GetTick();
                    hall_median_buffer_reset(&g_max_buffer[i]);
                    hall_median_buffer_push(&g_max_buffer[i], v, CALIBRATION_HOLD_SAMPLES_MAX);
                    g_press_count[i] = 0U;
                }
            }
            else
            {
#if defined(BRICK6_VARIANT_LOWCOST)
                if (hall_calibration_lowcost_released(i, v) != 0U)
#else
                if (v < CALIBRATION_RELEASE_ADC)
#endif
                {
                    g_key_state[i] = KEY_STATE_RELEASED;
                    g_hold_start_tick[i] = 0U;
                    g_press_count[i] = 0U;
                    hall_median_buffer_reset(&g_max_buffer[i]);
                }
                else
                {
                    const uint32_t elapsed_ms = HAL_GetTick() - g_hold_start_tick[i];
                    uint32_t progress = (elapsed_ms * CALIBRATION_PROGRESS_MAX) / CALIBRATION_HOLD_TIME_MS;

                    hall_median_buffer_push(&g_max_buffer[i], v, CALIBRATION_HOLD_SAMPLES_MAX);

                    if (progress > CALIBRATION_PROGRESS_MAX)
                    {
                        progress = CALIBRATION_PROGRESS_MAX;
                    }

                    g_press_count[i] = (uint8_t)progress;

                    if (elapsed_ms >= CALIBRATION_HOLD_TIME_MS)
                    {
                        const uint16_t min_median = hall_median_buffer_compute(&g_min_buffer[i]);
                        const uint16_t max_median = hall_median_buffer_compute(&g_max_buffer[i]);

#if defined(BRICK6_VARIANT_LOWCOST)
                        hall_calibration_lowcost_store_raw_span(i, min_median, max_median);
#else
                        g_cal_blob.min[i] = min_median;
                        g_cal_blob.max[i] = max_median;
#endif
                        g_key_done[i] = 1U;
                        g_press_count[i] = CALIBRATION_PROGRESS_MAX;
                    }
                }
            }
        }
        else
        {
#if defined(BRICK6_VARIANT_LOWCOST)
            if (hall_calibration_lowcost_released(i, v) != 0U)
            {
                hall_median_buffer_push(&g_min_buffer[i], v, CALIBRATION_REST_SAMPLES_MAX);
            }
#else
            hall_median_buffer_push(&g_min_buffer[i], v, CALIBRATION_REST_SAMPLES_MAX);
#endif
        }

        if (g_key_done[i] != 0U)
        {
            done_count++;
        }
    }

    if (done_count == key_count)
    {
        uint8_t stage_valid = 1U;

        for (uint8_t key_index = 0U; key_index < key_count; key_index++)
        {
            const uint8_t i = (uint8_t)(first_key + key_index);
            const uint16_t min_median = hall_median_buffer_compute(&g_min_buffer[i]);

#if defined(BRICK6_VARIANT_LOWCOST)
            hall_calibration_lowcost_store_raw_span(i, min_median, g_cal_blob.min[i]);
#else
            if (min_median < g_cal_blob.max[i])
            {
                g_cal_blob.min[i] = min_median;
            }
#endif

#if defined(BRICK6_VARIANT_LOWCOST)
            if (g_cal_blob.min[i] >= g_cal_blob.max[i])
            {
                g_cal_blob.min[i] = 0xFFFFU;
                g_cal_blob.max[i] = 0U;
                g_press_count[i] = 0U;
                g_key_done[i] = 0U;
                g_key_state[i] = KEY_STATE_RELEASED;
                g_hold_start_tick[i] = 0U;
                hall_median_buffer_reset(&g_min_buffer[i]);
                hall_median_buffer_reset(&g_max_buffer[i]);
                stage_valid = 0U;
            }
#endif
        }

#if defined(BRICK6_VARIANT_LOWCOST)
        if (stage_valid == 0U)
        {
            return;
        }

        if (g_calibration_stage == 0U)
        {
            g_calibration_stage = 1U;
            return;
        }

        if (hall_calibration_blob_is_valid(&g_cal_blob) == 0U)
        {
            return;
        }
#else
        (void)stage_valid;
#endif

        g_calibration_done = 1U;
        hall_engine_set_calibration(g_cal_blob.min, g_cal_blob.max);
    }
}

uint8_t hall_calibration_get_count(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_press_count[key];
}

uint8_t hall_calibration_is_done(void)
{
    return g_calibration_done;
}

uint8_t hall_calibration_get_stage(void)
{
    return g_calibration_stage;
}

uint8_t hall_calibration_is_key_done(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_key_done[key];
}

uint16_t hall_calibration_get_min(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_cal_blob.min[key];
}

uint16_t hall_calibration_get_max(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_cal_blob.max[key];
}

uint8_t hall_calibration_load(void)
{
    const hall_calibration_storage_blob_t *stored = (const hall_calibration_storage_blob_t *)HALL_CAL_FLASH_ADDRESS;
#if defined(BRICK6_VARIANT_LOWCOST)
    const hall_calibration_storage_v1_blob_t *stored_v1 =
        (const hall_calibration_storage_v1_blob_t *)HALL_CAL_FLASH_ADDRESS;
#endif
    const hall_calibration_blob_t *legacy = (const hall_calibration_blob_t *)HALL_CAL_FLASH_ADDRESS;

    memset(&g_user_profile, 0, sizeof(g_user_profile));
    hall_engine_set_user_velocity_profile(0);

    if (hall_storage_blob_is_valid(stored) != 0U)
    {
        g_cal_blob = stored->hall;
        if (hall_user_profile_is_valid_local(&stored->user) != 0U)
        {
            hall_user_profile_apply(&stored->user);
        }

#if defined(BRICK6_VARIANT_LOWCOST)
        hall_velocity_settings_apply(stored->velocity_profile,
                                     stored->velocity_mode,
                                     stored->velocity_curve);
#endif
        hall_engine_set_calibration(g_cal_blob.min, g_cal_blob.max);
        return 1U;
    }

#if defined(BRICK6_VARIANT_LOWCOST)
    if (hall_storage_v1_blob_is_valid(stored_v1) != 0U)
    {
        g_cal_blob = stored_v1->hall;
        if (hall_user_profile_is_valid_local(&stored_v1->user) != 0U)
        {
            hall_user_profile_apply(&stored_v1->user);
        }
        hall_velocity_settings_apply((hall_user_profile_is_valid_local(&stored_v1->user) != 0U)
                                         ? (uint8_t)HALL_VEL_PROFILE_USER
                                         : (uint8_t)HALL_VEL_PROFILE_DEFAULT,
                                     (uint8_t)HALL_VEL_MODE_DV_PEAK,
                                     (uint8_t)HALL_VEL_CURVE_LOG);
        hall_engine_set_calibration(g_cal_blob.min, g_cal_blob.max);
        return 1U;
    }
#endif

    if (hall_calibration_blob_is_valid(legacy) == 0U)
    {
        return 0U;
    }

    g_cal_blob = *legacy;
    hall_engine_set_calibration(g_cal_blob.min, g_cal_blob.max);

    return 1U;
}

void hall_calibration_save(void)
{
    hall_calibration_storage_blob_t blob = {
        .magic = HALL_CAL_STORAGE_MAGIC,
        .version = HALL_CAL_STORAGE_VERSION,
        .size = sizeof(hall_calibration_storage_blob_t),
        .hall = { {0}, {0} },
        .user = {{0}}
    };

    if (hall_calibration_blob_is_valid(&g_cal_blob) == 0U)
    {
        return;
    }

    blob.hall = g_cal_blob;
    blob.user = g_user_profile;
#if defined(BRICK6_VARIANT_LOWCOST)
    blob.velocity_profile = hall_get_velocity_profile();
    blob.velocity_mode = hall_get_velocity_mode();
    blob.velocity_curve = hall_get_velocity_curve();
#endif

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0U;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Banks = HALL_CAL_FLASH_BANK;
    erase.Sector = HALL_CAL_FLASH_SECTOR;
    erase.NbSectors = 1U;

    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return;
    }

    for (uint32_t i = 0U; i < HALL_CAL_FLASH_CHUNKS; i++)
    {
        const uint32_t src_address = (uint32_t)(uintptr_t)&((const uint8_t *)&blob)[i * 32U];
        const uint32_t dst_address = HALL_CAL_FLASH_ADDRESS + (i * 32U);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, dst_address, src_address) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return;
        }
    }

    HAL_FLASH_Lock();
}

void hall_user_calibration_start(void)
{
    memset(&g_user_calibration, 0, sizeof(g_user_calibration));
    g_user_calibration.stage = HALL_USER_CAL_STAGE_FORT;

    hall_velocity_capture_t capture;
    while (hall_engine_pop_velocity_capture(&capture) != 0U)
    {
    }
}

void hall_user_calibration_process(void)
{
    hall_velocity_capture_t capture;

    if (g_user_calibration.done != 0U)
    {
        return;
    }

    while (hall_engine_pop_velocity_capture(&capture) != 0U)
    {
        uint8_t duplicate = 0U;

        if (capture.metric == 0U)
        {
            continue;
        }

        if (capture.tick_ms < g_user_calibration.lockout_until_ms)
        {
            continue;
        }

        while ((g_user_calibration.pending_count > 0U) &&
               ((capture.tick_ms - g_user_calibration.pending[0].tick_ms) > HALL_USER_TRIAD_WINDOW_MS))
        {
            for (uint8_t i = 1U; i < g_user_calibration.pending_count; i++)
            {
                g_user_calibration.pending[(uint8_t)(i - 1U)] = g_user_calibration.pending[i];
            }
            g_user_calibration.pending_count--;
        }

        for (uint8_t i = 0U; i < g_user_calibration.pending_count; i++)
        {
            if (g_user_calibration.pending[i].key == capture.key)
            {
                duplicate = 1U;
                break;
            }
        }

        if (duplicate != 0U)
        {
            continue;
        }

        if (g_user_calibration.pending_count < 3U)
        {
            g_user_calibration.pending[g_user_calibration.pending_count] = capture;
            g_user_calibration.pending_count++;
        }
        else
        {
            g_user_calibration.pending[0] = g_user_calibration.pending[1];
            g_user_calibration.pending[1] = g_user_calibration.pending[2];
            g_user_calibration.pending[2] = capture;
            g_user_calibration.pending_count = 3U;
        }

        if (g_user_calibration.pending_count >= 3U)
        {
            const uint16_t triad_metric = hall_user_median3(g_user_calibration.pending[0].metric,
                                                            g_user_calibration.pending[1].metric,
                                                            g_user_calibration.pending[2].metric);

            hall_user_calibration_store_sample(triad_metric);
            g_user_calibration.lockout_until_ms = capture.tick_ms + HALL_USER_TRIAD_LOCKOUT_MS;
            g_user_calibration.pending_count = 0U;

            if (g_user_calibration.done != 0U)
            {
                break;
            }
        }
    }
}

uint8_t hall_user_calibration_is_done(void)
{
    return g_user_calibration.done;
}

uint8_t hall_user_calibration_was_successful(void)
{
    return g_user_calibration.success;
}

hall_user_calibration_stage_t hall_user_calibration_get_stage(void)
{
    return g_user_calibration.stage;
}

uint8_t hall_user_calibration_get_stage_count(void)
{
    const uint8_t stage_idx = hall_user_stage_to_index(g_user_calibration.stage);

    if (stage_idx >= HALL_USER_STAGE_COUNT)
    {
        return 0U;
    }

    return g_user_calibration.counts[stage_idx];
}

uint8_t hall_user_calibration_get_target_count(void)
{
    return HALL_USER_STAGE_SAMPLES;
}

uint8_t hall_user_velocity_profile_is_valid(void)
{
    return hall_user_profile_is_valid_local(&g_user_profile);
}

void hall_user_velocity_profile_get(hall_user_velocity_profile_t *profile)
{
    if (profile == 0)
    {
        return;
    }

    *profile = g_user_profile;
}
