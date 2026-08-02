#ifndef SEQ_OUTPUT_GUARD_H
#define SEQ_OUTPUT_GUARD_H

#include <stdint.h>

#include "Seq/seq_types.h"

#define SEQ_OUTPUT_GUARD_MAX_OCCURRENCES 64U
#define SEQ_OUTPUT_GUARD_MIDI_UART 0x01U
#define SEQ_OUTPUT_GUARD_MIDI_USB  0x02U

void seq_output_guard_init(void);
void seq_output_guard_reset(void);
uint8_t seq_output_guard_note_on_seen(seq_track_id_t track, uint8_t note,
                                      uint32_t occurrence_id, uint32_t generation);
uint8_t seq_output_guard_note_on_seen_mask(seq_track_id_t track, uint8_t note,
                                           uint32_t occurrence_id, uint32_t generation,
                                           uint8_t midi_dest_mask);
uint8_t seq_output_guard_note_off_seen(seq_track_id_t track, uint8_t note,
                                       uint32_t occurrence_id, uint32_t generation);
uint8_t seq_output_guard_is_note_active_on_track(seq_track_id_t track, uint8_t note);
uint8_t seq_output_guard_is_note_active_on_channel(uint8_t channel_zero_based, uint8_t note);
void seq_output_guard_panic(uint8_t send_transport_stop);

#endif /* SEQ_OUTPUT_GUARD_H */
