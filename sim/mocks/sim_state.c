#include <stdbool.h>
#include <string.h>

#include "Track/track_catalog.h"
#include "Track/track_runtime.h"
#include "Track/track_state.h"
#include "Track/entity_topology.h"
#include "Track/track_input_ownership.h"
#include "Seq/seq_param_iface.h"
#include "Mod/mod_lfo_v1_control.h"

static track_config_t g_tracks[BRICK_ENTITY_CAPACITY];
static track_runtime_ctx_t g_runtime[BRICK_ENTITY_CAPACITY];
static uint8_t g_midi_channel[BRICK_ENTITY_CAPACITY];
static uint8_t g_midi_source[BRICK_ENTITY_CAPACITY];
static uint8_t g_external_input[TRACK_COUNT];
static uint8_t g_ready;
static uint8_t g_transport_running;
static uint32_t g_tempo_milli = 120000U;
static float g_parameters[BRICK_ENTITY_CAPACITY][PARAM_COUNT];
static mod_lfo_control_bank_t g_lfo[BRICK_ENTITY_CAPACITY];

static void ensure_state(void)
{
    if (g_ready) return;
    for (uint8_t i = 0U; i < BRICK_ENTITY_CAPACITY; ++i)
    {
        const uint8_t top_level = (uint8_t)(i < BRICK_ENTITY_TOP_LEVEL_COUNT);
        g_tracks[i].family = top_level ? TRACK_FAMILY_SYNTH : TRACK_FAMILY_OFF;
        g_tracks[i].type = top_level ? TRACK_TYPE_PRISM : TRACK_TYPE_NONE;
        g_midi_channel[i] = (uint8_t)((i % 16U) + 1U);
        g_runtime[i].family = top_level ? TRACK_RUNTIME_FAMILY_SYNTH : TRACK_RUNTIME_FAMILY_OFF;
        g_runtime[i].type = top_level ? TRACK_RUNTIME_TYPE_PRISM : TRACK_RUNTIME_TYPE_NONE;
        g_runtime[i].midi_channel_1_16 = g_midi_channel[i];
    }
    g_tracks[BRICK_ENTITY_MASTER_ID].family = TRACK_FAMILY_OFF;
    g_tracks[BRICK_ENTITY_MASTER_ID].type = TRACK_TYPE_NONE;
    g_runtime[BRICK_ENTITY_MASTER_ID].family = TRACK_RUNTIME_FAMILY_OFF;
    g_runtime[BRICK_ENTITY_MASTER_ID].type = TRACK_RUNTIME_TYPE_NONE;
    g_ready = 1U;
}

static void sync_runtime(uint8_t entity)
{
    const track_family_t family = g_tracks[entity].family;
    const track_type_t type = g_tracks[entity].type;
    g_runtime[entity].family = (uint8_t)track_runtime_family_from_ui_config(family, type);
    g_runtime[entity].type = (uint8_t)track_runtime_type_from_ui(type);
    g_runtime[entity].midi_channel_1_16 = g_midi_channel[entity];
    g_runtime[entity].midi_source = g_midi_source[entity];
}

const track_config_t *track_state_get_configs(void) { ensure_state(); return g_tracks; }
track_config_t track_state_get_config(uint8_t track) { ensure_state(); return track < BRICK_ENTITY_CAPACITY ? g_tracks[track] : (track_config_t){0}; }
track_family_t track_state_get_family(uint8_t track) { return track_state_get_config(track).family; }
track_type_t track_state_get_type(uint8_t track) { return track_state_get_config(track).type; }
uint8_t track_state_get_midi_channel(uint8_t track) { ensure_state(); return track < BRICK_ENTITY_CAPACITY ? g_midi_channel[track] : 1U; }
track_midi_source_t track_state_get_midi_source(uint8_t track) { ensure_state(); return track < BRICK_ENTITY_CAPACITY ? (track_midi_source_t)g_midi_source[track] : TRACK_MIDI_SOURCE_INTERNAL; }
uint8_t track_state_get_external_input(uint8_t track) { return track < TRACK_COUNT ? g_external_input[track] : 0U; }
bool track_state_set_track_midi_channel(uint8_t track, uint8_t channel) { ensure_state(); if (track >= BRICK_ENTITY_CAPACITY || channel < 1U || channel > 16U) return false; g_midi_channel[track] = channel; sync_runtime(track); return true; }
bool track_state_set_track_midi_source(uint8_t track, track_midi_source_t source) { ensure_state(); if (track >= BRICK_ENTITY_CAPACITY || source >= TRACK_MIDI_SOURCE_COUNT) return false; g_midi_source[track] = (uint8_t)source; sync_runtime(track); return true; }
bool track_state_set_external_input(uint8_t track, uint8_t input) { ensure_state(); if (!track_input_ownership_can_claim(track, input)) return false; g_external_input[track] = input; return true; }
uint8_t track_state_count_tracks_with_family(track_family_t family) { ensure_state(); uint8_t n=0U; for(uint8_t i=0U;i<TRACK_COUNT;++i) if(g_tracks[i].family==family)++n; return n; }

bool track_structure_apply_entity_bulk_with_inputs(
    const uint8_t family[BRICK_ENTITY_CAPACITY],
    const uint8_t type[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_channel[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_source[BRICK_ENTITY_CAPACITY],
    const uint8_t external_input[TRACK_COUNT])
{
    ensure_state();
    if (!family || !type || !midi_channel || !midi_source || !external_input) return false;
    track_config_t proposed[BRICK_ENTITY_CAPACITY];
    for (uint8_t i = 0U; i < BRICK_ENTITY_CAPACITY; ++i)
    {
        if (family[i] >= TRACK_FAMILY_COUNT || type[i] >= TRACK_TYPE_COUNT
                || midi_channel[i] < 1U || midi_channel[i] > 16U
                || midi_source[i] >= TRACK_MIDI_SOURCE_COUNT) return false;
        proposed[i].family = (track_family_t)family[i];
        proposed[i].type = (track_type_t)type[i];
        if (i == BRICK_ENTITY_MASTER_ID)
        {
            if (proposed[i].family != TRACK_FAMILY_OFF || proposed[i].type != TRACK_TYPE_NONE) return false;
        }
        else if (proposed[i].family == TRACK_FAMILY_OFF)
        {
            if (proposed[i].type != TRACK_TYPE_NONE) return false;
        }
        else if (!track_catalog_type_is_valid_for_family(proposed[i].family, proposed[i].type)) return false;
    }
    if (!track_input_ownership_validate_bulk(proposed, external_input)) return false;
    memcpy(g_tracks, proposed, sizeof(g_tracks));
    memcpy(g_midi_channel, midi_channel, sizeof(g_midi_channel));
    memcpy(g_midi_source, midi_source, sizeof(g_midi_source));
    memcpy(g_external_input, external_input, sizeof(g_external_input));
    for (uint8_t i = 0U; i < BRICK_ENTITY_CAPACITY; ++i) sync_runtime(i);
    return true;
}

const track_runtime_ctx_t *track_runtime_get_ctx(uint8_t track) { ensure_state(); return track < BRICK_ENTITY_CAPACITY ? &g_runtime[track] : NULL; }
uint8_t track_runtime_is_ui_ensemble_available(uint8_t track, track_runtime_ui_ensemble_t ensemble) { (void)ensemble; return track < TRACK_COUNT ? 1U : 0U; }
uint8_t track_runtime_supports_vca_gate(const track_runtime_ctx_t *ctx) { return ctx != NULL ? 1U : 0U; }
track_runtime_family_t track_runtime_family_from_ui(track_family_t family)
{
    switch (family) {
        case TRACK_FAMILY_OFF: return TRACK_RUNTIME_FAMILY_OFF;
        case TRACK_FAMILY_SYNTH: return TRACK_RUNTIME_FAMILY_SYNTH;
        case TRACK_FAMILY_SAMPLER: return TRACK_RUNTIME_FAMILY_SAMPLER;
        case TRACK_FAMILY_DRUM: return TRACK_RUNTIME_FAMILY_DRUM;
        case TRACK_FAMILY_MIDI: return TRACK_RUNTIME_FAMILY_MIDI;
        case TRACK_FAMILY_EXTERNAL: return TRACK_RUNTIME_FAMILY_EXTERNAL;
        default: return TRACK_RUNTIME_FAMILY_OTHER;
    }
}
track_runtime_family_t track_runtime_family_from_ui_config(track_family_t family, track_type_t type)
{ return type == TRACK_TYPE_GROUP ? TRACK_RUNTIME_FAMILY_OTHER : track_runtime_family_from_ui(family); }
track_runtime_type_t track_runtime_type_from_ui(track_type_t type)
{
    switch (type) {
        case TRACK_TYPE_NONE: return TRACK_RUNTIME_TYPE_NONE;
        case TRACK_TYPE_PRISM: return TRACK_RUNTIME_TYPE_PRISM;
        case TRACK_TYPE_DRUM_MD: return TRACK_RUNTIME_TYPE_DRUM_MD;
        case TRACK_TYPE_MIDI: return TRACK_RUNTIME_TYPE_MIDI;
        case TRACK_TYPE_STREAM: return TRACK_RUNTIME_TYPE_STREAM;
        case TRACK_TYPE_MULTI: return TRACK_RUNTIME_TYPE_MULTI;
        case TRACK_TYPE_STACK: return TRACK_RUNTIME_TYPE_STACK;
        case TRACK_TYPE_WAVE: return TRACK_RUNTIME_TYPE_WAVE;
        case TRACK_TYPE_EXTERNAL: return TRACK_RUNTIME_TYPE_EXTERNAL;
        case TRACK_TYPE_GROUP: return TRACK_RUNTIME_TYPE_GROUP;
        case TRACK_TYPE_FM: return TRACK_RUNTIME_TYPE_FM;
        case TRACK_TYPE_TB303: return TRACK_RUNTIME_TYPE_TB303;
        case TRACK_TYPE_ACID: return TRACK_RUNTIME_TYPE_ACID;
        default: return TRACK_RUNTIME_TYPE_OTHER;
    }
}
track_runtime_engine_t track_runtime_choose_engine(track_runtime_family_t family, track_runtime_type_t type)
{
    if (family == TRACK_RUNTIME_FAMILY_EXTERNAL && type == TRACK_RUNTIME_TYPE_EXTERNAL) return TRACK_RUNTIME_ENGINE_AUDIO_TRACK;
    if (family == TRACK_RUNTIME_FAMILY_DRUM && type == TRACK_RUNTIME_TYPE_DRUM_MD) return TRACK_RUNTIME_ENGINE_DRUM;
    if (family == TRACK_RUNTIME_FAMILY_SAMPLER && (type == TRACK_RUNTIME_TYPE_STREAM || type == TRACK_RUNTIME_TYPE_MULTI)) return TRACK_RUNTIME_ENGINE_SAMPLER;
    if (family != TRACK_RUNTIME_FAMILY_SYNTH) return TRACK_RUNTIME_ENGINE_NONE;
    switch (type) {
        case TRACK_RUNTIME_TYPE_PRISM: return TRACK_RUNTIME_ENGINE_PRISM;
        case TRACK_RUNTIME_TYPE_STACK: return TRACK_RUNTIME_ENGINE_STACK;
        case TRACK_RUNTIME_TYPE_WAVE: return TRACK_RUNTIME_ENGINE_WAVE;
        case TRACK_RUNTIME_TYPE_FM: return TRACK_RUNTIME_ENGINE_FM;
        case TRACK_RUNTIME_TYPE_TB303: return TRACK_RUNTIME_ENGINE_TB303;
        case TRACK_RUNTIME_TYPE_ACID: return TRACK_RUNTIME_ENGINE_ACID;
        default: return TRACK_RUNTIME_ENGINE_NONE;
    }
}
uint8_t track_runtime_has_configurable_polyphony(track_runtime_family_t family, track_runtime_type_t type)
{ return (uint8_t)((family == TRACK_RUNTIME_FAMILY_SAMPLER && type == TRACK_RUNTIME_TYPE_MULTI)
    || (family == TRACK_RUNTIME_FAMILY_SYNTH && (type == TRACK_RUNTIME_TYPE_PRISM || type == TRACK_RUNTIME_TYPE_STACK || type == TRACK_RUNTIME_TYPE_WAVE || type == TRACK_RUNTIME_TYPE_FM))); }
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
    ensure_state();
    if (!out || track >= BRICK_ENTITY_CAPACITY) return 0U;
    memset(out, 0, sizeof(*out));
    out->family = (track_runtime_family_t)g_runtime[track].family;
    out->type = (track_runtime_type_t)g_runtime[track].type;
    out->engine = track_runtime_choose_engine(out->family, out->type);
    out->active = entity_topology_is_active(track);
    out->midi_channel_1_16 = g_midi_channel[track];
    out->ui_ensemble_mask = (uint16_t)((1U << TRACK_RUNTIME_UI_ENSEMBLE_COUNT) - 1U);
    return 1U;
}
uint8_t track_runtime_resolve_track(uint8_t track, track_runtime_resolved_track_t *out)
{
    if (!out || !track_runtime_get_descriptor(track, &out->descriptor)) return 0U;
    out->track_id = track;
    out->has_mix_target = 1U; out->mix_track_id = track;
    out->has_filter_target = 1U; out->filter_track_id = track;
    out->supports_vca_gate = 1U; return 1U;
}

uint8_t track_input_ownership_get_external_input(uint8_t track)
{ ensure_state(); return track < TRACK_COUNT ? g_external_input[track] : 0U; }
uint8_t track_input_ownership_get_external_owner(uint8_t input, uint8_t *out_track)
{
    ensure_state(); if (!out_track || input >= TRACK_AUDIO_INPUT_RESOURCE_COUNT) return 0U;
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track)
        if (g_tracks[track].family == TRACK_FAMILY_EXTERNAL && g_external_input[track] == input)
        { *out_track = track; return 1U; }
    *out_track = TRACK_INPUT_OWNER_NONE; return 1U;
}
uint8_t track_input_ownership_can_claim(uint8_t track, uint8_t input)
{
    uint8_t owner = TRACK_INPUT_OWNER_NONE;
    return (uint8_t)(track < TRACK_COUNT && input < TRACK_AUDIO_INPUT_RESOURCE_COUNT
        && track_input_ownership_get_external_owner(input, &owner)
        && (owner == TRACK_INPUT_OWNER_NONE || owner == track));
}
uint8_t track_input_ownership_any_available(uint8_t excluding_track)
{ uint8_t input; return track_input_ownership_first_available(excluding_track, &input); }
uint8_t track_input_ownership_first_available(uint8_t excluding_track, uint8_t *out_input)
{
    if (!out_input) return 0U;
    for (uint8_t input = 0U; input < TRACK_AUDIO_INPUT_RESOURCE_COUNT; ++input)
        if (track_input_ownership_can_claim(excluding_track, input)) { *out_input = input; return 1U; }
    return 0U;
}
uint8_t track_input_ownership_validate_bulk(const track_config_t configs[TRACK_COUNT], const uint8_t external_input[TRACK_COUNT])
{
    if (!configs || !external_input) return 0U;
    uint8_t used = 0U;
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) if (configs[track].family == TRACK_FAMILY_EXTERNAL)
    {
        const uint8_t input = external_input[track];
        if (input >= TRACK_AUDIO_INPUT_RESOURCE_COUNT || (used & (uint8_t)(1U << input))) return 0U;
        used |= (uint8_t)(1U << input);
    }
    return 1U;
}

void mod_lfo_v1_make_default(mod_lfo_control_bank_t *out) { if (out) memset(out, 0, sizeof(*out)); }
void mod_lfo_v1_init(void) { memset(g_lfo, 0, sizeof(g_lfo)); }
uint8_t mod_lfo_v1_reset_track(uint8_t track) { if(track>=BRICK_ENTITY_CAPACITY)return 0U;memset(&g_lfo[track],0,sizeof(g_lfo[track]));return 1U; }
uint8_t mod_lfo_v1_prepare_track_param(uint8_t track,uint8_t lfo,mod_lfo_param_t param,float value,uint8_t *owner,float *canonical)
{ if(track>=BRICK_ENTITY_CAPACITY||lfo>=MOD_LFO_COUNT_PER_TRACK||(uint8_t)param>=MOD_LFO_PARAM_COUNT||!owner||!canonical)return 0U;*owner=track;*canonical=value;return 1U; }
uint8_t mod_lfo_v1_install_prepared_track_param(uint8_t owner,uint8_t lfo,mod_lfo_param_t param,float value)
{ if(owner>=BRICK_ENTITY_CAPACITY||lfo>=MOD_LFO_COUNT_PER_TRACK||(uint8_t)param>=MOD_LFO_PARAM_COUNT)return 0U;(&g_lfo[owner].lfo[lfo].rate)[(uint8_t)param]=value;return 1U; }
uint8_t mod_lfo_v1_get_track_param(uint8_t track,uint8_t lfo,mod_lfo_param_t param,float *out)
{ if(track>=BRICK_ENTITY_CAPACITY||lfo>=MOD_LFO_COUNT_PER_TRACK||(uint8_t)param>=MOD_LFO_PARAM_COUNT||!out)return 0U;*out=(&g_lfo[track].lfo[lfo].rate)[(uint8_t)param];return 1U; }
uint8_t mod_lfo_v1_restore_track(uint8_t track,const mod_lfo_control_bank_t *state)
{ if(track>=BRICK_ENTITY_CAPACITY||!state)return 0U;g_lfo[track]=*state;return 1U; }
uint8_t mod_lfo_v1_prepare_bank(const mod_lfo_control_bank_t *state,mod_lfo_control_bank_t *out)
{ if(!state||!out)return 0U;*out=*state;return 1U; }
uint8_t mod_lfo_v1_shape_is_random(uint8_t track,uint8_t lfo){(void)track;(void)lfo;return 0U;}
void mod_lfo_v1_invalidate_dest_cache_track(uint8_t track){(void)track;}
void mod_lfo_v1_invalidate_dest_cache_all(void){}

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
