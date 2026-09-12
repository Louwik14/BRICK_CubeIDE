#include "Platform/brick_media_clock.h"

#include "stm32h7xx_hal.h"

typedef struct
{
    volatile uint32_t sequence;
    volatile uint32_t wrap_count;
    volatile uint32_t tick_hz;
} brick_media_clock_shared_state_t;

#if defined(BRICK_MEDIA_CLOCK_SHARED_STATE_ADDRESS)
#define BRICK_MEDIA_CLOCK_STATE \
    (*(brick_media_clock_shared_state_t *)(uintptr_t)BRICK_MEDIA_CLOCK_SHARED_STATE_ADDRESS)
#else
static brick_media_clock_shared_state_t g_media_clock_state;
#define BRICK_MEDIA_CLOCK_STATE g_media_clock_state
#endif

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

static uint8_t brick_media_clock_snapshot(uint32_t *out_tick,
                                          uint64_t *out_extended)
{
    if ((out_tick == NULL) || (out_extended == NULL)) return 0U;

    uint32_t sequence_before;
    uint32_t sequence_after;
    uint32_t wraps;
    uint32_t tick_before;
    uint32_t tick_after;
    uint32_t status;
    do
    {
        sequence_before = BRICK_MEDIA_CLOCK_STATE.sequence;
        __DMB();
        wraps = BRICK_MEDIA_CLOCK_STATE.wrap_count;
        tick_before = TIM5->CNT;
        status = TIM5->SR;
        tick_after = TIM5->CNT;
        __DMB();
        sequence_after = BRICK_MEDIA_CLOCK_STATE.sequence;
    } while (((sequence_before & 1U) != 0U)
             || (sequence_before != sequence_after));

    /* If UPDATE is pending, the sole IRQ owner has not published this wrap
     * yet.  Project it for this read without modifying the shared state. */
    if (((status & TIM_SR_UIF) != 0U) || (tick_after < tick_before))
    {
        ++wraps;
    }
    *out_tick = tick_after;
    *out_extended = ((uint64_t)wraps << 32) | tick_after;
    return 1U;
}

static uint64_t brick_media_clock_extended_tick_to_sample(uint64_t ticks)
{
    const uint32_t tick_hz = BRICK_MEDIA_CLOCK_STATE.tick_hz;
    const uint64_t whole = ticks / tick_hz;
    const uint64_t remainder = ticks % tick_hz;
    return whole * BOARD_AUDIO_SAMPLE_RATE_HZ
        + (remainder * BOARD_AUDIO_SAMPLE_RATE_HZ) / tick_hz;
}

void brick_media_clock_init(void)
{
    const uint32_t tick_hz = brick_media_clock_tim5_frequency();
    const uint32_t primask = brick_media_clock_enter_critical();

    BRICK_MEDIA_CLOCK_STATE.sequence = 1U;
    __DMB();
    BRICK_MEDIA_CLOCK_STATE.wrap_count = 0U;
    BRICK_MEDIA_CLOCK_STATE.tick_hz = tick_hz;
    TIM5->SR &= ~TIM_SR_UIF;
    TIM5->DIER |= TIM_DIER_UIE;
    __DMB();
    BRICK_MEDIA_CLOCK_STATE.sequence = 2U;

    brick_media_clock_exit_critical(primask);
}

void brick_media_clock_on_tim5_update_irq(void)
{
    /* TIM5 is lower priority than AUDIO.  Keep the seqlock write
     * non-preemptible so a higher-priority reader can never spin on this
     * core while the writer is suspended. */
    const uint32_t primask = brick_media_clock_enter_critical();
    BRICK_MEDIA_CLOCK_STATE.sequence++;
    __DMB();
    BRICK_MEDIA_CLOCK_STATE.wrap_count++;
    TIM5->SR &= ~TIM_SR_UIF;
    __DMB();
    BRICK_MEDIA_CLOCK_STATE.sequence++;
    brick_media_clock_exit_critical(primask);
}

uint32_t brick_media_clock_now_tick(void)
{
    return TIM5->CNT;
}

bool brick_media_clock_tick_to_sample(uint32_t capture_tick,
                                      uint64_t *out_sample_time)
{
    if ((out_sample_time == NULL) || (BRICK_MEDIA_CLOCK_STATE.tick_hz == 0U))
    {
        return false;
    }

    uint32_t now_tick;
    uint64_t now_extended;
    if (brick_media_clock_snapshot(&now_tick, &now_extended) == 0U)
    {
        return false;
    }
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
    if ((out_sample_time == NULL) || (BRICK_MEDIA_CLOCK_STATE.tick_hz == 0U))
    {
        return false;
    }
    uint32_t now_tick;
    uint64_t now_extended;
    if (brick_media_clock_snapshot(&now_tick, &now_extended) == 0U)
    {
        return false;
    }
    (void)now_tick;
    *out_sample_time = brick_media_clock_extended_tick_to_sample(now_extended);
    return true;
}

uint32_t brick_media_clock_tick_hz(void)
{
    return BRICK_MEDIA_CLOCK_STATE.tick_hz;
}
