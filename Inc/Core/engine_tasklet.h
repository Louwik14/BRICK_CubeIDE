#ifndef ENGINE_TASKLET_H
#define ENGINE_TASKLET_H

#include <stdint.h>

/**
 * @file engine_tasklet.h
 * @brief Interface du scheduler tasklet synchronisé sur le flux audio.
 *
 * Rôle du module:
 * - Accumuler des frames notifiées depuis l'IRQ audio.
 * - Produire des ticks engine dans la main loop.
 */

extern volatile uint32_t engine_tick_count;

#define ENGINE_TASKLET_MAX_TICKS_PER_POLL 8U

typedef struct
{
    volatile uint32_t calls;
    volatile uint32_t last_cycles;
    volatile uint32_t max_cycles;
    volatile uint32_t last_tick_cycles;
    volatile uint32_t max_tick_cycles;
    volatile uint32_t last_buttons_cycles;
    volatile uint32_t max_buttons_cycles;
    volatile uint32_t last_encoders_cycles;
    volatile uint32_t max_encoders_cycles;
    volatile uint32_t last_led_cycles;
    volatile uint32_t max_led_cycles;
    volatile uint32_t last_mux_pots_cycles;
    volatile uint32_t max_mux_pots_cycles;
    volatile uint32_t last_ticks;
    volatile uint32_t max_ticks;
    volatile uint32_t last_backlog_ticks;
    volatile uint32_t max_backlog_ticks;
    volatile uint32_t cap_hit_count;
} engine_tasklet_metrics_t;

extern volatile engine_tasklet_metrics_t g_engine_tasklet_metrics;

void engine_tasklet_init(uint32_t sample_rate);
void engine_tasklet_notify_frames(uint32_t frames);
void engine_tasklet_poll(void);

#endif /* ENGINE_TASKLET_H */
