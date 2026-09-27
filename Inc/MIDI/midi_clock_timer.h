#ifndef MIDI_CLOCK_TIMER_H
#define MIDI_CLOCK_TIMER_H

#include <stdint.h>

/* All times are TIM3/TIM5 ticks (1 us nominal), except IRQ durations in CPU cycles. */
typedef struct {
    uint32_t irq_count;
    uint32_t f8_published;
    uint32_t f8_consumed;
    uint32_t missed_ticks;
    uint32_t late_multi_count;
    uint32_t event_drops;
    uint32_t stale_drops;
    uint32_t usb_fifo_drops;
    uint32_t irq_late_last;
    uint32_t irq_late_min;
    uint32_t irq_late_max;
    uint32_t irq_late_total;
    uint32_t irq_cycles_last;
    uint32_t irq_cycles_min;
    uint32_t irq_cycles_max;
    uint32_t armed;
    uint32_t next_compare;
    uint32_t last_counter;
    uint32_t period_ticks;
    uint32_t period_fraction_q16;
    uint32_t fraction_accum_q16;
    uint32_t last_missed;
    uint32_t backlog;
    uint32_t backlog_max;
    uint32_t consume_delay_max;
    uint32_t last_publish_tick;
    uint32_t last_consume_tick;
    uint32_t period_remainder_sixth;
    uint32_t phase_remainder_sixth;
} midi_clock_prof_t;

extern volatile midi_clock_prof_t g_midi_clock_prof;

void midi_clock_timer_init(void);
void midi_clock_timer_set_period(uint32_t sample_period_q16);
void midi_clock_timer_arm(uint64_t start_sample, uint32_t sample_period_q16);
void midi_clock_timer_stop(void);
void midi_clock_timer_poll(void);
void midi_clock_timer_note_usb_drop(void);

#endif
