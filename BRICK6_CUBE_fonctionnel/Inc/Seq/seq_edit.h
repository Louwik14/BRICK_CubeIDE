#ifndef SEQ_EDIT_H
#define SEQ_EDIT_H

#include <stdint.h>

#include "Seq/seq_types.h"

void seq_edit_init(void);
uint8_t seq_edit_toggle_hall_step(seq_track_id_t track, uint8_t hall_index);
void seq_edit_change_page(seq_track_id_t track, int8_t delta);
uint8_t seq_edit_get_page(seq_track_id_t track);
uint8_t seq_edit_map_hall_to_step(seq_track_id_t track, uint8_t hall_index, seq_step_id_t *out_step);

#endif /* SEQ_EDIT_H */
