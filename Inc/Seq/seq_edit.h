#ifndef SEQ_EDIT_H
#define SEQ_EDIT_H

#include <stdint.h>

#include "Seq/seq_types.h"
#include "Seq/seq_model.h"
#include "Seq/seq_clipboard.h"

void seq_edit_init(void);
uint8_t seq_edit_toggle_hall_step(seq_track_id_t track, uint8_t hall_index);
void seq_edit_change_page(seq_track_id_t track, int8_t delta);
uint8_t seq_edit_get_page(seq_track_id_t track);
uint8_t seq_edit_map_hall_to_step(seq_track_id_t track, uint8_t hall_index, seq_step_id_t *out_step);

void seq_edit_step_press(seq_track_id_t track, uint8_t hall_index);
void seq_edit_step_release(seq_track_id_t track, uint8_t hall_index);
void seq_edit_step_hold_update(void);
uint8_t seq_edit_collect_held_steps(seq_track_id_t *out_track,
                                    seq_step_id_t *out_steps,
                                    uint8_t max_steps,
                                    uint8_t promote_pending);

uint8_t seq_edit_step_plock_find(seq_track_id_t track,
                                 seq_step_id_t step,
                                 uint8_t set_id,
                                 seq_param8_t param8,
                                 seq_plock_entry_t *out_entry);
seq_plock_op_status_t seq_edit_step_plock_upsert(seq_track_id_t track,
                                                  seq_step_id_t step,
                                                  uint8_t set_id,
                                                  seq_param8_t param8,
                                                  seq_value16_t value16,
                                                  uint8_t flags);
void seq_edit_step_plock_commit(seq_track_id_t track,
                                seq_step_id_t step,
                                uint8_t set_id,
                                seq_param8_t param8);
seq_plock_op_status_t seq_edit_step_plock_delete(seq_track_id_t track,
                                                  seq_step_id_t step,
                                                  uint8_t set_id,
                                                  seq_param8_t param8);
void seq_edit_step_plock_clear(seq_track_id_t track, seq_step_id_t step);
uint8_t seq_edit_step_plock_count(seq_track_id_t track, seq_step_id_t step);
uint8_t seq_edit_step_plock_get_at(seq_track_id_t track,
                                   seq_step_id_t step,
                                   uint8_t ordinal,
                                   seq_plock_entry_t *out_entry);

uint8_t seq_edit_copy_steps(seq_track_id_t track,
                            const seq_step_id_t *steps,
                            uint8_t step_count);
uint8_t seq_edit_paste_steps(seq_track_id_t track,
                             const seq_step_id_t *dest_steps,
                             uint8_t dest_count,
                             seq_clipboard_paste_result_t *out_result);
void seq_edit_clear_steps(seq_track_id_t track,
                          const seq_step_id_t *steps,
                          uint8_t step_count);

#endif /* SEQ_EDIT_H */

