#ifndef ENGINE_TASKLET_H
#define ENGINE_TASKLET_H

#include <stdint.h>
#include "brick6_refactor.h"

#if BRICK6_REFACTOR_STEP_3
extern volatile uint32_t engine_tick_count;

void engine_tasklet_init(uint32_t sample_rate);
void engine_tasklet_notify_frames(uint32_t frames);
void engine_tasklet_poll(void);
#endif

#endif /* ENGINE_TASKLET_H */
