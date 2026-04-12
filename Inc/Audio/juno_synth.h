#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    JUNO_PLAY_MODE_POLY = 0,
    JUNO_PLAY_MODE_POLY_PORTA,
    JUNO_PLAY_MODE_UNISON,
} juno_play_mode_t;

typedef enum
{
    JUNO_PARAM_SAW = 0,
    JUNO_PARAM_PULSE,
    JUNO_PARAM_SUB,
    JUNO_PARAM_PWM,
    JUNO_PARAM_VCF_FREQ,
    JUNO_PARAM_VCF_RES,
    JUNO_PARAM_VCF_ENV,
    JUNO_PARAM_VCF_LFO,
    JUNO_PARAM_ATTACK,
    JUNO_PARAM_DECAY,
    JUNO_PARAM_SUSTAIN,
    JUNO_PARAM_RELEASE,
    JUNO_PARAM_LFO_RATE,
    JUNO_PARAM_HPF,
    JUNO_PARAM_PORTA,
    JUNO_PARAM_MODE,
    JUNO_PARAM_COUNT,
} juno_param_id_t;

void juno_synth_init(float sample_rate, uint32_t block_size);
void juno_synth_set_enabled(uint8_t enabled);
uint8_t juno_synth_is_enabled(void);
void juno_synth_set_test_mode(uint8_t enabled);
void juno_synth_set_param(juno_param_id_t param, float value);
void juno_synth_note_on(uint8_t midi_note, uint8_t velocity);
void juno_synth_note_off(uint8_t midi_note);
void juno_synth_pitch_bend(int16_t value);
void juno_synth_all_notes_off(void);
void juno_synth_cc(uint8_t cc, uint8_t value);
void juno_synth_process_block(float *mono_out, uint32_t frames);

#ifdef __cplusplus
}
#endif
