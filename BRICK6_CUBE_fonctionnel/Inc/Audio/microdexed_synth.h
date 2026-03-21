#ifndef AUDIO_MICRODEXED_SYNTH_H
#define AUDIO_MICRODEXED_SYNTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MICRODEXED_PARAM_ALGORITHM = 0,
    MICRODEXED_PARAM_FEEDBACK,
    MICRODEXED_PARAM_TRANSPOSE,
    MICRODEXED_PARAM_LFO_SPEED,
    MICRODEXED_PARAM_LFO_DELAY,
    MICRODEXED_PARAM_LFO_PITCH_MOD_DEPTH,
    MICRODEXED_PARAM_LFO_AMP_MOD_DEPTH,
    MICRODEXED_PARAM_PITCH_BEND_RANGE,
    MICRODEXED_PARAM_PORTAMENTO_TIME,
    MICRODEXED_PARAM_MONO_MODE,
    MICRODEXED_PARAM_OPERATOR_MASK,
    MICRODEXED_PARAM_OPERATOR_1_LEVEL,
    MICRODEXED_PARAM_OPERATOR_2_LEVEL,
    MICRODEXED_PARAM_OPERATOR_3_LEVEL,
    MICRODEXED_PARAM_OPERATOR_4_LEVEL,
    MICRODEXED_PARAM_COUNT
} microdexed_param_t;

void microdexed_synth_init(float sample_rate, uint32_t block_size);
void microdexed_synth_set_enabled(uint8_t enabled);
uint8_t microdexed_synth_is_enabled(void);
void microdexed_synth_note_on(uint8_t midi_note, uint8_t velocity);
void microdexed_synth_note_off(uint8_t midi_note);
void microdexed_synth_all_notes_off(void);
void microdexed_synth_process_block(float *mono_out, uint32_t frames);
void microdexed_synth_set_param(microdexed_param_t param, float value);
float microdexed_synth_get_param(microdexed_param_t param);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MICRODEXED_SYNTH_H */
