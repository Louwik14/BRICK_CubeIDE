#ifndef AUDIO_CONTROL_COMMAND_H
#define AUDIO_CONTROL_COMMAND_H

#include <stdint.h>

#include "Audio/drum_model_ids.h"
#include "Param/param_store.h"
#include "mixer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_CONTROL_COMMANDS_PER_BLOCK_MAX 32U

typedef enum
{
    AUDIO_CONTROL_COMMAND_MIXER_MASTER = 0,
    AUDIO_CONTROL_COMMAND_MIXER_TRACK_GAIN,
    AUDIO_CONTROL_COMMAND_MIXER_TRACK_PAN,
    AUDIO_CONTROL_COMMAND_MIXER_TRACK_MUTE,
    AUDIO_CONTROL_COMMAND_MIXER_TRACK_SEND_LEVEL,
    AUDIO_CONTROL_COMMAND_MIXER_SEND_FX_SLOT,
    AUDIO_CONTROL_COMMAND_MIXER_REVERB_PARAM,
    AUDIO_CONTROL_COMMAND_MIXER_DELAY_PARAM,
    AUDIO_CONTROL_COMMAND_MIXER_FILTER_PARAM,
    AUDIO_CONTROL_COMMAND_MIXER_VCA_PARAM,
    AUDIO_CONTROL_COMMAND_ENGINE_WAVE_PARAM,
    AUDIO_CONTROL_COMMAND_ENGINE_WAVE_NOTE,
    AUDIO_CONTROL_COMMAND_ENGINE_DRUM_PARAM,
    AUDIO_CONTROL_COMMAND_ENGINE_DRUM_NOTE,
    AUDIO_CONTROL_COMMAND_ENGINE_SAMPLER_PARAM,
    AUDIO_CONTROL_COMMAND_ENGINE_SAMPLER_NOTE,
    AUDIO_CONTROL_COMMAND_LOOPER_PARAM,
    AUDIO_CONTROL_COMMAND_XFADE,
    AUDIO_CONTROL_COMMAND_PANIC_TRACK,
    AUDIO_CONTROL_COMMAND_PANIC_ALL,
    AUDIO_CONTROL_COMMAND_AUDIO_RUNTIME_RESET_ALL,
    AUDIO_CONTROL_COMMAND_MIXER_REBIND_ONE,
    AUDIO_CONTROL_COMMAND_MIXER_REBIND_ALL,
    AUDIO_CONTROL_COMMAND_MIXER_SNAP_TRACK
} audio_control_command_kind_t;

typedef enum
{
    AUDIO_CONTROL_REVERB_WET = 0,
    AUDIO_CONTROL_REVERB_SIZE,
    AUDIO_CONTROL_REVERB_DECAY,
    AUDIO_CONTROL_REVERB_PRE_DELAY,
    AUDIO_CONTROL_REVERB_SURROUND,
    AUDIO_CONTROL_REVERB_TYPE,
    AUDIO_CONTROL_REVERB_HPF,
    AUDIO_CONTROL_REVERB_LPF
} audio_control_reverb_param_t;

typedef enum
{
    AUDIO_CONTROL_DELAY_TYPE = 0,
    AUDIO_CONTROL_DELAY_MODE,
    AUDIO_CONTROL_DELAY_TIME,
    AUDIO_CONTROL_DELAY_TIME_R,
    AUDIO_CONTROL_DELAY_FEEDBACK,
    AUDIO_CONTROL_DELAY_HPF,
    AUDIO_CONTROL_DELAY_LPF,
    AUDIO_CONTROL_DELAY_PINGPONG,
    AUDIO_CONTROL_DELAY_WIDTH,
    AUDIO_CONTROL_DELAY_FEEDBACK_WIDTH,
    AUDIO_CONTROL_DELAY_MOD_DEPTH,
    AUDIO_CONTROL_DELAY_MOD_RATE,
    AUDIO_CONTROL_DELAY_REVERB_SEND,
    AUDIO_CONTROL_DELAY_VOLUME
} audio_control_delay_param_t;

typedef enum
{
    AUDIO_CONTROL_FILTER_TYPE = 0,
    AUDIO_CONTROL_FILTER_CUTOFF,
    AUDIO_CONTROL_FILTER_RESONANCE,
    AUDIO_CONTROL_FILTER_EG_AMOUNT,
    AUDIO_CONTROL_FILTER_ATTACK,
    AUDIO_CONTROL_FILTER_DECAY,
    AUDIO_CONTROL_FILTER_SUSTAIN,
    AUDIO_CONTROL_FILTER_RELEASE,
    AUDIO_CONTROL_FILTER_KEYTRACK,
    AUDIO_CONTROL_FILTER_EQ_LOW,
    AUDIO_CONTROL_FILTER_EQ_MID,
    AUDIO_CONTROL_FILTER_EQ_HIGH,
    AUDIO_CONTROL_FILTER_ALL_NOTES_OFF
} audio_control_filter_param_t;

typedef enum
{
    AUDIO_CONTROL_VCA_ATTACK = 0,
    AUDIO_CONTROL_VCA_DECAY,
    AUDIO_CONTROL_VCA_SUSTAIN,
    AUDIO_CONTROL_VCA_RELEASE,
    AUDIO_CONTROL_VCA_ENABLED,
    AUDIO_CONTROL_VCA_ALL_NOTES_OFF
} audio_control_vca_param_t;

typedef enum
{
    AUDIO_CONTROL_WAVE_EDIT = 0,
    AUDIO_CONTROL_WAVE_FINE,
    AUDIO_CONTROL_WAVE_COARSE,
    AUDIO_CONTROL_WAVE_FM,
    AUDIO_CONTROL_WAVE_TIMBRE,
    AUDIO_CONTROL_WAVE_MODULATION,
    AUDIO_CONTROL_WAVE_COLOR,
    AUDIO_CONTROL_WAVE_PHASE_RESET,
    AUDIO_CONTROL_WAVE_VCA_RELEASE,
    AUDIO_CONTROL_WAVE_ALL_NOTES_OFF,
    AUDIO_CONTROL_WAVE_RESET_INSTANCE
} audio_control_wave_param_t;

typedef enum
{
    AUDIO_CONTROL_SAMPLER_SAMPLE = 0,
    AUDIO_CONTROL_SAMPLER_GAIN,
    AUDIO_CONTROL_SAMPLER_MULTI_INSTRUMENT,
    AUDIO_CONTROL_SAMPLER_MULTI_GAIN,
    AUDIO_CONTROL_SAMPLER_MULTI_LOOP,
    AUDIO_CONTROL_SAMPLER_START,
    AUDIO_CONTROL_SAMPLER_END,
    AUDIO_CONTROL_SAMPLER_MODE,
    AUDIO_CONTROL_SAMPLER_TUNE,
    AUDIO_CONTROL_SAMPLER_LOOP_START,
    AUDIO_CONTROL_SAMPLER_SLICE_COUNT,
    AUDIO_CONTROL_SAMPLER_CLIP_SOURCE_BPM,
    AUDIO_CONTROL_SAMPLER_CLIP_SYNC_LENGTH,
    AUDIO_CONTROL_SAMPLER_CLIP_PITCH,
    AUDIO_CONTROL_SAMPLER_CLIP_PLAY_MODE,
    AUDIO_CONTROL_SAMPLER_CLIP_LOOP,
    AUDIO_CONTROL_SAMPLER_CLIP_STRETCH_MODE,
    AUDIO_CONTROL_SAMPLER_CLIP_GRAIN,
    AUDIO_CONTROL_SAMPLER_STOP,
    AUDIO_CONTROL_SAMPLER_STOP_TRANSPORT_CLIPS,
    AUDIO_CONTROL_SAMPLER_RESET_TRACK,
    AUDIO_CONTROL_SAMPLER_STOP_MULTI_INSTRUMENT
} audio_control_sampler_param_t;

typedef enum
{
    AUDIO_CONTROL_LOOPER_PLAY_AUTO = 0,
    AUDIO_CONTROL_LOOPER_STRETCH,
    AUDIO_CONTROL_LOOPER_STOP_PLAYBACK,
    AUDIO_CONTROL_LOOPER_TRANSPORT_START,
    AUDIO_CONTROL_LOOPER_TRANSPORT_STOP,
    AUDIO_CONTROL_LOOPER_PREPARE_REPLACE
} audio_control_looper_param_t;

typedef enum
{
    AUDIO_CONTROL_NOTE_OFF = 0,
    AUDIO_CONTROL_NOTE_ON,
    AUDIO_CONTROL_NOTE_ALL_OFF
} audio_control_note_action_t;

typedef struct
{
    uint32_t current_depth;
    uint32_t max_depth;
    uint32_t coalesced_commands;
    uint32_t rejected_commands;
    uint32_t critical_failures;
    uint32_t stale_generation_ignored;
    uint32_t max_consumed_per_block;
} audio_control_command_diag_t;

void audio_control_command_init(void);
void audio_control_command_process_from_audio(void);
void audio_control_command_diag_snapshot(audio_control_command_diag_t *out_diag);

uint8_t audio_control_command_submit_mixer_master(float value);
uint8_t audio_control_command_submit_mixer_track_gain(uint8_t mix_track, float value);
uint8_t audio_control_command_submit_mixer_track_pan(uint8_t mix_track, float value);
uint8_t audio_control_command_submit_mixer_track_mute(uint8_t mix_track, uint8_t value);
uint8_t audio_control_command_submit_mixer_track_send_level(uint8_t mix_track, uint8_t send, float value);
uint8_t audio_control_command_submit_mixer_send_fx_slot(uint8_t send, int8_t slot);
uint8_t audio_control_command_submit_mixer_reverb(uint8_t param, float value);
uint8_t audio_control_command_submit_mixer_delay(uint8_t param, float value);
uint8_t audio_control_command_submit_mixer_filter(uint8_t mix_track, uint8_t param, float value);
uint8_t audio_control_command_submit_mixer_vca(uint8_t mix_track, uint8_t param, float value);
uint8_t audio_control_command_submit_mixer_rebind_one(uint8_t previous_mix_track, uint8_t next_mix_track);
uint8_t audio_control_command_submit_mixer_rebind_all(const uint8_t *previous_mix_tracks,
                                                      const uint8_t *next_mix_tracks,
                                                      uint8_t track_count);
uint8_t audio_control_command_submit_mixer_snap_track(uint8_t mix_track);

uint8_t audio_control_command_submit_wave_param(uint8_t instance, uint8_t param, float value);
uint8_t audio_control_command_submit_wave_note(uint8_t instance, uint8_t action, uint8_t note, uint8_t velocity);
uint8_t audio_control_command_submit_drum_param(uint8_t instance, param_id_t param, float value);
uint8_t audio_control_command_submit_drum_note(uint8_t instance, uint8_t action, uint8_t note, uint8_t velocity);
uint8_t audio_control_command_submit_sampler_param(uint8_t track, uint8_t param, float value, uint16_t value_u16);
uint8_t audio_control_command_submit_sampler_note(uint8_t track, uint8_t action, uint8_t note, uint8_t velocity);
uint8_t audio_control_command_submit_looper_param(uint8_t track, uint8_t param, float value, uint16_t value_u16);
uint8_t audio_control_command_submit_looper_stretch(uint8_t track,
                                                    uint8_t mode,
                                                    float pitch_semitones,
                                                    uint16_t grain_frames);
uint8_t audio_control_command_submit_xfade(float value);
uint8_t audio_control_command_submit_panic_track(uint8_t track, uint8_t mix_track, uint8_t engine, uint8_t instance);
uint8_t audio_control_command_submit_panic_all(void);
uint8_t audio_control_command_submit_audio_runtime_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_CONTROL_COMMAND_H */
