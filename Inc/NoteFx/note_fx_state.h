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

typedef enum
{
    NOTE_FX_MODEL_OFF = 0,
    NOTE_FX_MODEL_ARP,
    NOTE_FX_MODEL_EUCLID,
    NOTE_FX_MODEL_PROBABILITY,
    NOTE_FX_MODEL_GATE,
    NOTE_FX_MODEL_GROOVE,
    NOTE_FX_MODEL_ECHO,
    NOTE_FX_MODEL_HARMONIZER,
    NOTE_FX_MODEL_CHORD,
    NOTE_FX_MODEL_COUNT
} note_fx_model_t;

#define NOTE_FX_PROBABILITY_CONDITION_COUNT 10U
#define NOTE_FX_GATE_MODE_CLIP 0U
#define NOTE_FX_GATE_MODE_LEGATO 1U
#define NOTE_FX_GATE_MODE_RETRIG 2U
#define NOTE_FX_GROOVE_TYPE_COUNT 4U
#define NOTE_FX_ECHO_REPEAT_MAX 2U
#define NOTE_FX_HARMONIZER_TYPE_COUNT 8U

typedef struct
{
    uint8_t min;
    uint8_t max;
    uint8_t default_value;
} note_fx_param_schema_t;

typedef struct
{
    uint8_t value[NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT];
} note_fx_track_state_t;

void note_fx_state_init(void);
uint8_t note_fx_state_param_map(param_id_t id, uint8_t *out_slot, uint8_t *out_param);
uint8_t note_fx_state_get_param(uint8_t track, param_id_t id, float *out_value);
uint8_t note_fx_state_get_param_schema(uint8_t model,
                                       uint8_t param,
                                       note_fx_param_schema_t *out_schema);
uint8_t note_fx_state_set_param(uint8_t track, param_id_t id, float value);
uint8_t note_fx_state_capture_track(uint8_t track, note_fx_track_state_t *out_state);
uint8_t note_fx_state_restore_track(uint8_t track, const note_fx_track_state_t *state);
uint8_t note_fx_state_install_prepared_track(uint8_t track,
                                             const note_fx_track_state_t *state);
uint8_t note_fx_state_normalize_track(note_fx_track_state_t *state);

#endif
