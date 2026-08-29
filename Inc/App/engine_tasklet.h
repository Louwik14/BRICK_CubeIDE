#ifndef ENGINE_TASKLET_H
#define ENGINE_TASKLET_H

#include <stdint.h>

/**
 * @file engine_tasklet.h
 * @brief Interface du scheduler tasklet synchronisé sur le flux audio.
 *
 * Rôle du module:
 * - Deriver les ticks engine de TIM5 dans la main loop CONTROL.
 */

extern volatile uint32_t engine_tick_count;

#define ENGINE_TASKLET_MAX_TICKS_PER_POLL 8U

void engine_tasklet_init(uint32_t sample_rate);
void engine_tasklet_poll(void);

#endif /* ENGINE_TASKLET_H */
