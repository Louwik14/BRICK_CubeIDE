#include "Platform/brick_media_clock.h"

#include "stm32h7xx_hal.h"

static uint32_t g_media_clock_tick_hz;
static uint32_t g_media_clock_last_tick;
static uint64_t g_media_clock_extended_ticks;

static uint32_t brick_media_clock_tim5_frequency(void)
{
    uint32_t tim_kernel_hz = HAL_RCC_GetPCLK1Freq();
    const uint32_t apb1_prescaler = RCC->D2CFGR & RCC_D2CFGR_D2PPRE1;

    if (apb1_prescaler != RCC_APB1_DIV1)
    {
        tim_kernel_hz *= 2U;
    }

    const uint32_t prescaler = (uint32_t)TIM5->PSC + 1U;
    return (prescaler != 0U) ? (tim_kernel_hz / prescaler) : 0U;
}

static uint32_t brick_media_clock_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return primask;
}

static void brick_media_clock_exit_critical(uint32_t primask)
{
    __DMB();
    __set_PRIMASK(primask);
}

static uint64_t brick_media_clock_extend_now(uint32_t now_tick)
{
    const uint32_t primask = brick_media_clock_enter_critical();
    g_media_clock_extended_ticks +=
        (uint32_t)(now_tick - g_media_clock_last_tick);
    g_media_clock_last_tick = now_tick;
    const uint64_t extended = g_media_clock_extended_ticks;
    brick_media_clock_exit_critical(primask);
    return extended;
}

static uint64_t brick_media_clock_extended_tick_to_sample(uint64_t ticks)
{
    const uint64_t whole = ticks / g_media_clock_tick_hz;
    const uint64_t remainder = ticks % g_media_clock_tick_hz;
    return whole * BOARD_AUDIO_SAMPLE_RATE_HZ
        + (remainder * BOARD_AUDIO_SAMPLE_RATE_HZ) / g_media_clock_tick_hz;
}

void brick_media_clock_init(void)
{
    const uint32_t tick_hz = brick_media_clock_tim5_frequency();
    const uint32_t now_tick = TIM5->CNT;
    const uint32_t primask = brick_media_clock_enter_critical();

    g_media_clock_tick_hz = tick_hz;
    g_media_clock_last_tick = now_tick;
    g_media_clock_extended_ticks = now_tick;

    brick_media_clock_exit_critical(primask);
}

uint32_t brick_media_clock_now_tick(void)
{
    return TIM5->CNT;
}

bool brick_media_clock_tick_to_sample(uint32_t capture_tick,
                                      uint64_t *out_sample_time)
{
    if ((out_sample_time == NULL) || (g_media_clock_tick_hz == 0U))
    {
        return false;
    }

    const uint32_t now_tick = TIM5->CNT;
    const uint64_t now_extended = brick_media_clock_extend_now(now_tick);
    const int32_t capture_from_now = (int32_t)(capture_tick - now_tick);
    uint64_t capture_extended;
    if (capture_from_now < 0)
    {
        const uint32_t age = (uint32_t)(-((int64_t)capture_from_now));
        if (now_extended < age)
        {
            return false;
        }
        capture_extended = now_extended - age;
    }
    else
    {
        capture_extended = now_extended + (uint32_t)capture_from_now;
    }

    *out_sample_time =
        brick_media_clock_extended_tick_to_sample(capture_extended);
    return true;
}

bool brick_media_clock_tick_to_guarded_sample(uint32_t capture_tick,
                                              uint64_t *out_sample_time)
{
    uint64_t sample_time;
    if ((out_sample_time == NULL)
            || !brick_media_clock_tick_to_sample(capture_tick, &sample_time))
    {
        return false;
    }

    if (sample_time > UINT64_MAX - BRICK_MEDIA_CLOCK_GUARD_SAMPLES)
    {
        return false;
    }
    *out_sample_time = sample_time + BRICK_MEDIA_CLOCK_GUARD_SAMPLES;
    return true;
}

bool brick_media_clock_now_sample(uint64_t *out_sample_time)
{
    return brick_media_clock_tick_to_sample(TIM5->CNT, out_sample_time);
}

uint32_t brick_media_clock_tick_hz(void)
{
    return g_media_clock_tick_hz;
}
