#ifndef BRICK6_CONTROL_RT_SAMPLED_STATE_H
#define BRICK6_CONTROL_RT_SAMPLED_STATE_H

#include <stdint.h>

void control_rt_sampled_state_init(void);
void control_rt_sampled_state_process(uint32_t now_ms);
uint8_t control_rt_sampled_state_next_deadline(uint32_t now_ms,
                                                uint32_t *out_deadline_ms);

#endif
