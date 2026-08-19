#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MOD_ENV3_PARAM_ATTACK = 0,
    MOD_ENV3_PARAM_DECAY,
    MOD_ENV3_PARAM_SUSTAIN,
    MOD_ENV3_PARAM_RELEASE,
    MOD_ENV3_PARAM_COUNT
} mod_env3_param_t;

void mod_env3_init(void);
void mod_env3_control_publish_snapshot_track(uint8_t track, uint8_t reset_runtime);
void mod_env3_control_publish_snapshot_all(uint8_t reset_runtime);
void mod_env3_audio_consume_snapshots(void);
void mod_env3_audio_apply_retrigger(uint8_t track, float value);
uint8_t mod_env3_control_set_track_param(uint8_t track, mod_env3_param_t param, float value);
uint8_t mod_env3_audio_apply_track_param(uint8_t track, mod_env3_param_t param, float value);
uint8_t mod_env3_get_track_param(uint8_t track, mod_env3_param_t param, float *out_value);
uint8_t mod_env3_control_set_track_retrigger_hard(uint8_t track, float value);
uint8_t mod_env3_get_track_retrigger_hard(uint8_t track, float *out_value);
uint8_t mod_env3_apply_track_param_temp(uint8_t track, mod_env3_param_t param, float value);
/* AUDIO-only runtime temporary clear, reached through the audio event path. */
uint8_t mod_env3_clear_track_param_temp_audio(uint8_t track, mod_env3_param_t param);
void mod_env3_note_on(uint8_t track);
void mod_env3_note_off(uint8_t track);
float mod_env3_process_track(uint8_t track, uint32_t elapsed_frames);

#ifdef __cplusplus
}
#endif
