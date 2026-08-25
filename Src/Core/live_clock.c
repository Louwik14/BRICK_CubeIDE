#include "Core/live_clock.h"

#include "Board/board_audio_format.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    live_clock_anchor_t value;
    volatile uint32_t sequence;
    volatile uint8_t valid;
} live_clock_state_t;

D3_IPC static live_clock_state_t g_live_clock;
static uint32_t g_tim5_hz;
static uint32_t g_samples_per_tim5_tick_q32;

static uint32_t live_clock_tim5_frequency(void)
{
    uint32_t tim_kernel_hz = HAL_RCC_GetPCLK1Freq();
    const uint32_t apb1_prescaler = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1);

    if (apb1_prescaler != RCC_APB1_DIV1)
    {
        tim_kernel_hz *= 2U;
    }

    const uint32_t prescaler = (uint32_t)TIM5->PSC + 1U;
    if (prescaler == 0U)
    {
        return 0U;
    }

    return tim_kernel_hz / prescaler;
}

static uint32_t live_clock_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return primask;
}

static void live_clock_exit_critical(uint32_t primask)
{
    __DMB();
    __set_PRIMASK(primask);
}

static int64_t live_clock_round_q32(int64_t value)
{
    const int64_t half = (int64_t)1 << 31;
    if (value >= 0)
    {
        return (value + half) >> 32;
    }

    return -(((-value) + half) >> 32);
}

void live_clock_init(void)
{
    const uint32_t tim5_hz = live_clock_tim5_frequency();
    const uint32_t primask = live_clock_enter_critical();

    g_tim5_hz = tim5_hz;
    g_samples_per_tim5_tick_q32 = (tim5_hz != 0U)
        ? ((((uint64_t)BOARD_AUDIO_SAMPLE_RATE_HZ << 32)
            + ((uint64_t)tim5_hz / 2ULL)) / (uint64_t)tim5_hz)
        : 0ULL;
    g_live_clock.value = (live_clock_anchor_t){0};
    g_live_clock.sequence = 0U;
    g_live_clock.valid = 0U;

    live_clock_exit_critical(primask);
}

void live_clock_audio_publish_anchor(uint64_t audio_sample)
{
    const uint32_t tim5_tick = TIM5->CNT;
    ++g_live_clock.sequence;
    __DMB();
    g_live_clock.value.tim5_tick = tim5_tick;
    g_live_clock.value.audio_sample = audio_sample;
    for (brick_entity_id_t entity_id = 0U;
         entity_id < (brick_entity_id_t)BRICK_ENTITY_CAPACITY;
         ++entity_id)
    {
        g_live_clock.value.binding_generation[entity_id] =
            audio_note_engine_adapter_installed_generation(entity_id);
    }
    __DMB();
    g_live_clock.valid = 1U;
    ++g_live_clock.sequence;
    __DMB();
}

bool live_clock_read_anchor(live_clock_anchor_t *out_anchor)
{
    if (out_anchor == NULL)
    {
        return false;
    }

    for (uint8_t attempt = 0U; attempt < 2U; ++attempt)
    {
        const uint32_t before = g_live_clock.sequence;
        __DMB();
        if ((before & 1U) != 0U)
            continue;
        const bool valid = (g_live_clock.valid != 0U);
        live_clock_anchor_t candidate;
        if (valid)
            candidate = g_live_clock.value;
        __DMB();
        if (before == g_live_clock.sequence)
        {
            if (valid)
                *out_anchor = candidate;
            return valid;
        }
    }

    /* The caller keeps its last coherent anchor and retries on the next
     * control/audio scheduling opportunity. */
    return false;
}

bool live_clock_read_audio_sample(uint64_t *out_audio_sample)
{
    if (out_audio_sample == NULL)
        return false;
    live_clock_anchor_t anchor;
    if (!live_clock_read_anchor(&anchor))
        return false;
    *out_audio_sample = anchor.audio_sample;
    return true;
}

uint64_t live_clock_audio_sample(void)
{
    uint64_t sample = 0U;
    (void)live_clock_read_audio_sample(&sample);
    return sample;
}

bool live_clock_read_binding_generation(brick_entity_id_t entity_id,
                                        uint32_t *out_generation)
{
    if ((out_generation == NULL) || (entity_id >= BRICK_ENTITY_CAPACITY))
        return false;
    live_clock_anchor_t anchor;
    if (!live_clock_read_anchor(&anchor))
        return false;
    *out_generation = anchor.binding_generation[entity_id];
    return true;
}

bool live_clock_tim5_to_sample_time(uint32_t capture_tick,
                                    uint64_t *out_sample_time)
{
    if ((out_sample_time == NULL) || (g_samples_per_tim5_tick_q32 == 0ULL))
    {
        return false;
    }

    live_clock_anchor_t anchor;
    if (!live_clock_read_anchor(&anchor))
    {
        return false;
    }

    /* Signed modulo arithmetic handles captures immediately before or after
     * the latest audio boundary, including a TIM5 wrap. */
    const int32_t delta_ticks = (int32_t)(capture_tick - anchor.tim5_tick);
    const int64_t delta_q32 = (int64_t)delta_ticks
                            * (int64_t)g_samples_per_tim5_tick_q32;
    const int64_t delta_samples = live_clock_round_q32(delta_q32);

    *out_sample_time = (delta_samples >= 0)
        ? anchor.audio_sample + (uint64_t)delta_samples
        : anchor.audio_sample - (uint64_t)(-delta_samples);
    return true;
}

bool live_clock_tim5_to_guarded_sample_time(uint32_t capture_tick,
                                            uint64_t *out_sample_time)
{
    uint64_t sample_time = 0ULL;
    if (!live_clock_tim5_to_sample_time(capture_tick, &sample_time))
    {
        return false;
    }

    if (sample_time > (UINT64_MAX - (uint64_t)LIVE_GUARD_SAMPLES))
    {
        sample_time = UINT64_MAX;
    }
    else
    {
        sample_time += (uint64_t)LIVE_GUARD_SAMPLES;
    }

    *out_sample_time = sample_time;
    return true;
}

uint32_t live_clock_capture_tick(void)
{
    return TIM5->CNT;
}

uint32_t live_clock_get_tim5_hz(void)
{
    return g_tim5_hz;
}
