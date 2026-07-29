#ifndef BRICK6_DAISY_RUNTIME_H
#define BRICK6_DAISY_RUNTIME_H

#include <stdint.h>

#include "Seq/seq_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_DAISY_SAMPLE_RATE 48000.0f
#define BRICK6_DAISY_MAX_INSTANCES SEQ_TRACK_COUNT
#define BRICK6_DAISY_PARAM_COUNT 15U

typedef enum
{
    BRICK6_DAISY_MODEL_OSC = 0,
    BRICK6_DAISY_MODEL_VAR_SAW,
    BRICK6_DAISY_MODEL_VAR_SHAPE,
    BRICK6_DAISY_MODEL_FM2,
    BRICK6_DAISY_MODEL_FORMANT,
    BRICK6_DAISY_MODEL_VOSIM,
    BRICK6_DAISY_MODEL_Z_OSC,
    BRICK6_DAISY_MODEL_OSC_BANK,
    BRICK6_DAISY_MODEL_HARMONIC,
    BRICK6_DAISY_MODEL_COUNT
} brick6_daisy_model_t;

void brick6_daisy_runtime_init(void);
void brick6_daisy_runtime_reset_instance(uint8_t instance_id);
void brick6_daisy_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity);
void brick6_daisy_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_daisy_runtime_all_notes_off(uint8_t instance_id);
void brick6_daisy_runtime_set_model(uint8_t instance_id, brick6_daisy_model_t model);
void brick6_daisy_runtime_set_param(uint8_t instance_id, uint8_t param_index, float value);
uint8_t brick6_daisy_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_DAISY_RUNTIME_H */
