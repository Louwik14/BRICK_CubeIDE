#pragma once

#include <stdint.h>

#include "param_registry.h"
#include "Core/entity_topology.h"
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
    TRACK_RUNTIME_ENGINE_SAMPLER,
    TRACK_RUNTIME_ENGINE_LOOPER,
    TRACK_RUNTIME_ENGINE_PRISM,
    TRACK_RUNTIME_ENGINE_DRUM,
    TRACK_RUNTIME_ENGINE_STACK,
    TRACK_RUNTIME_ENGINE_WAVE,
    TRACK_RUNTIME_ENGINE_FM,
    TRACK_RUNTIME_ENGINE_COUNT
} track_runtime_engine_t;

typedef enum
{
    TRACK_RUNTIME_VOICE_MODE_MONO = 0,
    TRACK_RUNTIME_VOICE_MODE_POLY
} track_runtime_voice_mode_t;

typedef struct track_runtime_ctx_s
{
    uint8_t midi_channel_1_16;
    uint8_t midi_source;
    uint8_t family;
    uint8_t type;
    uint8_t flags;
} track_runtime_ctx_t;

typedef enum
{
    TRACK_RUNTIME_FAMILY_OFF = 0,
    TRACK_RUNTIME_FAMILY_SYNTH,
    TRACK_RUNTIME_FAMILY_SAMPLER,
    TRACK_RUNTIME_FAMILY_DRUM,
    TRACK_RUNTIME_FAMILY_MIDI,
    TRACK_RUNTIME_FAMILY_EXTERNAL,
    TRACK_RUNTIME_FAMILY_OTHER
} track_runtime_family_t;

typedef enum
{
    TRACK_RUNTIME_TYPE_NONE = 0,
    TRACK_RUNTIME_TYPE_RAM,
    TRACK_RUNTIME_TYPE_PRISM,
    TRACK_RUNTIME_TYPE_DRUM_MD,
    TRACK_RUNTIME_TYPE_MIDI,
    TRACK_RUNTIME_TYPE_STREAM,
    TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG,
    TRACK_RUNTIME_TYPE_LOOPER,
    TRACK_RUNTIME_TYPE_MULTI,
    TRACK_RUNTIME_TYPE_STACK,
    TRACK_RUNTIME_TYPE_WAVE,
    TRACK_RUNTIME_TYPE_EXTERNAL,
    TRACK_RUNTIME_TYPE_GROUP,
    TRACK_RUNTIME_TYPE_FM,
    TRACK_RUNTIME_TYPE_OTHER
} track_runtime_type_t;

typedef enum
{
    TRACK_RUNTIME_PARAM_DOMAIN_NONE = 0,
    TRACK_RUNTIME_PARAM_DOMAIN_CFG = 1,
    TRACK_RUNTIME_PARAM_DOMAIN_ENV,
    TRACK_RUNTIME_PARAM_DOMAIN_TONE,
    TRACK_RUNTIME_PARAM_DOMAIN_MOD,
    TRACK_RUNTIME_PARAM_DOMAIN_MIX,
    TRACK_RUNTIME_PARAM_DOMAIN_PLAY,
    TRACK_RUNTIME_PARAM_DOMAIN_MIDI_FX,
    TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX
} track_runtime_param_domain_t;

typedef enum
{
    TRACK_RUNTIME_RESOURCE_NONE = 0,
    TRACK_RUNTIME_RESOURCE_FILTER,
    TRACK_RUNTIME_RESOURCE_SYNTH,
    TRACK_RUNTIME_RESOURCE_MIX,
    TRACK_RUNTIME_RESOURCE_PLAY,
    TRACK_RUNTIME_RESOURCE_MIDI_FX,
    TRACK_RUNTIME_RESOURCE_POLYPHONY,
    TRACK_RUNTIME_RESOURCE_AUDIO_FX
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

typedef enum
{
    TRACK_RUNTIME_UI_ENSEMBLE_CFG = 0,
    TRACK_RUNTIME_UI_ENSEMBLE_ENV = 1,
    TRACK_RUNTIME_UI_ENSEMBLE_TONE,
    TRACK_RUNTIME_UI_ENSEMBLE_MOD,
    TRACK_RUNTIME_UI_ENSEMBLE_MIX,
    TRACK_RUNTIME_UI_ENSEMBLE_PLAY,
    TRACK_RUNTIME_UI_ENSEMBLE_KEYBOARD = 7U,
    TRACK_RUNTIME_UI_ENSEMBLE_MIDI_FX = 8U,
    TRACK_RUNTIME_UI_ENSEMBLE_SEQ = 9U,
    TRACK_RUNTIME_UI_ENSEMBLE_FX = 10U,
    TRACK_RUNTIME_UI_ENSEMBLE_COUNT = 11U
} track_runtime_ui_ensemble_t;

typedef struct
{
    track_runtime_family_t family;
    track_runtime_type_t type;
    track_runtime_engine_t engine;
    track_runtime_bind_state_t bind_state;
    track_runtime_bind_reason_t bind_reason;
    uint8_t instance_id;
    uint8_t mix_track_id;
    uint8_t flags;
    uint8_t midi_channel_1_16;
    uint16_t ui_ensemble_mask;
    uint16_t topology_capabilities;
} track_runtime_descriptor_t;

typedef enum
{
    TRACK_RUNTIME_MIDI_SOURCE_INTERNAL = 0,
    TRACK_RUNTIME_MIDI_SOURCE_EXTERNAL,
    TRACK_RUNTIME_MIDI_SOURCE_ALL
} track_runtime_midi_source_t;

typedef struct
{
    uint8_t track_id;
    track_runtime_descriptor_t descriptor;
    uint8_t has_mix_target;
    uint8_t mix_track_id;
    uint8_t has_filter_target;
    uint8_t filter_track_id;
    uint8_t supports_vca_gate;
    uint8_t midi_channel_zero_based;
    track_runtime_midi_source_t midi_source;
} track_runtime_resolved_track_t;

void track_runtime_init(void);
void track_runtime_invalidate_all(void);
void track_runtime_invalidate_track(uint8_t track);
uint8_t track_runtime_refresh_if_dirty(void);
void track_runtime_refresh_track(uint8_t track);
void track_runtime_refresh_all(void);
/*
 * Revision guards:
 * - track_runtime_get_revision / track_runtime_get_track_revision are coherence markers only.
 * - they are valid after an explicit refresh at the consumer edge.
 * - they must not become UI business logic or hidden refresh triggers.
 */
uint32_t track_runtime_get_revision(void);
uint32_t track_runtime_get_track_revision(uint8_t track);
/*
 * Projection surface:
 * - pure reads of runtime state / descriptor / routing / gating / ensemble availability.
 * - refresh remains explicit at the call site; getters never auto-refresh.
 * - track_runtime_get_ctx is an escape hatch for consumers that need the full runtime ctx.
 */
const track_runtime_ctx_t *track_runtime_get_ctx(uint8_t track);
/* Main tracks keep their physical identity; GROUP children expose sampler
 * runtime contexts without becoming UI tracks or mixer lanes. */
uint8_t track_runtime_is_audio_routable_ctx(const track_runtime_ctx_t *ctx);
uint8_t track_runtime_is_audio_routable(uint8_t track);
uint8_t track_runtime_has_capability(uint8_t track, track_capability_t capability);
uint8_t track_runtime_get_mix_target_track(uint8_t track, uint8_t *out_mix_track);
uint8_t track_runtime_resolve_filter_target_track(uint8_t ui_track, uint8_t *out_filter_track);
uint8_t track_runtime_get_midi_channel_1_16(uint8_t track);
uint8_t track_runtime_get_midi_channel_zero_based(uint8_t track);
track_runtime_midi_source_t track_runtime_get_midi_source(uint8_t track);
uint8_t track_runtime_get_descriptor(uint8_t track, track_runtime_descriptor_t *out_descriptor);
uint8_t track_runtime_resolve_track(uint8_t track, track_runtime_resolved_track_t *out_resolved);
uint8_t track_runtime_is_ui_ensemble_available(uint8_t track, track_runtime_ui_ensemble_t ensemble);
uint8_t track_runtime_supports_vca_gate(const track_runtime_ctx_t *ctx);
track_runtime_param_status_t track_runtime_get_effective_param_status(uint8_t track, param_id_t param);
track_runtime_param_rule_t track_runtime_get_param_rule(param_id_t param);
uint8_t track_runtime_tone_slot_to_param(track_runtime_type_t type,
                                         uint8_t slot,
                                         param_id_t *out_param);
uint8_t track_runtime_tone_param_to_slot(track_runtime_type_t type,
                                         param_id_t param,
                                         uint8_t *out_slot);
track_runtime_voice_mode_t track_runtime_get_voice_mode(const track_runtime_ctx_t *ctx);
uint8_t track_runtime_is_track_prism_available(uint8_t track);
track_runtime_family_t track_runtime_family_from_ui(ui_track_family_t family);
track_runtime_type_t track_runtime_type_from_ui(ui_track_type_t type);
uint8_t track_runtime_compute_flags(track_runtime_family_t family,
                                    track_runtime_type_t type);

#ifdef __cplusplus
}
#endif
