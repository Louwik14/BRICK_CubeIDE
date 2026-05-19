#ifndef HALL_LOOP_H
#define HALL_LOOP_H

#include <stdint.h>

#define HALL_LOOP_MAX_SAMPLES_PER_POLL 32U

typedef struct
{
    volatile uint32_t calls;
    volatile uint32_t last_cycles;
    volatile uint32_t max_cycles;
    volatile uint32_t last_samples;
    volatile uint32_t max_samples;
    volatile uint32_t last_backlog_samples;
    volatile uint32_t max_backlog_samples;
    volatile uint32_t cap_hit_count;
} hall_loop_metrics_t;

extern volatile hall_loop_metrics_t g_hall_loop_metrics;

void hall_loop_init(void);
void hall_loop_process(void);

#endif
