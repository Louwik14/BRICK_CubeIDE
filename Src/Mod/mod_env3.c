#include "Mod/mod_env3.h"

#include <stdbool.h>
#include <math.h>
#include <string.h>

#include "stm32h7xx.h"
#include "Audio/env_adsr.h"
#include "Audio/audio_note_engine_adapter.h"
#include "IPC/control_audio_command.h"
#include "Param/param_filter_audio.h"
#include "Track/entity_types.h"
#include "Platform/memory_layout.h"

/* GROUP ENV3 configuration is owned by its master entity. */
#undef SEQ_TRACK_COUNT
#define SEQ_TRACK_COUNT BRICK_ENTITY_CAPACITY

#define MOD_ENV3_AUDIO_SAMPLE_RATE 48000.0f

typedef struct
{
    float attack;
    float decay;
    float sustain;
    float release;
} mod_env3_values_t;

typedef struct
{
    env_adsr_t env;
    uint8_t held_notes;
    uint8_t temp_valid;
    mod_env3_values_t applied;
    mod_env3_values_t temp;
} mod_env3_runtime_track_t;

static mod_env3_runtime_track_t g_mod_env3_runtime[SEQ_TRACK_COUNT];
static mod_env3_values_t g_mod_env3_audio_config[SEQ_TRACK_COUNT];
static float g_mod_env3_audio_retrigger[SEQ_TRACK_COUNT];

static uint8_t g_mod_env3_audio_initialized;
static void mod_env3_audio_init(void);

/* All public ENV3 entry points accept an entity ID.  Runtime state is owned
 * by the modulation owner, not by a GROUP child lane. */

static uint8_t mod_env3_audio_resolve_owner(uint8_t track, uint8_t *out_owner)
{
    track_audio_runtime_ctx_t ctx;
    if ((out_owner == NULL) || !audio_note_engine_adapter_current_ctx(track, &ctx))
        return 0U;
    if ((ctx.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD) == 0U)
    {
        *out_owner = track;
        return 1U;
    }
    for (uint8_t entity = 0U; entity < SEQ_TRACK_COUNT; ++entity)
        if (audio_note_engine_adapter_current_ctx(entity, &ctx)
                && ((ctx.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U))
        {
            *out_owner = entity;
            return 1U;
        }
    return 0U;
}

void mod_env3_audio_apply_retrigger(uint8_t track, float value)
{
    if (g_mod_env3_audio_initialized == 0U) mod_env3_audio_init();
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

static void mod_env3_apply_settings(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    mod_env3_runtime_track_t *const rt = &g_mod_env3_runtime[track];
    const mod_env3_values_t *const s = (rt->temp_valid != 0U)
        ? &rt->temp : &g_mod_env3_audio_config[track];
    if (memcmp(&rt->applied, s, sizeof(rt->applied)) == 0)
    {
        return;
    }

    env_adsr_set_attack(&rt->env, mod_env3_seconds_to_u16(param_filter_audio_attack_s(s->attack)));
    env_adsr_set_decay(&rt->env, mod_env3_seconds_to_u16(param_filter_audio_decay_s(s->decay)));
    env_adsr_set_sustain(&rt->env, mod_env3_sustain_to_u15(s->sustain));
    env_adsr_set_release(&rt->env, mod_env3_seconds_to_u16(param_filter_audio_release_s(s->release)));
    rt->applied = *s;
}

static void mod_env3_audio_invalidate_applied(uint8_t track)
{
    g_mod_env3_runtime[track].applied.attack = -1.0f;
    g_mod_env3_runtime[track].applied.decay = -1.0f;
    g_mod_env3_runtime[track].applied.sustain = -1.0f;
    g_mod_env3_runtime[track].applied.release = -1.0f;
}

static void mod_env3_audio_init(void)
{
    memset(g_mod_env3_audio_config, 0, sizeof(g_mod_env3_audio_config));
    memset(g_mod_env3_audio_retrigger, 0, sizeof(g_mod_env3_audio_retrigger));
    memset(g_mod_env3_runtime, 0, sizeof(g_mod_env3_runtime));

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        env_adsr_init(&g_mod_env3_runtime[track].env,
                      MOD_ENV3_AUDIO_SAMPLE_RATE);
        mod_env3_audio_invalidate_applied(track);
    }
    g_mod_env3_audio_initialized = 1U;
}

void mod_env3_init(void)
{
    g_mod_env3_audio_initialized = 0U;
}

static uint8_t mod_env3_write_param(mod_env3_values_t *s, mod_env3_param_t param, float value)
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


uint8_t mod_env3_audio_apply_track_param(uint8_t track, mod_env3_param_t param, float value)
{
    if (g_mod_env3_audio_initialized == 0U) mod_env3_audio_init();
    if ((track >= SEQ_TRACK_COUNT) || (param >= MOD_ENV3_PARAM_COUNT)) return 0U;
    uint8_t owner = 0U;
    if (mod_env3_audio_resolve_owner(track, &owner) == 0U) return 0U;
    if (mod_env3_write_param(&g_mod_env3_audio_config[owner], param, value) == 0U) return 0U;
    mod_env3_apply_settings(owner);
    return 1U;
}




uint8_t mod_env3_apply_track_param_temp(uint8_t track, mod_env3_param_t param, float value)
{
    if (g_mod_env3_audio_initialized == 0U) mod_env3_audio_init();
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
        const mod_env3_values_t *const base = &g_mod_env3_audio_config[track];
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
    if (g_mod_env3_audio_initialized == 0U) mod_env3_audio_init();
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
    if (g_mod_env3_audio_initialized == 0U) mod_env3_audio_init();
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
    if (g_mod_env3_audio_initialized == 0U) mod_env3_audio_init();
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
    if (g_mod_env3_audio_initialized == 0U) mod_env3_audio_init();
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
