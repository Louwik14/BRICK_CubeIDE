#ifndef AUDIO_CONTROL_SNAPSHOT_H
#define AUDIO_CONTROL_SNAPSHOT_H

#include <stdint.h>

#include "Core/track_runtime.h"
#include "Core/track_sound_state.h"
#include "Core/track_tone_sound_state.h"
#include "mixer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float type[4];
    float level[4];
    float macro_a[4];
    float macro_b[4];
} audio_control_master_fx_snapshot_t;

typedef struct
{
    track_mod_lfo_state_t mod_lfo[2];
    param_id_t lfo_dest[2];
    float lfo_base[2];
    uint8_t lfo_base_valid[2];
} audio_control_mod_snapshot_t;

typedef struct
{
    uint32_t generation;
    uint32_t track_runtime_revision;
    track_runtime_ctx_t runtime[SEQ_TRACK_COUNT];
    track_runtime_synth_usage_t synth_usage;
    uint8_t logical_track_by_mix_track[MIXER_MAX_TRACKS];
    uint8_t ui_family[SEQ_TRACK_COUNT];
    uint8_t ui_type[SEQ_TRACK_COUNT];
    uint8_t voice_group_role[SEQ_TRACK_COUNT];
    audio_control_mod_snapshot_t mod[SEQ_TRACK_COUNT];
    audio_control_master_fx_snapshot_t master_fx[SEQ_TRACK_COUNT];
} audio_control_snapshot_t;

typedef struct
{
    uint32_t published_generation;
    uint32_t applied_generation;
    uint32_t publish_count;
    uint32_t apply_count;
    uint32_t retained_old_generation_blocks;
    uint32_t publish_skipped_pending_count;
} audio_control_snapshot_diag_t;

void audio_control_snapshot_init(void);
uint8_t audio_control_snapshot_publish_from_control(void);
void audio_control_snapshot_force_apply_latest(void);
void audio_control_snapshot_apply_pending_from_audio(void);
const audio_control_snapshot_t *audio_control_snapshot_get_active(void);
uint32_t audio_control_snapshot_get_active_track_runtime_revision(void);
const track_runtime_ctx_t *audio_control_snapshot_get_track_ctx(uint8_t track);
uint8_t audio_control_snapshot_is_audio_routable(uint8_t track);
void audio_control_snapshot_get_synth_usage(track_runtime_synth_usage_t *out_usage);
uint8_t audio_control_snapshot_get_logical_track_for_mix_track(uint8_t mix_track, uint8_t *out_track);
uint8_t audio_control_snapshot_get_midi_channel_zero_based(uint8_t track);
uint8_t audio_control_snapshot_resolve_track(uint8_t track, track_runtime_resolved_track_t *out_resolved);
track_runtime_param_status_t audio_control_snapshot_get_effective_param_status(uint8_t track, param_id_t param);
uint8_t audio_control_snapshot_get_voice_group_role(uint8_t track, uint8_t *out_role);
uint8_t audio_control_snapshot_collect_voice_group_members(uint8_t master_track,
                                                           uint8_t *out_members,
                                                           uint8_t out_members_capacity,
                                                           uint8_t *out_count);
uint8_t audio_control_snapshot_get_lfo_settings(uint8_t track, uint8_t lfo_index, track_mod_lfo_state_t *out_settings);
uint8_t audio_control_snapshot_get_lfo_base(uint8_t track, uint8_t lfo_index, param_id_t dest, float *out_base);
uint8_t audio_control_snapshot_get_master_fx(uint8_t track, audio_control_master_fx_snapshot_t *out_fx);
void audio_control_snapshot_diag_snapshot(audio_control_snapshot_diag_t *out_diag);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_CONTROL_SNAPSHOT_H */
