#include "IPC/live_clock.h"

#include "Board/board_audio_format.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    live_clock_anchor_t value;
    volatile uint32_t sequence;
    volatile uint8_t valid;
} live_clock_state_t;

D3_IPC static live_clock_state_t g_live_clock;
static uint32_t g_tim5_hz;
static uint32_t g_tim5_origin_tick;
static uint32_t g_tim5_last_tick;
static uint64_t g_tim5_extended_ticks;

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

static int64_t live_clock_ticks_to_samples(int64_t ticks)
{
    if (g_tim5_hz == 0U) return 0;
    const int64_t whole = ticks / (int64_t)g_tim5_hz;
    const int64_t remainder = ticks % (int64_t)g_tim5_hz;
    const int64_t rounded = (remainder >= 0)
        ? ((remainder * BOARD_AUDIO_SAMPLE_RATE_HZ + (g_tim5_hz / 2U))
           / g_tim5_hz)
        : -(((-remainder * BOARD_AUDIO_SAMPLE_RATE_HZ) + (g_tim5_hz / 2U))
            / g_tim5_hz);
    return whole * BOARD_AUDIO_SAMPLE_RATE_HZ + rounded;
}

static uint64_t live_clock_extend_tim5_now(uint32_t now_tick)
{
    const uint32_t primask = live_clock_enter_critical();
    g_tim5_extended_ticks += (uint32_t)(now_tick - g_tim5_last_tick);
    g_tim5_last_tick = now_tick;
    const uint64_t extended = g_tim5_extended_ticks;
    live_clock_exit_critical(primask);
    return extended;
}

void live_clock_init(void)
{
    const uint32_t tim5_hz = live_clock_tim5_frequency();
    const uint32_t primask = live_clock_enter_critical();

    g_tim5_hz = tim5_hz;
    g_live_clock.value = (live_clock_anchor_t){0};
    g_live_clock.sequence = 0U;
    g_live_clock.valid = 0U;
    g_tim5_origin_tick = TIM5->CNT;
    g_tim5_last_tick = g_tim5_origin_tick;
    g_tim5_extended_ticks = 0U;

    live_clock_exit_critical(primask);
}

void live_clock_audio_publish_anchor(uint64_t audio_sample)
{
    /* TIM5 and SAI share the HSE-derived clock tree.  The nominal contract is
     * therefore one boot phase anchor; later IRQs only ensure it exists. */
    if (g_live_clock.valid != 0U)
        return;
    const uint32_t tim5_tick = TIM5->CNT;
    ++g_live_clock.sequence;
    __DMB();
    g_live_clock.value.tim5_tick = tim5_tick;
    g_live_clock.value.audio_sample = audio_sample;
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
    return live_clock_tim5_to_sample_time(TIM5->CNT, out_audio_sample);
}

uint64_t live_clock_audio_sample(void)
{
    uint64_t sample = 0U;
    (void)live_clock_read_audio_sample(&sample);
    return sample;
}

bool live_clock_tim5_to_sample_time(uint32_t capture_tick,
                                    uint64_t *out_sample_time)
{
    if ((out_sample_time == NULL) || (g_tim5_hz == 0U))
    {
        return false;
    }

    live_clock_anchor_t anchor;
    if (!live_clock_read_anchor(&anchor))
    {
        return false;
    }

    /* CONTROL extends TIM5 monotonically; capture_tick only needs to be near
     * the current tick. This keeps a single boot anchor valid across wraps. */
    const uint32_t now_tick = TIM5->CNT;
    const uint64_t now_extended = live_clock_extend_tim5_now(now_tick);
    const int32_t capture_from_now = (int32_t)(capture_tick - now_tick);
    const uint64_t anchor_extended = (uint32_t)(anchor.tim5_tick
                                                - g_tim5_origin_tick);
    const int64_t delta_ticks = (int64_t)now_extended
                              + (int64_t)capture_from_now
                              - (int64_t)anchor_extended;
    const int64_t delta_samples = live_clock_ticks_to_samples(delta_ticks);

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
