#ifndef SEQ_LIVE_REC_CAPTURE_H
#define SEQ_LIVE_REC_CAPTURE_H

#include <stdint.h>

#include "Seq/seq_runtime.h"

void seq_live_rec_capture_init(void);
void seq_live_rec_capture_reset(void);
void seq_live_rec_capture_flush(uint32_t stop_tick, uint16_t ticks_per_step);
void seq_live_rec_capture_note_on(uint8_t active,
                                  const seq_runtime_state_t *runtime_state,
                                  seq_live_rec_source_t source,
                                  uint8_t channel_zero_based,
                                  uint8_t note,
                                  uint8_t velocity,
                                  uint32_t now_tick);
void seq_live_rec_capture_note_off(uint8_t active,
                                   const seq_runtime_state_t *runtime_state,
                                   seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note,
                                   uint32_t now_tick);

#endif /* SEQ_LIVE_REC_CAPTURE_H */
