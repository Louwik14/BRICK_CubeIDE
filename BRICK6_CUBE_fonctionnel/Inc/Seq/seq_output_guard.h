#ifndef SEQ_OUTPUT_GUARD_H
#define SEQ_OUTPUT_GUARD_H

#include <stdint.h>

#include "Seq/seq_types.h"

void seq_output_guard_init(void);
void seq_output_guard_reset(void);
void seq_output_guard_note_on_seen(seq_track_id_t track, uint8_t note);
void seq_output_guard_note_off_seen(seq_track_id_t track, uint8_t note);
uint8_t seq_output_guard_is_note_active_on_channel(uint8_t channel_zero_based, uint8_t note);
void seq_output_guard_panic(uint8_t send_transport_stop);

#endif /* SEQ_OUTPUT_GUARD_H */
