#ifndef AUDIO_NOTE_ENGINE_ADAPTER_H
#define AUDIO_NOTE_ENGINE_ADAPTER_H

#include <stdint.h>

#include "Track/entity_topology.h"
#include "Track/track_runtime.h"

typedef struct
{
    brick_entity_id_t entity_id;
    uint8_t mix_track_id;
    uint8_t engine;
    uint8_t instance_id;
    uint8_t active;
} audio_program_route_t;

typedef struct track_audio_runtime_ctx_s
{
    audio_program_route_t program_route;
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
    audio_program_route_t program_route;
    track_runtime_type_t type;
    uint8_t has_mix_target;
    uint8_t mix_track_id;
    uint8_t has_filter_target;
    uint8_t filter_track_id;
    uint8_t supports_vca_gate;
} audio_note_engine_program_t;

/* Value-only install command extracted from the transient scheduler event.
 * It is suitable for a prepared bulk plan and contains no queue metadata. */
typedef struct
{
    brick_entity_id_t entity_id;
    uint8_t family;
    uint8_t type;
    uint8_t topology_flags;
} audio_note_engine_install_spec_t;

#if defined(__cplusplus)
static_assert(sizeof(audio_note_engine_install_spec_t) == 4U,
              "prepared program install ABI changed");
#else
_Static_assert(sizeof(audio_note_engine_install_spec_t) == 4U,
               "prepared program install ABI changed");
#endif

void audio_note_engine_adapter_init(void);
uint8_t audio_note_engine_adapter_current_ctx(
    brick_entity_id_t entity_id,
    track_audio_runtime_ctx_t *out_context);
uint16_t audio_note_engine_adapter_entity_mask(
    track_runtime_engine_t engine);
brick_entity_id_t audio_note_engine_adapter_entity_for_mix_lane(
    uint8_t mix_track_id);
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

uint8_t audio_note_engine_adapter_current(
    brick_entity_id_t entity_id,
    audio_note_engine_program_t *out_program);
uint8_t audio_note_engine_adapter_apply_output(
    brick_entity_id_t entity_id, uint8_t note, uint8_t velocity,
    uint8_t is_note_on, uint32_t output_id);

uint8_t audio_note_engine_adapter_install_prepared(
    const audio_note_engine_install_spec_t *spec);
uint8_t audio_note_engine_adapter_initialize_held_outputs(
    brick_entity_id_t entity_id);
void audio_note_engine_adapter_forget_outputs(brick_entity_id_t entity_id);
uint8_t audio_note_engine_adapter_apply_polyphony(
    brick_entity_id_t entity_id, uint8_t voice_count, float spread);
uint8_t audio_note_engine_adapter_apply_midi_config(
    brick_entity_id_t entity_id, uint8_t channel_1_16, uint8_t source);
uint8_t audio_note_engine_adapter_set_mute(brick_entity_id_t entity_id,
                                           uint8_t muted);
uint8_t audio_note_engine_adapter_set_master(float gain);

#endif /* AUDIO_NOTE_ENGINE_ADAPTER_H */
