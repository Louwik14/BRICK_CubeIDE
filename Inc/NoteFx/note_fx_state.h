#ifndef NOTE_FX_STATE_H
#define NOTE_FX_STATE_H

#include <stdint.h>

#include "Param/param_ids.h"
#include "NoteFx/note_fx_contract.h"
#include "Seq/seq_types.h"

#define NOTE_FX_TRACK_COUNT SEQ_LANE_CAPACITY
#define NOTE_FX_EUCLID_LENGTH_MIN 1U
#define NOTE_FX_EUCLID_LENGTH_MAX 64U
#define NOTE_FX_EUCLID_LENGTH_DEFAULT 16U
#define NOTE_FX_EUCLID_PULSE_DEFAULT 4U
#define NOTE_FX_VOICER_TYPE_COUNT 8U

void note_fx_state_init(void);
void note_fx_chain_state_make_default(note_fx_chain_state_t *out_state);
uint8_t note_fx_chain_state_make_effective(
    const note_fx_chain_state_t *raw_state,
    note_fx_chain_state_t *out_effective);
uint8_t note_fx_chain_param_is_plockable(note_fx_chain_stage_t stage,
                                         uint8_t param);
uint8_t note_fx_chain_param_map(param_id_t id, note_fx_chain_stage_t *out_stage,
                                uint8_t *out_param);
uint8_t note_fx_chain_state_get_param(uint8_t track, param_id_t id,
                                      float *out_value);
uint8_t note_fx_chain_state_set_param(uint8_t track, param_id_t id, float value);
uint8_t note_fx_chain_state_capture_track(uint8_t track,
                                          note_fx_chain_state_t *out_state);
uint8_t note_fx_chain_state_install_track(uint8_t track,
                                          const note_fx_chain_state_t *state);

#endif
