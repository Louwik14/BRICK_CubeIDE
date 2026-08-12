#ifndef SEQ_EDIT_H
#define SEQ_EDIT_H

#include <stdint.h>

#include "Seq/seq_types.h"
#include "Seq/seq_model.h"
#include "Seq/seq_clipboard.h"

void seq_edit_init(void);
uint8_t seq_edit_track_sequence_is_locked(seq_track_id_t track);
uint8_t seq_edit_toggle_hall_step(seq_track_id_t track, uint8_t hall_index);
void seq_edit_change_page(seq_track_id_t track, int8_t delta);
uint8_t seq_edit_get_page(seq_track_id_t track);
uint8_t seq_edit_map_hall_to_step(seq_track_id_t track, uint8_t hall_index, seq_step_id_t *out_step);

void seq_edit_step_press(seq_track_id_t track, uint8_t hall_index);
void seq_edit_step_release(seq_track_id_t track, uint8_t hall_index);
void seq_edit_step_hold_update(void);
uint8_t seq_edit_step_is_pressed(seq_track_id_t track, seq_step_id_t step);

typedef enum
{
    SEQ_EDIT_HELD_CONTENT_NONE = 0,
    SEQ_EDIT_HELD_CONTENT_ALL_EMPTY,
    SEQ_EDIT_HELD_CONTENT_ALL_FILLED,
    SEQ_EDIT_HELD_CONTENT_MIXED,
    SEQ_EDIT_HELD_CONTENT_QUICK_LENGTH
} seq_edit_held_content_t;

seq_edit_held_content_t seq_edit_classify_held_steps(void);
uint8_t seq_edit_prepare_held_note_capture(seq_track_id_t *out_track,
                                            seq_step_id_t *out_steps,
                                            uint8_t max_steps,
                                            uint8_t *out_count);
uint8_t seq_edit_replace_step_play_notes(seq_track_id_t track,
                                         const seq_step_id_t *steps,
                                         uint8_t step_count,
                                         const uint8_t *notes,
                                         const uint8_t *velocities,
                                         uint8_t note_count);
uint8_t seq_edit_capture_held_note_on(uint8_t note, uint8_t velocity);
uint8_t seq_edit_note_capture_note_off(uint8_t note);
void seq_edit_note_capture_reset(void);
uint8_t seq_edit_adjust_held_step_roll(int8_t delta,
                                       seq_track_id_t *out_track,
                                       seq_step_id_t *out_step,
                                       uint8_t *out_roll);
uint8_t seq_edit_collect_held_steps(seq_track_id_t *out_track,
                                    seq_step_id_t *out_steps,
                                    uint8_t max_steps,
                                    uint8_t promote_pending);
uint8_t seq_edit_collect_pressed_steps(seq_track_id_t *out_track,
                                       seq_step_id_t *out_steps,
                                       uint8_t max_steps);
uint8_t seq_edit_lowcost_length_flash_step_visible(seq_track_id_t track,
                                                   seq_step_id_t step);
uint8_t seq_edit_lowcost_range_length_candidate(seq_track_id_t track,
                                                uint8_t hall_index);

uint8_t seq_edit_step_play_find(seq_track_id_t track,
                                seq_step_id_t step,
                                param_id_t param,
                                seq_value16_t *out_value16);
seq_plock_op_status_t seq_edit_step_play_upsert(seq_track_id_t track,
                                                 seq_step_id_t step,
                                                 param_id_t param,
                                                 seq_value16_t value16);
void seq_edit_step_play_commit(seq_track_id_t track,
                               seq_step_id_t step,
                               param_id_t param);
seq_plock_op_status_t seq_edit_step_play_delete(seq_track_id_t track,
                                                 seq_step_id_t step,
                                                 param_id_t param);
void seq_edit_step_play_clear_voice(seq_track_id_t track,
                                    seq_step_id_t step,
                                    uint8_t voice);
void seq_edit_step_play_clear(seq_track_id_t track, seq_step_id_t step);

uint8_t seq_edit_step_plock_find(seq_track_id_t track,
                                 seq_step_id_t step,
                                 uint8_t set_id,
                                 seq_param_slot_t param_slot,
                                 seq_plock_entry_t *out_entry);
seq_plock_op_status_t seq_edit_step_plock_upsert(seq_track_id_t track,
                                                  seq_step_id_t step,
                                                  uint8_t set_id,
                                                  seq_param_slot_t param_slot,
                                                  seq_value16_t value16,
                                                  uint8_t flags);
void seq_edit_step_plock_commit(seq_track_id_t track,
                                seq_step_id_t step,
                                uint8_t set_id,
                                seq_param_slot_t param_slot);
seq_plock_op_status_t seq_edit_step_plock_delete(seq_track_id_t track,
                                                  seq_step_id_t step,
                                                  uint8_t set_id,
                                                  seq_param_slot_t param_slot);
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
void seq_edit_clear_steps_without_undo(seq_track_id_t track,
                                       const seq_step_id_t *steps,
                                       uint8_t step_count);

#endif /* SEQ_EDIT_H */
