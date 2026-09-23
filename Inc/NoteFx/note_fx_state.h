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
    NOTE_FX_MODEL_VOICER,
    NOTE_FX_MODEL_SCALER,
    NOTE_FX_MODEL_COUNT
} note_fx_model_t;

typedef enum
{
    NOTE_FX_FAMILY_OFF = 0,
    NOTE_FX_FAMILY_ARP,
    NOTE_FX_FAMILY_EUCLID,
    NOTE_FX_FAMILY_PROBABILITY,
    NOTE_FX_FAMILY_GATE,
    NOTE_FX_FAMILY_VOICER,
    NOTE_FX_FAMILY_SCALER,
    NOTE_FX_FAMILY_COUNT
} note_fx_family_t;

#define NOTE_FX_PROBABILITY_CONDITION_COUNT 10U
#define NOTE_FX_GATE_MODE_CLIP 0U
#define NOTE_FX_GATE_MODE_EXTEND 1U
#define NOTE_FX_VOICER_TYPE_COUNT 8U
#define NOTE_FX_SCALER_STICK_DOWN 0U
#define NOTE_FX_SCALER_STICK_UP 1U
#define NOTE_FX_SCALER_STICK_DROP 2U

typedef struct
{
    uint8_t min;
    uint8_t max;
    uint8_t default_value;
} note_fx_param_schema_t;

typedef struct
{
    uint8_t value[NOTE_FX_SLOT_COUNT][NOTE_FX_VALUE_COUNT];
    uint8_t order;
} note_fx_track_state_t;

_Static_assert(sizeof(note_fx_track_state_t) == 16U,
               "three-slot Note FX state budget");

void note_fx_state_init(void);
void note_fx_state_make_default(note_fx_track_state_t *out_state);
uint8_t note_fx_state_param_map(param_id_t id, uint8_t *out_slot, uint8_t *out_param);
uint8_t note_fx_state_order_map(param_id_t id);
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
note_fx_family_t note_fx_state_model_family(uint8_t model);
uint8_t note_fx_state_available_models(const note_fx_track_state_t *state,
                                       uint8_t edited_slot,
                                       uint8_t *out_models,
                                       uint8_t capacity);
uint8_t note_fx_state_validate_unique_families(
    const note_fx_track_state_t *state);

/* PASS 1 fixed-chain contract.  Raw base values and p-locks are never rewritten
 * when GENERATOR mode changes.  This projection is the sole mode-dependent
 * clamp boundary used to prepare a safe executable state. */
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
