#include "Core/live_clock.h"

#include "Board/board_audio_format.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    live_clock_anchor_t value;
    volatile uint8_t valid;
} live_clock_state_t;

static live_clock_state_t g_live_clock;
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
    g_live_clock.value = (live_clock_anchor_t){0U, 0ULL};
    g_live_clock.valid = 0U;

    live_clock_exit_critical(primask);
}

void live_clock_audio_publish_anchor(uint64_t audio_sample)
{
    const uint32_t tim5_tick = TIM5->CNT;
    const uint32_t primask = live_clock_enter_critical();

    g_live_clock.value.tim5_tick = tim5_tick;
    g_live_clock.value.audio_sample = audio_sample;
    __DMB();
    g_live_clock.valid = 1U;

    live_clock_exit_critical(primask);
}

bool live_clock_read_anchor(live_clock_anchor_t *out_anchor)
{
    if (out_anchor == NULL)
    {
        return false;
    }

    const uint32_t primask = live_clock_enter_critical();
    const bool valid = (g_live_clock.valid != 0U);
    if (valid)
    {
        *out_anchor = g_live_clock.value;
    }
    live_clock_exit_critical(primask);
    return valid;
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

uint32_t live_clock_get_tim5_hz(void)
{
    return g_tim5_hz;
}
