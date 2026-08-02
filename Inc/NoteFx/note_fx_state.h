#ifndef NOTE_FX_STATE_H
#define NOTE_FX_STATE_H

#include <stdint.h>

#include "Param/param_store.h"

#define NOTE_FX_TRACK_COUNT 8U
#define NOTE_FX_SLOT_COUNT 4U
#define NOTE_FX_PARAM_COUNT 4U

typedef enum
{
    NOTE_FX_MODEL_OFF = 0,
    NOTE_FX_MODEL_ARP,
    NOTE_FX_MODEL_COUNT
} note_fx_model_t;

typedef struct
{
    uint8_t value[NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT];
} note_fx_track_state_t;

void note_fx_state_init(void);
uint8_t note_fx_state_param_map(param_id_t id, uint8_t *out_slot, uint8_t *out_param);
uint8_t note_fx_state_get_param(uint8_t track, param_id_t id, float *out_value);
uint8_t note_fx_state_set_param(uint8_t track, param_id_t id, float value);
uint8_t note_fx_state_capture_track(uint8_t track, note_fx_track_state_t *out_state);
uint8_t note_fx_state_restore_track(uint8_t track, const note_fx_track_state_t *state);
uint8_t note_fx_state_normalize_track(note_fx_track_state_t *state);

#endif
