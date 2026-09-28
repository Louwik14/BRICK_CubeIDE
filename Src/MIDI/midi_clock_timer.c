#include "MIDI/midi_clock_timer.h"

#include "Platform/brick_media_clock.h"
#include "midi.h"
#include "stm32h7xx.h"

#include <limits.h>

#define MIDI_CLOCK_EVENTS 16U
#define MIDI_CLOCK_COMPARE_HORIZON 60000U
#define MIDI_CLOCK_COMPARE_GUARD 8U

typedef struct {
    uint32_t tim5_tick;
    uint32_t target_frame;
    uint32_t target_tick;
} midi_clock_event_t;
static midi_clock_event_t g_events[MIDI_CLOCK_EVENTS];
static volatile uint32_t g_head;
static volatile uint32_t g_tail;
static volatile uint64_t g_due_q16;
static volatile uint32_t g_due_tim5;
static volatile uint32_t g_compare_due_tim5;
static volatile uint32_t g_period_q16;
static volatile uint32_t g_period_remainder;
static volatile uint32_t g_phase_remainder;
static volatile uint32_t g_pending_period_q16;
static volatile uint32_t g_pending_period_remainder;
static volatile uint8_t g_armed;
static volatile uint32_t g_sof_frame;
static volatile uint32_t g_sof_tick;
static volatile uint8_t g_sof_valid;

volatile midi_clock_prof_t g_midi_clock_prof __attribute__((used));

static uint32_t midi_clock_timer_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static uint64_t midi_clock_period_ticks_numerator(uint32_t sample_period_q16)
{
    /* TIM3 and TIM5 are both 1 MHz; one 48 kHz sample is 125/6 us. */
    return (uint64_t)sample_period_q16 * 125U;
}

static void midi_clock_timer_advance(uint32_t periods)
{
    const uint64_t remainder = (uint64_t)g_phase_remainder
        + (uint64_t)periods * g_period_remainder;
    g_due_q16 += (uint64_t)periods * g_period_q16 + remainder / 6U;
    g_phase_remainder = (uint32_t)(remainder % 6U);
    g_due_tim5 = (uint32_t)(g_due_q16 >> 16);
}

static void midi_clock_timer_program(uint32_t now5, uint16_t now3)
{
    int32_t until = (int32_t)(g_due_tim5 - now5);
    uint32_t delay = (until > 0) ? (uint32_t)until : MIDI_CLOCK_COMPARE_GUARD;
    if (delay > MIDI_CLOCK_COMPARE_HORIZON) delay = MIDI_CLOCK_COMPARE_HORIZON;
    if (delay < MIDI_CLOCK_COMPARE_GUARD) delay = MIDI_CLOCK_COMPARE_GUARD;
    const uint16_t compare = (uint16_t)(now3 + delay);
    g_compare_due_tim5 = now5 + delay;
    TIM3->CCR1 = compare;
    g_midi_clock_prof.next_compare = compare;
}

void midi_clock_timer_init(void)
{
    const uint32_t primask = midi_clock_timer_critical();
    __HAL_RCC_TIM3_CLK_ENABLE();
    TIM3->CR1 = 0U;
    TIM3->DIER = 0U;
    TIM3->PSC = 239U;
    TIM3->ARR = 0xFFFFU;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;
    TIM3->CNT = 0U;
    TIM3->CR1 = TIM_CR1_CEN;
    g_head = g_tail = 0U;
    g_armed = 0U;
    g_sof_frame = 0U;
    g_sof_tick = 0U;
    g_sof_valid = 0U;
    g_period_q16 = 0U;
    g_period_remainder = 0U;
    g_phase_remainder = 0U;
    g_pending_period_q16 = 0U;
    g_pending_period_remainder = 0U;
    volatile uint32_t *const words = (volatile uint32_t *)&g_midi_clock_prof;
    for (uint32_t i = 0U; i < sizeof(g_midi_clock_prof) / sizeof(uint32_t); ++i)
        words[i] = 0U;
    g_midi_clock_prof.irq_late_min = UINT32_MAX;
    g_midi_clock_prof.irq_cycles_min = UINT32_MAX;
    g_midi_clock_prof.usb_ready_delay_min = UINT32_MAX;
    g_midi_clock_prof.f8_irq_cycles_min = UINT32_MAX;
    g_midi_clock_prof.publish_delay_min = UINT32_MAX;
    g_midi_clock_prof.usb_submit_delay_min = UINT32_MAX;
    g_midi_clock_prof.usb_complete_delay_min = UINT32_MAX;
    g_midi_clock_prof.usb_submit_to_complete_min = UINT32_MAX;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    NVIC_SetPriority(TIM3_IRQn, 0U);
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(TIM3_IRQn);
    __set_PRIMASK(primask);
}

void midi_clock_timer_set_period(uint32_t sample_period_q16)
{
    if (sample_period_q16 == 0U) return;
    const uint64_t numerator = midi_clock_period_ticks_numerator(sample_period_q16);
    const uint32_t period = (uint32_t)(numerator / 6U);
    const uint32_t primask = midi_clock_timer_critical();
    /* A running transport finishes its current interval before taking tempo. */
    g_pending_period_q16 = period;
    g_pending_period_remainder = (uint32_t)(numerator % 6U);
    if (g_armed == 0U)
    {
        g_period_q16 = period;
        g_period_remainder = g_pending_period_remainder;
    }
    __set_PRIMASK(primask);
}

void midi_clock_timer_arm(uint64_t start_sample, uint32_t sample_period_q16)
{
    if (sample_period_q16 == 0U) return;
    uint64_t now_sample_q16;
    uint32_t capture_tick;
    if (!brick_media_clock_now_sample_q16(&now_sample_q16, &capture_tick)) return;
    const uint64_t numerator = midi_clock_period_ticks_numerator(sample_period_q16);
    const uint32_t period = (uint32_t)(numerator / 6U);
    const uint64_t period_sample = sample_period_q16;
    uint64_t first_sample_q16 = (start_sample << 16) + period_sample;
    if (first_sample_q16 <= now_sample_q16)
    {
        const uint64_t skipped = (now_sample_q16 - first_sample_q16) / period_sample + 1U;
        first_sample_q16 += skipped * period_sample;
        g_midi_clock_prof.missed_ticks += (uint32_t)skipped;
    }
    const uint64_t delay_q16 = ((first_sample_q16 - now_sample_q16) * 125U) / 6U;
    const uint32_t primask = midi_clock_timer_critical();
    TIM3->DIER &= ~TIM_DIER_CC1IE;
    g_armed = 0U;
    g_tail = g_head;
    g_period_q16 = period;
    g_period_remainder = (uint32_t)(numerator % 6U);
    g_phase_remainder = 0U;
    g_pending_period_q16 = 0U;
    g_pending_period_remainder = 0U;
    const uint32_t now5 = TIM5->CNT;
    const uint16_t now3 = (uint16_t)TIM3->CNT;
    const uint64_t elapsed_q16 = (uint64_t)(now5 - capture_tick) << 16;
    g_due_q16 = ((uint64_t)now5 << 16)
        + ((delay_q16 > elapsed_q16) ? (delay_q16 - elapsed_q16) : 0U);
    g_due_tim5 = (uint32_t)(g_due_q16 >> 16);
    midi_clock_timer_program(now5, now3);
    TIM3->SR = (uint16_t)~TIM_SR_CC1IF;
    g_armed = 1U;
    g_midi_clock_prof.armed = 1U;
    TIM3->DIER |= TIM_DIER_CC1IE;
    __set_PRIMASK(primask);
}

void midi_clock_timer_stop(void)
{
    const uint32_t primask = midi_clock_timer_critical();
    TIM3->DIER &= ~TIM_DIER_CC1IE;
    TIM3->SR = (uint16_t)~TIM_SR_CC1IF;
    g_armed = 0U;
    g_midi_clock_prof.armed = 0U;
    g_tail = g_head;
    __set_PRIMASK(primask);
}

void TIM3_IRQHandler(void)
{
    const uint32_t cycle_start = DWT->CYCCNT;
    if ((TIM3->SR & TIM_SR_CC1IF) == 0U) return;
    TIM3->SR = (uint16_t)~TIM_SR_CC1IF;
    const uint32_t now5 = TIM5->CNT;
    const uint16_t now3 = (uint16_t)TIM3->CNT;
    volatile midi_clock_prof_t *const prof = &g_midi_clock_prof;
    ++prof->irq_count;
    prof->last_counter = now3;
    uint32_t late = (uint16_t)(now3 - (uint16_t)prof->next_compare);
    const int32_t long_late = (int32_t)(now5 - g_compare_due_tim5);
    if (long_late > 65535) late = (uint32_t)long_late;
    prof->irq_late_last = late;
    if (late < prof->irq_late_min) prof->irq_late_min = late;
    if (late > prof->irq_late_max) prof->irq_late_max = late;
    prof->irq_late_total += late;
    prof->last_missed = 0U;

    if ((g_armed != 0U) && ((int32_t)(now5 - g_due_tim5) >= 0))
    {
        const uint32_t head = g_head;
        if (head - g_tail < MIDI_CLOCK_EVENTS)
        {
            midi_clock_event_t *const event = &g_events[head & (MIDI_CLOCK_EVENTS - 1U)];
            event->tim5_tick = g_due_tim5;
            if ((g_sof_valid != 0U) && ((uint32_t)(now5 - g_sof_tick) < 2000U))
            {
                const int32_t until = (int32_t)(g_due_tim5 - g_sof_tick);
                const uint32_t frames = (until > 0) ? ((uint32_t)until + 999U) / 1000U : 0U;
                event->target_frame = g_sof_frame + frames;
                event->target_tick = g_sof_tick + frames * 1000U;
            }
            else
            {
                event->target_frame = g_sof_frame + 1U;
                event->target_tick = g_due_tim5;
            }
            __DMB();
            g_head = head + 1U;
            ++prof->f8_published;
            prof->last_publish_tick = now5;
            const uint32_t f8_cycles = DWT->CYCCNT - cycle_start;
            prof->f8_irq_cycles_last = f8_cycles;
            if (f8_cycles < prof->f8_irq_cycles_min) prof->f8_irq_cycles_min = f8_cycles;
            if (f8_cycles > prof->f8_irq_cycles_max) prof->f8_irq_cycles_max = f8_cycles;
        }
        else ++prof->event_drops;

        if (g_pending_period_q16 != 0U)
        {
            g_period_q16 = g_pending_period_q16;
            g_period_remainder = g_pending_period_remainder;
            g_pending_period_q16 = 0U;
        }
        midi_clock_timer_advance(1U);
        if ((int32_t)(now5 - g_due_tim5) >= 0)
        {
            const uint32_t overdue = (uint32_t)(now5 - g_due_tim5);
            const uint32_t skipped = (uint32_t)
                ((((uint64_t)overdue << 16) / g_period_q16) + 1U);
            midi_clock_timer_advance(skipped);
            prof->missed_ticks += skipped;
            prof->last_missed = skipped;
            ++prof->late_multi_count;
            if ((int32_t)(now5 - g_due_tim5) >= 0)
            {
                midi_clock_timer_advance(1U);
                ++prof->missed_ticks;
                ++prof->last_missed;
            }
        }
        /* Never arm a compare that has already passed during this IRQ. */
        if ((int32_t)(g_due_tim5 - TIM5->CNT) <= MIDI_CLOCK_COMPARE_GUARD)
        {
            midi_clock_timer_advance(1U);
            ++prof->missed_ticks;
            ++prof->last_missed;
            ++prof->late_multi_count;
        }
    }
    if (g_armed != 0U) midi_clock_timer_program(TIM5->CNT, (uint16_t)TIM3->CNT);
    prof->period_ticks = g_period_q16 >> 16;
    prof->period_fraction_q16 = g_period_q16 & 0xFFFFU;
    prof->fraction_accum_q16 = (uint32_t)g_due_q16 & 0xFFFFU;
    prof->period_remainder_sixth = g_period_remainder;
    prof->phase_remainder_sixth = g_phase_remainder;
    const uint32_t elapsed = DWT->CYCCNT - cycle_start;
    prof->irq_cycles_last = elapsed;
    if (elapsed < prof->irq_cycles_min) prof->irq_cycles_min = elapsed;
    if (elapsed > prof->irq_cycles_max) prof->irq_cycles_max = elapsed;
}

void midi_clock_timer_on_sof(uint32_t sof_tick)
{
    g_sof_valid = 0U;
    g_sof_tick = sof_tick;
    ++g_sof_frame;
    g_sof_valid = 1U;
    const uint32_t head = g_head;
    if (g_tail == head) return;
    if (head - g_tail > 1U)
    {
        g_midi_clock_prof.stale_drops += head - g_tail - 1U;
        g_tail = head - 1U;
    }

    const midi_clock_event_t event = g_events[g_tail & (MIDI_CLOCK_EVENTS - 1U)];
    if ((int32_t)(g_sof_frame - event.target_frame) < 0) return;
    ++g_tail; /* At most one F8 per SOF; never catch up with a burst. */
    volatile midi_clock_prof_t *const prof = &g_midi_clock_prof;
    const uint32_t target_error = (uint32_t)((int32_t)(event.target_tick - event.tim5_tick) < 0
        ? event.tim5_tick - event.target_tick : event.target_tick - event.tim5_tick);
    if (target_error > prof->sof_target_error_max) prof->sof_target_error_max = target_error;
    const uint32_t release_tick = TIM5->CNT;
    const uint32_t release_error = release_tick - event.tim5_tick;
    if (release_error > prof->sof_release_error_max) prof->sof_release_error_max = release_error;
    const uint32_t late_frames = g_sof_frame - event.target_frame;
    if (late_frames != 0U)
    {
        ++prof->sof_late_count;
        if (late_frames > prof->sof_late_frames_max) prof->sof_late_frames_max = late_frames;
    }
    prof->last_consume_tick = release_tick;
    const uint8_t publish_result = midi_clock_irq_publish(event.tim5_tick, midi_clock_get_destination());
    if (publish_result != 0U)
    {
        ++prof->f8_consumed;
        if (publish_result == 1U)
        {
            ++prof->backlog;
            if (prof->backlog > prof->backlog_max) prof->backlog_max = prof->backlog;
        }
    }
    else ++prof->usb_fifo_drops;
    const uint32_t publish_delay = TIM5->CNT - event.tim5_tick;
    prof->publish_delay_last = publish_delay;
    if (publish_delay < prof->publish_delay_min) prof->publish_delay_min = publish_delay;
    if (publish_delay > prof->publish_delay_max) prof->publish_delay_max = publish_delay;
}

void midi_clock_timer_note_usb_ready(uint32_t publish_tick)
{
    const uint32_t delay = TIM5->CNT - publish_tick;
    if (g_midi_clock_prof.backlog != 0U) --g_midi_clock_prof.backlog;
    g_midi_clock_prof.usb_ready_delay_last = delay;
    if (delay < g_midi_clock_prof.usb_ready_delay_min)
        g_midi_clock_prof.usb_ready_delay_min = delay;
    if (delay > g_midi_clock_prof.usb_ready_delay_max)
        g_midi_clock_prof.usb_ready_delay_max = delay;
    if (delay > g_midi_clock_prof.consume_delay_max)
        g_midi_clock_prof.consume_delay_max = delay;
}

void midi_clock_timer_note_usb_drop(void)
{
    ++g_midi_clock_prof.usb_fifo_drops;
}

void midi_clock_timer_note_usb_submit(uint32_t deadline_tick, uint32_t submit_tick)
{
    const uint32_t delay = submit_tick - deadline_tick;
    g_midi_clock_prof.usb_submit_delay_last = delay;
    if (delay < g_midi_clock_prof.usb_submit_delay_min)
        g_midi_clock_prof.usb_submit_delay_min = delay;
    if (delay > g_midi_clock_prof.usb_submit_delay_max)
        g_midi_clock_prof.usb_submit_delay_max = delay;
    ++g_midi_clock_prof.f8_usb_submitted;
}

void midi_clock_timer_note_usb_complete(uint32_t deadline_tick, uint32_t submit_tick,
                                        uint32_t complete_tick)
{
    const uint32_t delay = complete_tick - deadline_tick;
    const uint32_t transfer = complete_tick - submit_tick;
    g_midi_clock_prof.usb_complete_delay_last = delay;
    if (delay < g_midi_clock_prof.usb_complete_delay_min)
        g_midi_clock_prof.usb_complete_delay_min = delay;
    if (delay > g_midi_clock_prof.usb_complete_delay_max)
        g_midi_clock_prof.usb_complete_delay_max = delay;
    g_midi_clock_prof.usb_submit_to_complete_last = transfer;
    if (transfer < g_midi_clock_prof.usb_submit_to_complete_min)
        g_midi_clock_prof.usb_submit_to_complete_min = transfer;
    if (transfer > g_midi_clock_prof.usb_submit_to_complete_max)
        g_midi_clock_prof.usb_submit_to_complete_max = transfer;
    ++g_midi_clock_prof.f8_usb_completed;
}
