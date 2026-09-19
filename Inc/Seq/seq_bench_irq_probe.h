#ifndef SEQ_BENCH_IRQ_PROBE_H
#define SEQ_BENCH_IRQ_PROBE_H

#include "stm32h7xx.h"
#include <stdint.h>

extern volatile uint32_t g_seq_bench_irq_window_active;
extern volatile uint32_t g_seq_bench_irq_depth;
extern volatile uint32_t g_seq_bench_irq_started;
extern volatile uint32_t g_seq_bench_irq_cycles;

static inline void seq_bench_irq_enter(void)
{
    if (g_seq_bench_irq_window_active != 0U) {
        if (g_seq_bench_irq_depth++ == 0U)
            g_seq_bench_irq_started = DWT->CYCCNT;
    }
}

static inline void seq_bench_irq_exit(void)
{
    if ((g_seq_bench_irq_window_active != 0U)
            && (g_seq_bench_irq_depth != 0U)) {
        if (--g_seq_bench_irq_depth == 0U)
            g_seq_bench_irq_cycles += DWT->CYCCNT - g_seq_bench_irq_started;
    }
}

#endif
