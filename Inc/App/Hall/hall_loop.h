#ifndef HALL_LOOP_H
#define HALL_LOOP_H

#include <stdint.h>

#define HALL_LOOP_MAX_SAMPLES_PER_POLL 32U

void hall_loop_init(void);
void hall_loop_process(void);

#endif
