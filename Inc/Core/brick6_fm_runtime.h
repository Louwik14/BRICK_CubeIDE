#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_FM_VOICE_COUNT 16U
#define BRICK6_FM_RENDER_BLOCK 64U

typedef enum
{
    BRICK6_FM_OPERATOR_LEVEL = 0,
    BRICK6_FM_OPERATOR_FREQ,
    BRICK6_FM_OPERATOR_DETUNE,
    BRICK6_FM_OPERATOR_ENV_ATTACK,
    BRICK6_FM_OPERATOR_ENV_DECAY,
    BRICK6_FM_OPERATOR_ENV_SUSTAIN,
    BRICK6_FM_OPERATOR_ENV_RELEASE,
    BRICK6_FM_OPERATOR_ON,
    BRICK6_FM_OPERATOR_MODE,
    BRICK6_FM_OPERATOR_VEL,
    BRICK6_FM_OPERATOR_KEY,
    BRICK6_FM_OPERATOR_PARAM_COUNT
} brick6_fm_operator_param_t;

void brick6_fm_runtime_init(void);
void brick6_fm_runtime_reset_instance(uint8_t instance_id);
void brick6_fm_runtime_all_notes_off(uint8_t instance_id);
void brick6_fm_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity);
void brick6_fm_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_fm_runtime_set_ratio(uint8_t instance_id, float value);
void brick6_fm_runtime_set_algorithm(uint8_t instance_id, uint8_t algorithm);
void brick6_fm_runtime_set_feedback(uint8_t instance_id, uint8_t feedback);
void brick6_fm_runtime_set_sync(uint8_t instance_id, uint8_t enabled);
void brick6_fm_runtime_set_bright(uint8_t instance_id, float value);
void brick6_fm_runtime_set_body(uint8_t instance_id, float value);
void brick6_fm_runtime_set_detail(uint8_t instance_id, float value);
void brick6_fm_runtime_set_metal(uint8_t instance_id, float value);
void brick6_fm_runtime_set_env(uint8_t instance_id,
                               float attack,
                               float decay,
                               float sustain,
                               float release);
void brick6_fm_runtime_set_play(uint8_t instance_id,
                                float velocity,
                                float key_scaling,
                                float pitch_env,
                                float pitch_time);
void brick6_fm_runtime_set_operator(uint8_t instance_id,
                                    uint8_t operator_id,
                                    brick6_fm_operator_param_t param,
                                    float value);
void brick6_fm_runtime_sync_voice(uint8_t source_instance_id, uint8_t destination_instance_id);
uint8_t brick6_fm_runtime_render_instance(uint8_t instance_id,
                                          float *out_mono,
                                          uint32_t frames);

#ifdef __cplusplus
}
#endif
