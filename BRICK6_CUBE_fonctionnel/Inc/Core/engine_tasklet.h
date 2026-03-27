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
extern volatile uint32_t engine_audio_frame_count;

void engine_tasklet_init(uint32_t sample_rate);
void engine_tasklet_notify_frames(uint32_t frames);
void engine_tasklet_poll(void);

#endif /* ENGINE_TASKLET_H */
