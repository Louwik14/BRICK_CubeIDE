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

/* Must run during audio bootstrap; the IRQ path never performs lazy init. */
void mod_env3_audio_init(void);
void mod_env3_init(void);
void mod_env3_audio_apply_retrigger(uint8_t track, float value);
uint8_t mod_env3_audio_apply_track_param(uint8_t track, mod_env3_param_t param, float value);
uint8_t mod_env3_apply_track_param_temp(uint8_t track, mod_env3_param_t param, float value);
/* AUDIO-only runtime temporary clear, reached through the audio event path. */
uint8_t mod_env3_clear_track_param_temp_audio(uint8_t track, mod_env3_param_t param);
void mod_env3_note_on(uint8_t track);
void mod_env3_note_off(uint8_t track);
float mod_env3_process_track(uint8_t track, uint32_t elapsed_frames);

#ifdef __cplusplus
}
#endif
