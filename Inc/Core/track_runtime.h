#pragma once

#include <stdint.h>

#include "param_registry.h"
#include "Seq/seq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    TRACK_RUNTIME_BIND_UNBOUND = 0,
    TRACK_RUNTIME_BIND_BOUND,
    TRACK_RUNTIME_BIND_QUOTA_BLOCKED
} track_runtime_bind_state_t;

typedef enum
{
    TRACK_RUNTIME_BIND_REASON_NONE = 0,
    TRACK_RUNTIME_BIND_REASON_TRACK_OFF,
    TRACK_RUNTIME_BIND_REASON_UNSUPPORTED,
    TRACK_RUNTIME_BIND_REASON_QUOTA_EXCEEDED
} track_runtime_bind_reason_t;

typedef enum
{
    TRACK_RUNTIME_ENGINE_NONE = 0,
    TRACK_RUNTIME_ENGINE_AUDIO_TRACK,
    TRACK_RUNTIME_ENGINE_DX7, /* legacy engine id retained; not bound anymore */
    TRACK_RUNTIME_ENGINE_MONOB, /* legacy engine id retained; not bound anymore */
    TRACK_RUNTIME_ENGINE_SAMPLER,
    TRACK_RUNTIME_ENGINE_MASTER_BUFFER,
    TRACK_RUNTIME_ENGINE_TB3, /* legacy engine id retained; not bound anymore */
    TRACK_RUNTIME_ENGINE_DRUM
} track_runtime_engine_t;

typedef enum
{
    TRACK_RUNTIME_VOICE_MODE_MONO = 0,
    TRACK_RUNTIME_VOICE_MODE_POLY
} track_runtime_voice_mode_t;

typedef struct
{
    uint8_t track_id;
    uint8_t mix_track_id;
    uint8_t family;
    uint8_t type;
    uint8_t engine;
    uint8_t instance_id;
    track_runtime_bind_state_t bind_state;
    track_runtime_bind_reason_t bind_reason;
    uint8_t flags;
} track_runtime_ctx_t;

typedef enum
{
    TRACK_RUNTIME_FAMILY_OFF = 0,
    TRACK_RUNTIME_FAMILY_INPUT,
    TRACK_RUNTIME_FAMILY_SYNTH,
    TRACK_RUNTIME_FAMILY_DRUM,
    TRACK_RUNTIME_FAMILY_MASTER,
    TRACK_RUNTIME_FAMILY_MIDI,
    TRACK_RUNTIME_FAMILY_OTHER
} track_runtime_family_t;

typedef enum
{
    TRACK_RUNTIME_TYPE_AUDIO = 0,
    TRACK_RUNTIME_TYPE_HYBRID,
    TRACK_RUNTIME_TYPE_DX7, /* legacy runtime type retained; remapped to Sampler runtime */
    TRACK_RUNTIME_TYPE_MONOB, /* legacy runtime type retained; remapped to Sampler runtime */
    TRACK_RUNTIME_TYPE_SAMPLER,
    TRACK_RUNTIME_TYPE_BUFFER,
    TRACK_RUNTIME_TYPE_TB3, /* legacy runtime type retained; not bound anymore */
    TRACK_RUNTIME_TYPE_DRUM_TRX_BD,
    TRACK_RUNTIME_TYPE_DRUM_TRX_CLAVES,
    TRACK_RUNTIME_TYPE_DRUM_TRX_HIHAT,
    TRACK_RUNTIME_TYPE_DRUM_TRX_SNARE,
    TRACK_RUNTIME_TYPE_DRUM_FM_KICK,
    TRACK_RUNTIME_TYPE_DRUM_FM_SNARE,
    TRACK_RUNTIME_TYPE_DRUM_FM_TOM,
    TRACK_RUNTIME_TYPE_DRUM_FM_RIMSHOT,
    TRACK_RUNTIME_TYPE_DRUM_FM_CLAP,
    TRACK_RUNTIME_TYPE_DRUM_FM_COWBELL,
    TRACK_RUNTIME_TYPE_DRUM_FM_CYMBAL,
    TRACK_RUNTIME_TYPE_MIDI,
    TRACK_RUNTIME_TYPE_OTHER
} track_runtime_type_t;

typedef enum
{
    TRACK_RUNTIME_PARAM_DOMAIN_NONE = 0,
    TRACK_RUNTIME_PARAM_DOMAIN_COLORS,
    TRACK_RUNTIME_PARAM_DOMAIN_TONE,
    TRACK_RUNTIME_PARAM_DOMAIN_MOD,
    TRACK_RUNTIME_PARAM_DOMAIN_MIX,
    TRACK_RUNTIME_PARAM_DOMAIN_BUFFER,
    TRACK_RUNTIME_PARAM_DOMAIN_PLAY
} track_runtime_param_domain_t;

typedef enum
{
    TRACK_RUNTIME_RESOURCE_NONE = 0,
    TRACK_RUNTIME_RESOURCE_FILTER,
    TRACK_RUNTIME_RESOURCE_SYNTH,
    TRACK_RUNTIME_RESOURCE_MIX,
    TRACK_RUNTIME_RESOURCE_BUFFER,
    TRACK_RUNTIME_RESOURCE_PLAY
} track_runtime_resource_t;

typedef enum
{
    TRACK_RUNTIME_CARDINALITY_PER_TRACK = 0,
    TRACK_RUNTIME_CARDINALITY_SHARED,
    TRACK_RUNTIME_CARDINALITY_GLOBAL
} track_runtime_cardinality_t;

typedef enum
{
    TRACK_RUNTIME_PARAM_ALLOWED = 0,
    TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL,
    TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED
} track_runtime_param_status_t;

typedef struct
{
    track_runtime_param_domain_t domain;
    track_runtime_resource_t resource;
    track_runtime_cardinality_t cardinality;
    track_runtime_param_status_t status;
} track_runtime_param_rule_t;

typedef struct
{
    uint8_t monob_tracks;
    uint8_t drum_tracks;
} track_runtime_synth_usage_t;

void track_runtime_init(void);
void track_runtime_invalidate_all(void);
void track_runtime_invalidate_track(uint8_t track);
void track_runtime_refresh_track(uint8_t track);
void track_runtime_refresh_all(void);
void track_runtime_get_cached_synth_usage(track_runtime_synth_usage_t *out_usage);
uint32_t track_runtime_get_revision(void);
uint32_t track_runtime_get_track_revision(uint8_t track);
const track_runtime_ctx_t *track_runtime_get_ctx(uint8_t track);
uint8_t track_runtime_is_audio_routable(uint8_t track);
uint8_t track_runtime_get_mix_target_track(uint8_t track, uint8_t *out_mix_track);
uint8_t track_runtime_get_logical_track_for_mix_track(uint8_t mix_track, uint8_t *out_track);
uint8_t track_runtime_resolve_filter_target_track(uint8_t ui_track, uint8_t *out_filter_track);
uint8_t track_runtime_supports_vca_gate(const track_runtime_ctx_t *ctx);
track_runtime_param_status_t track_runtime_get_effective_param_status(uint8_t track, param_id_t param);
track_runtime_param_rule_t track_runtime_get_param_rule(param_id_t param);
track_runtime_voice_mode_t track_runtime_get_voice_mode(const track_runtime_ctx_t *ctx);
uint8_t track_runtime_get_play_voice_count(const track_runtime_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
