#include "Mod/mod_env3.h"

#include <stdbool.h>
#include <math.h>
#include <string.h>

#include "stm32h7xx.h"
#include "Audio/env_adsr.h"
#include "Core/track_sound_state.h"
#include "Core/entity_topology.h"
#include "Audio/audio_modulation_projection.h"
#include "Param/param_filter.h"
#include "Seq/seq_types.h"
#include "Storage/memory_layout.h"

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
static float g_mod_env3_audio_retrigger[SEQ_TRACK_COUNT];

typedef struct
{
    track_mod_env3_state_t config;
    float retrigger_hard;
    uint8_t reset_runtime;
    uint8_t reserved[3];
} mod_env3_control_snapshot_t;

typedef struct
{
    volatile uint32_t sequence;
    mod_env3_control_snapshot_t snapshot;
} mod_env3_control_mailbox_t;

CTRL_STATE static mod_env3_control_mailbox_t
    g_mod_env3_control_mailbox[SEQ_TRACK_COUNT];
static volatile uint32_t g_mod_env3_audio_mailbox_sequence[SEQ_TRACK_COUNT];
static uint8_t g_mod_env3_audio_initialized;

static const track_mod_env3_state_t *mod_env3_track_settings_const(uint8_t track);

/* All public ENV3 entry points accept an entity ID.  Runtime state is owned
 * by the modulation owner, not by a GROUP child lane. */
static uint8_t mod_env3_control_resolve_owner(uint8_t track, uint8_t *out_owner)
{
    brick_entity_id_t owner = track;
    if ((out_owner == NULL)
            || (track >= SEQ_TRACK_COUNT)
            || (entity_topology_mod_owner(track, &owner) == 0U)
            || (owner >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }
    *out_owner = (uint8_t)owner;
    return 1U;
}

static uint8_t mod_env3_audio_resolve_owner(uint8_t track, uint8_t *out_owner)
{
    return audio_modulation_projection_audio_resolve_owner(track, out_owner);
}

static void mod_env3_audio_apply_config(uint8_t track,
                                        const track_mod_env3_state_t *config,
                                        float retrigger_hard)
{
    if ((track >= SEQ_TRACK_COUNT) || (config == NULL))
    {
        return;
    }
    g_mod_env3_audio_config[track] = *config;
    g_mod_env3_audio_retrigger[track] = (retrigger_hard >= 0.5f) ? 1.0f : 0.0f;
}

void mod_env3_audio_apply_retrigger(uint8_t track, float value)
{
    uint8_t owner = 0U;
    if (mod_env3_audio_resolve_owner(track, &owner) != 0U)
    {
        g_mod_env3_audio_retrigger[owner] = (value >= 0.5f) ? 1.0f : 0.0f;
    }
}

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

void mod_env3_control_publish_snapshot_track(uint8_t track, uint8_t reset_runtime)
{
    uint8_t owner = 0U;
    if (mod_env3_control_resolve_owner(track, &owner) == 0U)
    {
        return;
    }

    const track_sound_state_t *const sound = track_sound_state_get_const(owner);
    if (sound == NULL)
    {
        return;
    }

    mod_env3_control_mailbox_t *const mailbox = &g_mod_env3_control_mailbox[owner];
    mod_env3_control_snapshot_t snapshot = {
        .config = sound->mod_env3,
        .retrigger_hard = sound->env_retrig_mod,
        .reset_runtime = (reset_runtime != 0U) ? 1U : 0U,
        .reserved = { 0U, 0U, 0U }
    };

    uint32_t sequence = mailbox->sequence;
    if ((sequence & 1U) != 0U)
    {
        ++sequence;
    }
    mailbox->sequence = sequence + 1U;
    __DMB();
    mailbox->snapshot = snapshot;
    __DMB();
    mailbox->sequence = sequence + 2U;
}

void mod_env3_control_publish_snapshot_all(uint8_t reset_runtime)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        brick_entity_id_t owner = track;
        if ((entity_topology_mod_owner(track, &owner) != 0U)
                && (owner == track))
        {
            mod_env3_control_publish_snapshot_track(track, reset_runtime);
        }
    }
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

static void mod_env3_audio_invalidate_applied(uint8_t track)
{
    g_mod_env3_runtime[track].applied.attack = -1.0f;
    g_mod_env3_runtime[track].applied.decay = -1.0f;
    g_mod_env3_runtime[track].applied.sustain = -1.0f;
    g_mod_env3_runtime[track].applied.release = -1.0f;
}

static void mod_env3_audio_reset_track_runtime(uint8_t track)
{
    env_adsr_reset(&g_mod_env3_runtime[track].env);
    g_mod_env3_runtime[track].held_notes = 0U;
    g_mod_env3_runtime[track].temp_valid = 0U;
    mod_env3_audio_invalidate_applied(track);
}

static void mod_env3_audio_apply_control_snapshot(
    uint8_t track,
    const mod_env3_control_snapshot_t *snapshot)
{
    if ((snapshot == NULL) || (track >= SEQ_TRACK_COUNT))
    {
        return;
    }

    if (snapshot->reset_runtime != 0U)
    {
        mod_env3_audio_reset_track_runtime(track);
    }
    mod_env3_audio_apply_config(track, &snapshot->config,
                                snapshot->retrigger_hard);
    mod_env3_apply_settings(track);
}

static void mod_env3_audio_init(void)
{
    memset(g_mod_env3_audio_config, 0, sizeof(g_mod_env3_audio_config));
    memset(g_mod_env3_audio_retrigger, 0, sizeof(g_mod_env3_audio_retrigger));
    memset(g_mod_env3_runtime, 0, sizeof(g_mod_env3_runtime));
    memset((void *)g_mod_env3_audio_mailbox_sequence, 0,
           sizeof(g_mod_env3_audio_mailbox_sequence));

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        env_adsr_init(&g_mod_env3_runtime[track].env,
                      MOD_ENV3_AUDIO_SAMPLE_RATE);
        mod_env3_audio_invalidate_applied(track);
    }
    g_mod_env3_audio_initialized = 1U;
}

void mod_env3_audio_consume_snapshots(void)
{
    if (g_mod_env3_audio_initialized == 0U)
    {
        mod_env3_audio_init();
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const mod_env3_control_mailbox_t *const mailbox =
            &g_mod_env3_control_mailbox[track];
        const uint32_t before = mailbox->sequence;
        if (((before & 1U) != 0U)
                || (before == g_mod_env3_audio_mailbox_sequence[track]))
        {
            continue;
        }
        __DMB();
        const mod_env3_control_snapshot_t snapshot = mailbox->snapshot;
        __DMB();
        const uint32_t after = mailbox->sequence;
        if ((before != after) || ((after & 1U) != 0U))
        {
            continue;
        }
        mod_env3_audio_apply_control_snapshot(track, &snapshot);
        g_mod_env3_audio_mailbox_sequence[track] = after;
    }
}

void mod_env3_init(void)
{
    memset(g_mod_env3_control_mailbox, 0, sizeof(g_mod_env3_control_mailbox));
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

uint8_t mod_env3_control_set_track_param(uint8_t track, mod_env3_param_t param, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= MOD_ENV3_PARAM_COUNT)) return 0U;
    uint8_t owner = 0U;
    if (mod_env3_control_resolve_owner(track, &owner) == 0U) return 0U;
    return mod_env3_write_param(mod_env3_track_settings(owner), param, value);
}

uint8_t mod_env3_audio_apply_track_param(uint8_t track, mod_env3_param_t param, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= MOD_ENV3_PARAM_COUNT)) return 0U;
    uint8_t owner = 0U;
    if (mod_env3_audio_resolve_owner(track, &owner) == 0U) return 0U;
    if (mod_env3_write_param(&g_mod_env3_audio_config[owner], param, value) == 0U) return 0U;
    mod_env3_apply_settings(owner);
    return 1U;
}

uint8_t mod_env3_get_track_param(uint8_t track, mod_env3_param_t param, float *out_value)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= MOD_ENV3_PARAM_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    uint8_t owner = 0U;
    if (mod_env3_control_resolve_owner(track, &owner) == 0U) return 0U;
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

uint8_t mod_env3_control_set_track_retrigger_hard(uint8_t track, float value)
{
    if (track >= SEQ_TRACK_COUNT) return 0U;
    uint8_t owner = 0U;
    if (mod_env3_control_resolve_owner(track, &owner) == 0U) return 0U;
    track_sound_state_t *const sound = track_sound_state_get(owner);
    if (sound == NULL) return 0U;
    sound->env_retrig_mod = (value >= 0.5f) ? 1.0f : 0.0f;
    return 1U;
}

uint8_t mod_env3_get_track_retrigger_hard(uint8_t track, float *out_value)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    uint8_t owner = 0U;
    if (mod_env3_control_resolve_owner(track, &owner) == 0U) return 0U;
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

    uint8_t owner = 0U;
    if (mod_env3_audio_resolve_owner(track, &owner) == 0U) return 0U;
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

uint8_t mod_env3_clear_track_param_temp_audio(uint8_t track, mod_env3_param_t param)
{
    (void)param;
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    uint8_t owner = 0U;
    if (mod_env3_audio_resolve_owner(track, &owner) == 0U) return 0U;
    track = owner;
    g_mod_env3_runtime[track].temp_valid = 0U;
    mod_env3_apply_settings(track);
    return 1U;
}

void mod_env3_note_on(uint8_t track)
{
    uint8_t owner = 0U;
    if (mod_env3_audio_resolve_owner(track, &owner) == 0U)
    {
        return;
    }
    track = owner;

    mod_env3_apply_settings(track);
    if (g_mod_env3_runtime[track].held_notes < 255U)
    {
        g_mod_env3_runtime[track].held_notes++;
    }
    env_adsr_retrigger(&g_mod_env3_runtime[track].env,
                       g_mod_env3_audio_retrigger[track] >= 0.5f);
}

void mod_env3_note_off(uint8_t track)
{
    uint8_t owner = 0U;
    if (mod_env3_audio_resolve_owner(track, &owner) == 0U)
    {
        return;
    }
    track = owner;

    if (g_mod_env3_runtime[track].held_notes > 0U)
    {
        g_mod_env3_runtime[track].held_notes--;
    }
    if (g_mod_env3_runtime[track].held_notes == 0U)
    {
        env_adsr_gate_off(&g_mod_env3_runtime[track].env);
    }
}

float mod_env3_process_track(uint8_t track, uint32_t elapsed_frames)
{
    uint8_t owner = 0U;
    if (mod_env3_audio_resolve_owner(track, &owner) == 0U)
    {
        return 0.0f;
    }
    track = owner;

    mod_env3_apply_settings(track);
    const int16_t value = env_adsr_value(&g_mod_env3_runtime[track].env);
    (void)env_adsr_process_advance(
        &g_mod_env3_runtime[track].env,
        elapsed_frames,
        NULL);
    return (float)value * (1.0f / 32767.0f);
}
