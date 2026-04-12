#include "App/Hall/hall_filter_asc.h"

typedef struct
{
    uint32_t sum;
    uint16_t last;
    uint8_t factor;
    uint8_t count;
} hall_filter_asc_state_t;

static hall_filter_asc_state_t g_hall_filter_asc[HALL_KEY_COUNT];

static uint8_t hall_filter_asc_sanitize_factor(uint8_t factor)
{
    return (factor == 0U) ? 1U : factor;
}

void hall_filter_asc_reset_key(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return;
    }

    g_hall_filter_asc[key].sum = 0U;
    g_hall_filter_asc[key].count = 0U;
    g_hall_filter_asc[key].last = 0U;
}

void hall_filter_asc_reset_all(void)
{
    for (uint8_t key = 0U; key < HALL_KEY_COUNT; key++)
    {
        hall_filter_asc_reset_key(key);
    }
}

void hall_filter_asc_set_factor(uint8_t key, uint8_t factor)
{
    if (key >= HALL_KEY_COUNT)
    {
        return;
    }

    g_hall_filter_asc[key].factor = hall_filter_asc_sanitize_factor(factor);
    hall_filter_asc_reset_key(key);
}

void hall_filter_asc_set_factor_range(uint8_t start, uint8_t length, uint8_t factor)
{
    const uint8_t clamped_factor = hall_filter_asc_sanitize_factor(factor);
    const uint16_t end = (uint16_t)start + (uint16_t)length;

    if ((start >= HALL_KEY_COUNT) || (end > HALL_KEY_COUNT))
    {
        return;
    }

    for (uint8_t key = start; key < end; key++)
    {
        g_hall_filter_asc[key].factor = clamped_factor;
        hall_filter_asc_reset_key(key);
    }
}

void hall_filter_asc_init(void)
{
    for (uint8_t key = 0U; key < HALL_KEY_COUNT; key++)
    {
        g_hall_filter_asc[key].factor = HALL_FILTER_ASC_FACTOR_DEFAULT;
        hall_filter_asc_reset_key(key);
    }
}

uint8_t hall_filter_asc_process(uint8_t key, uint16_t raw, uint16_t *filtered_raw)
{
    hall_filter_asc_state_t *state;
    uint8_t factor;

    if ((key >= HALL_KEY_COUNT) || (filtered_raw == 0))
    {
        return 0U;
    }

    state = &g_hall_filter_asc[key];
    factor = hall_filter_asc_sanitize_factor(state->factor);

    state->sum += raw;

    *filtered_raw = (uint16_t)(state->sum / ((uint32_t)state->count + 1U));

    state->count = (uint8_t)((state->count + 1U) % factor);
    state->last = *filtered_raw;

    if (state->count == 0U)
    {
        state->sum = 0U;
        return 1U;
    }

    return 0U;
}

uint8_t hall_filter_asc_get_factor(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_filter_asc[key].factor;
}

uint8_t hall_filter_asc_get_count(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_filter_asc[key].count;
}

uint16_t hall_filter_asc_get_last(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_filter_asc[key].last;
}
