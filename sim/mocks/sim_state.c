#include <stdbool.h>
#include <string.h>

#include "Track/track_catalog.h"
#include "Track/track_runtime.h"
#include "Track/track_state.h"
#include "Track/entity_topology.h"
#include "Seq/seq_param_iface.h"

static track_config_t g_tracks[BRICK_ENTITY_CAPACITY];
static track_runtime_ctx_t g_runtime[BRICK_ENTITY_CAPACITY];
static uint8_t g_midi_channel[BRICK_ENTITY_CAPACITY];
static uint8_t g_midi_source[BRICK_ENTITY_CAPACITY];
static uint8_t g_external_input[TRACK_COUNT];
static uint8_t g_ready;
static uint8_t g_transport_running;
static uint32_t g_tempo_milli = 120000U;
static float g_parameters[BRICK_ENTITY_CAPACITY][PARAM_COUNT];

static void ensure_state(void)
{
    if (g_ready) return;
    for (uint8_t i = 0U; i < BRICK_ENTITY_CAPACITY; ++i)
    {
        g_tracks[i].family = TRACK_FAMILY_SYNTH;
        g_tracks[i].type = TRACK_TYPE_PRISM;
        g_midi_channel[i] = (uint8_t)((i % 16U) + 1U);
        g_runtime[i].family = TRACK_RUNTIME_FAMILY_SYNTH;
        g_runtime[i].type = TRACK_RUNTIME_TYPE_PRISM;
        g_runtime[i].midi_channel_1_16 = g_midi_channel[i];
    }
    g_ready = 1U;
}

const track_config_t *track_state_get_configs(void) { ensure_state(); return g_tracks; }
track_config_t track_state_get_config(uint8_t track) { ensure_state(); return track < BRICK_ENTITY_CAPACITY ? g_tracks[track] : (track_config_t){0}; }
track_family_t track_state_get_family(uint8_t track) { return track_state_get_config(track).family; }
track_type_t track_state_get_type(uint8_t track) { return track_state_get_config(track).type; }
uint8_t track_state_get_midi_channel(uint8_t track) { ensure_state(); return track < BRICK_ENTITY_CAPACITY ? g_midi_channel[track] : 1U; }
track_midi_source_t track_state_get_midi_source(uint8_t track) { ensure_state(); return track < BRICK_ENTITY_CAPACITY ? (track_midi_source_t)g_midi_source[track] : TRACK_MIDI_SOURCE_INTERNAL; }
uint8_t track_state_get_external_input(uint8_t track) { return track < TRACK_COUNT ? g_external_input[track] : 0U; }
bool track_state_set_track_midi_channel(uint8_t track, uint8_t channel) { ensure_state(); if (track >= BRICK_ENTITY_CAPACITY || channel < 1U || channel > 16U) return false; g_midi_channel[track] = channel; return true; }
bool track_state_set_track_midi_source(uint8_t track, track_midi_source_t source) { ensure_state(); if (track >= BRICK_ENTITY_CAPACITY) return false; g_midi_source[track] = (uint8_t)source; return true; }
bool track_state_set_external_input(uint8_t track, uint8_t input) { if (track >= TRACK_COUNT) return false; g_external_input[track] = input; return true; }
uint8_t track_state_count_tracks_with_family(track_family_t family) { ensure_state(); uint8_t n=0U; for(uint8_t i=0U;i<TRACK_COUNT;++i) if(g_tracks[i].family==family)++n; return n; }

const track_runtime_ctx_t *track_runtime_get_ctx(uint8_t track) { ensure_state(); return track < BRICK_ENTITY_CAPACITY ? &g_runtime[track] : NULL; }
uint8_t track_runtime_is_ui_ensemble_available(uint8_t track, track_runtime_ui_ensemble_t ensemble) { (void)ensemble; return track < TRACK_COUNT ? 1U : 0U; }
uint8_t track_runtime_supports_vca_gate(const track_runtime_ctx_t *ctx) { return ctx != NULL ? 1U : 0U; }
track_runtime_family_t track_runtime_family_from_ui(track_family_t family) { return family == TRACK_FAMILY_SYNTH ? TRACK_RUNTIME_FAMILY_SYNTH : TRACK_RUNTIME_FAMILY_OTHER; }
track_runtime_type_t track_runtime_type_from_ui(track_type_t type) { return type == TRACK_TYPE_PRISM ? TRACK_RUNTIME_TYPE_PRISM : TRACK_RUNTIME_TYPE_OTHER; }
uint8_t track_runtime_has_configurable_polyphony(track_runtime_family_t family, track_runtime_type_t type) { (void)family; (void)type; return 0U; }
track_runtime_param_rule_t track_runtime_get_param_rule(param_id_t param)
{
    (void)param;
    return (track_runtime_param_rule_t){ TRACK_RUNTIME_PARAM_DOMAIN_TONE,
        TRACK_RUNTIME_RESOURCE_NONE, TRACK_RUNTIME_PARAM_ALLOWED };
}
track_runtime_param_status_t track_runtime_get_effective_param_status(uint8_t track, param_id_t param)
{ (void)param; return track < TRACK_COUNT ? TRACK_RUNTIME_PARAM_ALLOWED : TRACK_RUNTIME_PARAM_UNAVAILABLE; }
uint8_t track_runtime_get_descriptor(uint8_t track, track_runtime_descriptor_t *out)
{
    if (!out || track >= TRACK_COUNT) return 0U;
    memset(out, 0, sizeof(*out)); out->family = TRACK_RUNTIME_FAMILY_SYNTH;
    out->type = TRACK_RUNTIME_TYPE_PRISM; out->engine = TRACK_RUNTIME_ENGINE_PRISM; out->active = 1U; return 1U;
}
uint8_t track_runtime_resolve_track(uint8_t track, track_runtime_resolved_track_t *out)
{
    if (!out || !track_runtime_get_descriptor(track, &out->descriptor)) return 0U;
    out->track_id = track; out->supports_vca_gate = 1U; return 1U;
}

bool track_catalog_family_is_engine(track_family_t family) { return family == TRACK_FAMILY_SYNTH; }
bool track_catalog_type_is_valid_for_family(track_family_t family, track_type_t type) { return family == TRACK_FAMILY_SYNTH && type == TRACK_TYPE_PRISM; }
bool track_catalog_type_is_available(uint8_t track, track_family_t family, track_type_t type, const track_config_t configs[BRICK_ENTITY_CAPACITY]) { (void)track; (void)configs; return track_catalog_type_is_valid_for_family(family, type); }
bool track_catalog_family_is_available(uint8_t track, track_family_t family, const track_config_t configs[BRICK_ENTITY_CAPACITY]) { (void)track; (void)configs; return family == TRACK_FAMILY_SYNTH; }
uint8_t track_catalog_type_count_for_family(track_family_t family, uint8_t track, const track_config_t configs[BRICK_ENTITY_CAPACITY]) { (void)track; (void)configs; return family == TRACK_FAMILY_SYNTH ? 1U : 0U; }
track_type_t track_catalog_type_at(track_family_t family, uint8_t index) { return family == TRACK_FAMILY_SYNTH && index == 0U ? TRACK_TYPE_PRISM : TRACK_TYPE_NONE; }
track_type_t track_catalog_first_available_type(track_family_t family, uint8_t track, const track_config_t configs[BRICK_ENTITY_CAPACITY]) { (void)track; (void)configs; return track_catalog_default_type_for_family(family); }
track_type_t track_catalog_default_type_for_family(track_family_t family) { return family == TRACK_FAMILY_SYNTH ? TRACK_TYPE_PRISM : TRACK_TYPE_NONE; }
const char *track_catalog_family_short_name(track_family_t family) { return family == TRACK_FAMILY_SYNTH ? "SYN" : "OFF"; }

uint8_t entity_topology_get(brick_entity_id_t entity, entity_topology_descriptor_t *out)
{
    if (out == NULL || entity >= BRICK_ENTITY_CAPACITY) return 0U;
    memset(out, 0, sizeof(*out));
    out->entity_id = entity;
    out->parent_entity_id = entity;
    out->role = ENTITY_ROLE_MAIN;
    out->active = 1U;
    return 1U;
}
uint8_t entity_topology_is_active(brick_entity_id_t entity) { return entity < BRICK_ENTITY_CAPACITY ? 1U : 0U; }

void seq_runtime_toggle_play_stop(void) { g_transport_running ^= 1U; }
uint8_t seq_runtime_is_running(void) { return g_transport_running; }
uint32_t seq_runtime_get_tempo_bpm_milli(void) { return g_tempo_milli; }
void seq_runtime_set_tempo_bpm_milli(uint32_t tempo) { if (tempo >= 20000U && tempo <= 300000U) g_tempo_milli = tempo; }

uint8_t tone_program_control_get(uint8_t track, param_id_t id, float *out)
{
    if (track >= BRICK_ENTITY_CAPACITY || id >= PARAM_COUNT || out == NULL) return 0U;
    *out = g_parameters[track][id]; return 1U;
}
uint8_t tone_program_control_set(uint8_t track, param_id_t id, float value)
{
    if (track >= BRICK_ENTITY_CAPACITY || id >= PARAM_COUNT) return 0U;
    g_parameters[track][id] = value; return 1U;
}

uint8_t seq_param_iface_param_is_supported(seq_track_id_t track, uint8_t set_id, param_id_t id)
{ (void)set_id; (void)id; return track < TRACK_COUNT ? 1U : 0U; }
uint8_t param_backend_apply_track_value_control(uint8_t track, param_id_t id, float value)
{ (void)value; return (track < TRACK_COUNT && id < PARAM_COUNT) ? 1U : 0U; }
