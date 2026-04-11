#ifndef SEQ_PLAY_SCHEDULER_H
#define SEQ_PLAY_SCHEDULER_H

#if !defined(SEQ_RUNTIME_INTERNAL_USE) && !defined(SEQ_PLAY_SCHEDULER_IMPLEMENTATION)
#error "seq_play_scheduler is internal to the sequencer time-domain runtime."
#endif

#include <stdint.h>

#include "Seq/seq_types.h"

typedef struct
{
    uint8_t type;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint16_t sample_offset_in_block;
} seq_play_scheduler_audio_event_t;

void seq_play_scheduler_init(void);
void seq_play_scheduler_clear(void);
void seq_play_scheduler_schedule_step(seq_track_id_t track,
                                      seq_step_id_t step,
                                      uint16_t ticks_per_step,
                                      uint32_t step_tick,
                                      uint64_t step_sample_time,
                                      uint32_t samples_per_step_q16);
void seq_play_scheduler_service(uint32_t now_tick, uint8_t running);
uint16_t seq_play_scheduler_audio_collect_block_events(seq_play_scheduler_audio_event_t *out_events,
                                                       uint16_t max_events,
                                                       uint16_t block_frames,
                                                       uint64_t block_start_sample);
void seq_play_scheduler_audio_apply_event(const seq_play_scheduler_audio_event_t *event);

#endif /* SEQ_PLAY_SCHEDULER_H */
