#ifndef BRICK6_DELUGE_RUNTIME_H
#define BRICK6_DELUGE_RUNTIME_H

#include <stdint.h>

#include "Seq/seq_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_DELUGE_SAMPLE_RATE 48000.0f
#define BRICK6_DELUGE_MAX_INSTANCES SEQ_TRACK_COUNT

typedef enum
{
    BRICK6_DELUGE_MODEL_SINE = 0,
    BRICK6_DELUGE_MODEL_TRI,
    BRICK6_DELUGE_MODEL_SQUARE,
    BRICK6_DELUGE_MODEL_ANALOG_SQUARE,
    BRICK6_DELUGE_MODEL_SAW,
    BRICK6_DELUGE_MODEL_ANALOG_SAW,
    BRICK6_DELUGE_MODEL_COUNT
} brick6_deluge_model_t;

void brick6_deluge_runtime_init(void);
void brick6_deluge_runtime_reset_instance(uint8_t instance_id);
void brick6_deluge_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity);
void brick6_deluge_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_deluge_runtime_all_notes_off(uint8_t instance_id);
void brick6_deluge_runtime_set_model(uint8_t instance_id, brick6_deluge_model_t model);
void brick6_deluge_runtime_set_level(uint8_t instance_id, float level);
void brick6_deluge_runtime_set_tune(uint8_t instance_id, float semitones);
void brick6_deluge_runtime_set_fine(uint8_t instance_id, float cents);
void brick6_deluge_runtime_set_width(uint8_t instance_id, float value);
void brick6_deluge_runtime_set_width_modulated(uint8_t instance_id, float value);
void brick6_deluge_runtime_set_phase(uint8_t instance_id, float degrees);
void brick6_deluge_runtime_set_retrig(uint8_t instance_id, uint8_t enabled);
uint8_t brick6_deluge_runtime_prepare_block(uint8_t instance_id,
                                            uint32_t frames,
                                            uint8_t downstream_source_required);
uint8_t brick6_deluge_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_DELUGE_RUNTIME_H */
