#include "Mod/mod_env3.h"

#include <stdbool.h>
#include <math.h>
#include <string.h>

#include "Audio/env_adsr.h"
#include "Core/track_sound_state.h"
#include "Core/entity_topology.h"
#include "Param/param_filter.h"
#include "Seq/seq_types.h"
#include "Mod/mod_matrix.h"

/* GROUP ENV3 configuration is owned by its master entity. */
#undef SEQ_TRACK_COUNT
#define SEQ_TRACK_COUNT SEQ_LANE_CAPACITY

#define MOD_ENV3_AUDIO_SAMPLE_RATE 48000.0f

typedef struct
{
    env_adsr_t env;
    uint8_t held_notes;
    uint8_t temp_valid;
    track_mod_env3_state_t applied;
    track_mod_env3_state_t temp;
} mod_env3_runtime_track_t;

static mod_env3_runtime_track_t g_mod_env3_runtime[SEQ_TRACK_COUNT];
static track_mod_env3_state_t g_mod_env3_audio_config[SEQ_TRACK_COUNT];
static uint8_t g_mod_env3_audio_retrigger[SEQ_TRACK_COUNT];

static float mod_env3_clampf(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

static uint16_t mod_env3_seconds_to_u16(float seconds)
{
    const float normalized = mod_env3_clampf(seconds, 0.0f, 30.0f) / 30.0f;
    const float shaped = cbrtf(mod_env3_clampf(normalized, 0.0f, 1.0f));
    return (uint16_t)(shaped * 65535.0f + 0.5f);
}

static uint16_t mod_env3_sustain_to_u15(float value)
{
    return (uint16_t)(mod_env3_clampf(value, 0.0f, 127.0f) * (32767.0f / 127.0f));
}

static track_mod_env3_state_t *mod_env3_track_settings(uint8_t track)
{
    track_sound_state_t *const sound = track_sound_state_get(track);
    if (sound == NULL)
    {
        return NULL;
    }
    return &sound->mod_env3;
}

static const track_mod_env3_state_t *mod_env3_track_settings_const(uint8_t track)
{
    const track_sound_state_t *const sound = track_sound_state_get_const(track);
    if (sound == NULL)
    {
        return NULL;
    }
    return &sound->mod_env3;
}

uint8_t mod_env3_audio_config_get(uint8_t track,
                                  modulation_env3_publication_t *out)
{
    if ((track >= SEQ_TRACK_COUNT) || (out == NULL))
    {
        return 0U;
    }
    const track_mod_env3_state_t *const config = &g_mod_env3_audio_config[track];
    out->attack = config->attack;
    out->decay = config->decay;
    out->sustain = config->sustain;
    out->release = config->release;
    out->retrigger_hard = g_mod_env3_audio_retrigger[track];
    memset(out->reserved, 0, sizeof(out->reserved));
    return 1U;
}

void mod_env3_audio_apply_config(uint8_t track,
                                 const modulation_env3_publication_t *config)
{
    if ((track >= SEQ_TRACK_COUNT) || (config == NULL))
    {
        return;
    }
    g_mod_env3_audio_config[track] = (track_mod_env3_state_t){
        .attack = config->attack,
        .decay = config->decay,
        .sustain = config->sustain,
        .release = config->release
    };
    g_mod_env3_audio_retrigger[track] = config->retrigger_hard;
    g_mod_env3_runtime[track].applied.attack = -1.0f;
    g_mod_env3_runtime[track].applied.decay = -1.0f;
    g_mod_env3_runtime[track].applied.sustain = -1.0f;
    g_mod_env3_runtime[track].applied.release = -1.0f;
}

static void mod_env3_apply_settings(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    mod_env3_runtime_track_t *const rt = &g_mod_env3_runtime[track];
    const track_mod_env3_state_t *const s = (rt->temp_valid != 0U)
        ? &rt->temp : &g_mod_env3_audio_config[track];
    if (memcmp(&rt->applied, s, sizeof(rt->applied)) == 0)
    {
        return;
    }

    env_adsr_set_attack(&rt->env, mod_env3_seconds_to_u16(param_filter_ui127_to_attack_s(s->attack)));
    env_adsr_set_decay(&rt->env, mod_env3_seconds_to_u16(param_filter_ui127_to_decay_s(s->decay)));
    env_adsr_set_sustain(&rt->env, mod_env3_sustain_to_u15(s->sustain));
    env_adsr_set_release(&rt->env, mod_env3_seconds_to_u16(param_filter_ui127_to_release_s(s->release)));
    rt->applied = *s;
}

void mod_env3_init(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        env_adsr_init(&g_mod_env3_runtime[track].env, MOD_ENV3_AUDIO_SAMPLE_RATE);
        g_mod_env3_runtime[track].held_notes = 0U;
        g_mod_env3_runtime[track].temp_valid = 0U;
        g_mod_env3_runtime[track].applied.attack = -1.0f;
        g_mod_env3_runtime[track].applied.decay = -1.0f;
        g_mod_env3_runtime[track].applied.sustain = -1.0f;
        g_mod_env3_runtime[track].applied.release = -1.0f;
        mod_env3_apply_settings(track);
    }
}

void mod_env3_reset_runtime(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        env_adsr_reset(&g_mod_env3_runtime[track].env);
        g_mod_env3_runtime[track].held_notes = 0U;
        g_mod_env3_runtime[track].temp_valid = 0U;
        g_mod_env3_runtime[track].applied.attack = -1.0f;
        g_mod_env3_runtime[track].applied.decay = -1.0f;
        g_mod_env3_runtime[track].applied.sustain = -1.0f;
        g_mod_env3_runtime[track].applied.release = -1.0f;
        mod_env3_apply_settings(track);
    }
}

static uint8_t mod_env3_write_param(track_mod_env3_state_t *s, mod_env3_param_t param, float value)
{
    if ((s == NULL) || (param >= MOD_ENV3_PARAM_COUNT))
    {
        return 0U;
    }

    const float clamped = mod_env3_clampf(value, 0.0f, 127.0f);
    switch (param)
    {
        case MOD_ENV3_PARAM_ATTACK: s->attack = clamped; return 1U;
        case MOD_ENV3_PARAM_DECAY: s->decay = clamped; return 1U;
        case MOD_ENV3_PARAM_SUSTAIN: s->sustain = clamped; return 1U;
        case MOD_ENV3_PARAM_RELEASE: s->release = clamped; return 1U;
        default: return 0U;
    }
}

uint8_t mod_env3_set_track_param(uint8_t track, mod_env3_param_t param, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= MOD_ENV3_PARAM_COUNT))
    {
        return 0U;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    track_mod_env3_state_t *const s = mod_env3_track_settings(track);
    if (s == NULL)
    {
        return 0U;
    }

    const uint8_t ok = mod_env3_write_param(s, param, value);
    if (ok != 0U)
    {
        mod_matrix_publish_control_snapshot(track);
    }
    return ok;
}

uint8_t mod_env3_get_track_param(uint8_t track, mod_env3_param_t param, float *out_value)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= MOD_ENV3_PARAM_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    const track_mod_env3_state_t *const s = mod_env3_track_settings_const(track);
    if (s == NULL)
    {
        return 0U;
    }

    switch (param)
    {
        case MOD_ENV3_PARAM_ATTACK: *out_value = s->attack; return 1U;
        case MOD_ENV3_PARAM_DECAY: *out_value = s->decay; return 1U;
        case MOD_ENV3_PARAM_SUSTAIN: *out_value = s->sustain; return 1U;
        case MOD_ENV3_PARAM_RELEASE: *out_value = s->release; return 1U;
        default: return 0U;
    }
}

uint8_t mod_env3_set_track_retrigger_hard(uint8_t track, float value)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    track_sound_state_t *const sound = track_sound_state_get(track);
    if (sound == NULL)
    {
        return 0U;
    }

    sound->env_retrig_mod = (value >= 0.5f) ? 1.0f : 0.0f;
    mod_matrix_publish_control_snapshot(track);
    return 1U;
}

uint8_t mod_env3_get_track_retrigger_hard(uint8_t track, float *out_value)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    const track_sound_state_t *const sound = track_sound_state_get_const(track);
    if (sound == NULL)
    {
        return 0U;
    }

    *out_value = sound->env_retrig_mod;
    return 1U;
}

uint8_t mod_env3_apply_track_param_temp(uint8_t track, mod_env3_param_t param, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= MOD_ENV3_PARAM_COUNT))
    {
        return 0U;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    mod_env3_runtime_track_t *const rt = &g_mod_env3_runtime[track];
    if (rt->temp_valid == 0U)
    {
        const track_mod_env3_state_t *const base = &g_mod_env3_audio_config[track];
        if (base == NULL)
        {
            return 0U;
        }
        rt->temp = *base;
        rt->temp_valid = 1U;
    }

    if (mod_env3_write_param(&rt->temp, param, value) == 0U)
    {
        return 0U;
    }

    mod_env3_apply_settings(track);
    return 1U;
}

void mod_env3_clear_track_param_temp(uint8_t track, mod_env3_param_t param)
{
    (void)param;
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return;
    track = owner;
    g_mod_env3_runtime[track].temp_valid = 0U;
    mod_env3_apply_settings(track);
}

void mod_env3_note_on(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    mod_env3_apply_settings(track);
    if (g_mod_env3_runtime[track].held_notes < 255U)
    {
        g_mod_env3_runtime[track].held_notes++;
    }
    env_adsr_retrigger(&g_mod_env3_runtime[track].env,
                       g_mod_env3_audio_retrigger[track] != 0U);
}

void mod_env3_note_off(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (g_mod_env3_runtime[track].held_notes > 0U)
    {
        g_mod_env3_runtime[track].held_notes--;
    }
    if (g_mod_env3_runtime[track].held_notes == 0U)
    {
        env_adsr_gate_off(&g_mod_env3_runtime[track].env);
    }
}

void mod_env3_all_notes_off(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_mod_env3_runtime[track].held_notes = 0U;
    env_adsr_gate_off(&g_mod_env3_runtime[track].env);
}

float mod_env3_process_track(uint8_t track, uint32_t elapsed_frames)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0.0f;
    }

    mod_env3_apply_settings(track);
    const int16_t value = env_adsr_value(&g_mod_env3_runtime[track].env);
    (void)env_adsr_process_advance(
        &g_mod_env3_runtime[track].env,
        elapsed_frames,
        NULL);
    return (float)value * (1.0f / 32767.0f);
}

uint8_t mod_env3_is_running(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    return (env_adsr_stage(&g_mod_env3_runtime[track].env) != ENV_ADSR_PEAKS_STAGE_IDLE) ? 1U : 0U;
}
