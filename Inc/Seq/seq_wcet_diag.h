#ifndef SEQ_WCET_DIAG_H
#define SEQ_WCET_DIAG_H

#include <stdint.h>
#include "stm32h7xx.h"

typedef struct
{
    volatile uint32_t last_cycles;
    volatile uint32_t max_cycles;
    volatile uint32_t count;
} seq_wcet_metric_t;

typedef struct
{
    seq_wcet_metric_t plock_restore;
    seq_wcet_metric_t plock_apply;
    seq_wcet_metric_t play_scheduler;
    seq_wcet_metric_t note_fx;
    seq_wcet_metric_t note_admission;
    seq_wcet_metric_t publication;
    seq_wcet_metric_t full_pass;
} seq_wcet_diag_t;

extern volatile seq_wcet_diag_t g_seq_wcet_diag;

static inline uint32_t seq_wcet_begin(void)
{
    return DWT->CYCCNT;
}

static inline void seq_wcet_end(volatile seq_wcet_metric_t *metric,
                                uint32_t started)
{
    const uint32_t cycles = DWT->CYCCNT - started;
    metric->last_cycles = cycles;
    if (cycles > metric->max_cycles)
        metric->max_cycles = cycles;
    ++metric->count;
}

#endif /* SEQ_WCET_DIAG_H */
