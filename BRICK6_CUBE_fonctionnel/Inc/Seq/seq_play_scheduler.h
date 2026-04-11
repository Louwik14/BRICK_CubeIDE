#ifndef SEQ_PLAY_SCHEDULER_H
#define SEQ_PLAY_SCHEDULER_H

#if !defined(SEQ_RUNTIME_INTERNAL_USE) && !defined(SEQ_PLAY_SCHEDULER_IMPLEMENTATION)
#error "seq_play_scheduler is internal to the sequencer time-domain runtime."
#endif

#include <stdint.h>

#include "Seq/seq_types.h"

void seq_play_scheduler_init(void);
void seq_play_scheduler_clear(void);
void seq_play_scheduler_schedule_step(seq_track_id_t track,
                                      seq_step_id_t step,
                                      uint16_t ticks_per_step,
                                      uint32_t step_tick);
void seq_play_scheduler_service(uint32_t now_tick, uint8_t running);
void seq_play_scheduler_audio_consume_block_start(void);

#endif /* SEQ_PLAY_SCHEDULER_H */
