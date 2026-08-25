#ifndef AUDIO_NOTE_ENGINE_ADAPTER_H
#define AUDIO_NOTE_ENGINE_ADAPTER_H

#include <stdint.h>

#include "Audio/control_audio_queue.h"
#include "Core/entity_topology.h"
#include "Core/track_runtime.h"

#define AUDIO_RUNTIME_FLAG_GROUP_MASTER (1U << 6)
#define AUDIO_RUNTIME_FLAG_GROUP_CHILD  (1U << 7)
#define AUDIO_RUNTIME_FLAG_CAN_FILTER   (1U << 0)

typedef struct
{
    brick_entity_id_t entity_id;
    uint8_t mix_track_id;
    uint8_t engine;
    uint8_t instance_id;
    track_runtime_bind_state_t bind_state;
    track_runtime_bind_reason_t bind_reason;
    uint32_t generation;
} track_audio_binding_t;

typedef struct track_audio_runtime_ctx_s
{
    track_audio_binding_t audio_binding;
    uint8_t midi_channel_1_16;
    uint8_t midi_source;
    uint8_t family;
    uint8_t type;
    uint8_t flags;
    uint8_t has_filter_target;
    uint8_t filter_track_id;
    uint8_t supports_vca_gate;
} track_audio_runtime_ctx_t;

typedef struct
{
    track_audio_binding_t audio_binding;
    track_runtime_type_t type;
    uint8_t has_mix_target;
    uint8_t mix_track_id;
    uint8_t has_filter_target;
    uint8_t filter_track_id;
    uint8_t supports_vca_gate;
} audio_note_engine_binding_t;

typedef struct
{
    track_audio_binding_t binding;
    uint8_t family;
    uint8_t type;
    uint8_t flags;
    uint8_t configured_voice_count;
    uint8_t physical_voice_capacity;
    uint8_t sampler_slice_mode_active;
} audio_binding_snapshot_t;

/* Value-only install command extracted from the transient scheduler event.
 * It is suitable for a prepared bulk plan and contains no queue metadata. */
typedef struct
{
    brick_entity_id_t entity_id;
    uint8_t family;
    uint8_t type;
    uint8_t midi_channel_1_16;
    uint8_t midi_source;
    uint8_t flags;
    uint8_t voice_count;
    uint8_t reserved;
    float voice_spread;
} audio_note_engine_install_spec_t;

#if defined(__cplusplus)
static_assert(sizeof(audio_note_engine_install_spec_t) == 12U,
              "prepared binding install ABI changed");
#else
_Static_assert(sizeof(audio_note_engine_install_spec_t) == 12U,
               "prepared binding install ABI changed");
#endif

void audio_note_engine_adapter_init(void);
void audio_note_engine_adapter_audio_publish_snapshot(void);
void audio_note_engine_adapter_audio_publish_snapshot_entity(
    brick_entity_id_t entity_id);
const track_audio_runtime_ctx_t *audio_note_engine_adapter_audio_ctx(
    brick_entity_id_t entity_id);
uint16_t audio_note_engine_adapter_entity_mask(
    track_runtime_engine_t engine);
brick_entity_id_t audio_note_engine_adapter_entity_for_mix_lane(
    uint8_t mix_track_id);
uint8_t audio_note_engine_adapter_snapshot_read(
    brick_entity_id_t entity_id,
    audio_binding_snapshot_t *out_snapshot);
uint8_t audio_note_engine_adapter_ctx_is_audio_routable(
    const track_audio_runtime_ctx_t *ctx);
uint8_t audio_note_engine_adapter_ctx_supports_vca_gate(
    const track_audio_runtime_ctx_t *ctx);
uint8_t audio_note_engine_adapter_ctx_filter_target(
    const track_audio_runtime_ctx_t *ctx,
    uint8_t *out_track);
uint8_t audio_note_engine_adapter_audio_midi_channel_zero_based(
    const track_audio_runtime_ctx_t *ctx,
    uint8_t *out_channel);

uint8_t audio_note_engine_adapter_resolve(
    brick_entity_id_t entity_id,
    uint32_t binding_generation,
    audio_note_engine_binding_t *out_binding);
uint8_t audio_note_engine_adapter_apply(
                                        const audio_note_engine_binding_t *binding,
                                        uint8_t note,
                                        uint8_t velocity,
                                        uint8_t is_note_on,
    uint32_t occurrence_token);

void audio_note_engine_adapter_install_intent(
    const control_audio_event_t *event);
uint8_t audio_note_engine_adapter_prepare_install_spec(
    const control_audio_event_t *event,
    audio_note_engine_install_spec_t *out_spec);
track_runtime_engine_t audio_note_engine_adapter_choose_engine(
    track_runtime_family_t family, track_runtime_type_t type);
void audio_note_engine_adapter_install_prepared(
    const audio_note_engine_install_spec_t *spec);
uint8_t audio_note_engine_adapter_apply_polyphony(
    brick_entity_id_t entity_id, uint8_t voice_count, float spread);
uint8_t audio_note_engine_adapter_set_mute(brick_entity_id_t entity_id,
                                           uint8_t muted);
uint8_t audio_note_engine_adapter_set_master(float gain);
uint32_t audio_note_engine_adapter_installed_generation(
    brick_entity_id_t entity_id);

#endif /* AUDIO_NOTE_ENGINE_ADAPTER_H */
