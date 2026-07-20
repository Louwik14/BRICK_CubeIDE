#include "Audio/audio_control_snapshot.h"

#include <stddef.h>
#include <string.h>

#include "Core/track_runtime.h"
#include "Core/track_sound_state.h"
#include "Core/track_state.h"
#include "Core/track_tone_sound_state.h"
#include "UI/ui_core.h"
#include "stm32h7xx_hal.h"

#define AUDIO_CONTROL_SNAPSHOT_FLAG_CAN_FILTER (1U << 0)

static audio_control_snapshot_t g_audio_control_snapshot_buffers[2];
static uint8_t g_audio_control_snapshot_active_index;
static volatile uint8_t g_audio_control_snapshot_pending_valid;
static volatile uint8_t g_audio_control_snapshot_pending_index;
static uint32_t g_audio_control_snapshot_next_generation;
static volatile uint32_t g_audio_control_snapshot_published_generation;
static volatile uint32_t g_audio_control_snapshot_applied_generation;
static volatile uint32_t g_audio_control_snapshot_publish_count;
static volatile uint32_t g_audio_control_snapshot_apply_count;
static volatile uint32_t g_audio_control_snapshot_retained_old_generation_blocks;
static volatile uint32_t g_audio_control_snapshot_publish_skipped_pending_count;

static uint8_t audio_control_snapshot_ctx_is_sampler_clip_or_looper(const track_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    return (uint8_t)((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && ((ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_CLIP)
                || (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER)));
}

static uint8_t audio_control_snapshot_supports_vca_gate(const track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    if (audio_control_snapshot_ctx_is_sampler_clip_or_looper(ctx) != 0U)
    {
        return 0U;
    }

    if ((ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
            || (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            || (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE))
    {
        return 1U;
    }

    return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_HYBRID)) ? 1U : 0U;
}

static track_runtime_midi_source_t audio_control_snapshot_midi_source_from_ctx(const track_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return TRACK_RUNTIME_MIDI_SOURCE_ALL;
    }

    switch ((ui_track_midi_source_t)ctx->midi_source)
    {
        case UI_TRACK_MIDI_SRC_INT:
            return TRACK_RUNTIME_MIDI_SOURCE_INTERNAL;
        case UI_TRACK_MIDI_SRC_EXT:
            return TRACK_RUNTIME_MIDI_SOURCE_EXTERNAL;
        case UI_TRACK_MIDI_SRC_ALL:
        default:
            return TRACK_RUNTIME_MIDI_SOURCE_ALL;
    }
}

static uint8_t audio_control_snapshot_param_play_voice_index(param_id_t param)
{
    switch (param)
    {
        case PARAM_SEQ_PLAY_V1_NOTE:
        case PARAM_SEQ_PLAY_V1_VEL:
        case PARAM_SEQ_PLAY_V1_LEN:
        case PARAM_SEQ_PLAY_V1_MICTIM:
            return 0U;
        case PARAM_SEQ_PLAY_V2_NOTE:
        case PARAM_SEQ_PLAY_V2_VEL:
        case PARAM_SEQ_PLAY_V2_LEN:
        case PARAM_SEQ_PLAY_V2_MICTIM:
            return 1U;
        case PARAM_SEQ_PLAY_V3_NOTE:
        case PARAM_SEQ_PLAY_V3_VEL:
        case PARAM_SEQ_PLAY_V3_LEN:
        case PARAM_SEQ_PLAY_V3_MICTIM:
            return 2U;
        case PARAM_SEQ_PLAY_V4_NOTE:
        case PARAM_SEQ_PLAY_V4_VEL:
        case PARAM_SEQ_PLAY_V4_LEN:
        case PARAM_SEQ_PLAY_V4_MICTIM:
            return 3U;
        default:
            return 0xFFU;
    }
}

static uint8_t audio_control_snapshot_play_voice_count(const track_runtime_ctx_t *ctx)
{
    return ((ctx != NULL)
            && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI)) ? 4U : 1U;
}

static void audio_control_snapshot_build(audio_control_snapshot_t *dst, uint32_t generation)
{
    if (dst == NULL)
    {
        return;
    }

    memset(dst, 0, sizeof(*dst));
    (void)track_runtime_refresh_if_dirty();
    dst->generation = generation;
    dst->track_runtime_revision = track_runtime_get_revision();
    track_runtime_get_cached_synth_usage(&dst->synth_usage);

    for (uint8_t mix_track = 0U; mix_track < MIXER_MAX_TRACKS; ++mix_track)
    {
        dst->logical_track_by_mix_track[mix_track] = 0xFFU;
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if (ctx != NULL)
        {
            dst->runtime[track] = *ctx;
            if ((ctx->bind_state == TRACK_RUNTIME_BIND_BOUND) && (ctx->mix_track_id < MIXER_MAX_TRACKS))
            {
                dst->logical_track_by_mix_track[ctx->mix_track_id] = track;
            }
        }

        dst->ui_family[track] = (uint8_t)ui_get_track_family(track);
        dst->ui_type[track] = (uint8_t)ui_get_track_type(track);
        dst->voice_group_role[track] = (uint8_t)track_state_get_voice_group_role(track);

        const track_sound_state_t *const sound = track_sound_state_get_const(track);
        if (sound != NULL)
        {
            dst->mod[track].mod_lfo[0] = sound->mod_lfo[0];
            dst->mod[track].mod_lfo[1] = sound->mod_lfo[1];
            for (uint8_t lfo = 0U; lfo < 2U; ++lfo)
            {
                const param_id_t dest = (param_id_t)((uint16_t)(sound->mod_lfo[lfo].dest + 0.5f));
                dst->mod[track].lfo_dest[lfo] = dest;
                if ((dest < PARAM_COUNT)
                        && (param_registry_get_track_value(dest, track, &dst->mod[track].lfo_base[lfo]) != 0U))
                {
                    dst->mod[track].lfo_base_valid[lfo] = 1U;
                }
            }
        }

        const track_tone_sound_state_t *const tone = track_tone_sound_state_get_const(track);
        if (tone != NULL)
        {
            for (uint8_t slot = 0U; slot < 4U; ++slot)
            {
                dst->master_fx[track].type[slot] = tone->master_fx.type[slot];
                dst->master_fx[track].level[slot] = tone->master_fx.level[slot];
                dst->master_fx[track].macro_a[slot] = tone->master_fx.macro_a[slot];
                dst->master_fx[track].macro_b[slot] = tone->master_fx.macro_b[slot];
            }
        }
    }
}

void audio_control_snapshot_init(void)
{
    memset(g_audio_control_snapshot_buffers, 0, sizeof(g_audio_control_snapshot_buffers));
    g_audio_control_snapshot_active_index = 0U;
    g_audio_control_snapshot_pending_valid = 0U;
    g_audio_control_snapshot_pending_index = 1U;
    g_audio_control_snapshot_next_generation = 1U;
    g_audio_control_snapshot_published_generation = 0U;
    g_audio_control_snapshot_applied_generation = 0U;
    g_audio_control_snapshot_publish_count = 0U;
    g_audio_control_snapshot_apply_count = 0U;
    g_audio_control_snapshot_retained_old_generation_blocks = 0U;
    g_audio_control_snapshot_publish_skipped_pending_count = 0U;
}

uint8_t audio_control_snapshot_publish_from_control(void)
{
    if (g_audio_control_snapshot_pending_valid != 0U)
    {
        g_audio_control_snapshot_publish_skipped_pending_count++;
        return 0U;
    }

    const uint8_t staging_index = (uint8_t)(1U - g_audio_control_snapshot_active_index);
    audio_control_snapshot_t *const staging = &g_audio_control_snapshot_buffers[staging_index];
    audio_control_snapshot_build(staging, g_audio_control_snapshot_next_generation);

    const audio_control_snapshot_t *const active = &g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index];
    if ((active->generation != 0U)
            && (memcmp(&staging->track_runtime_revision,
                       &active->track_runtime_revision,
                       sizeof(*staging) - offsetof(audio_control_snapshot_t, track_runtime_revision)) == 0))
    {
        staging->generation = 0U;
        return 0U;
    }

    g_audio_control_snapshot_next_generation++;
    if (g_audio_control_snapshot_next_generation == 0U)
    {
        g_audio_control_snapshot_next_generation = 1U;
    }

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_audio_control_snapshot_pending_index = staging_index;
    g_audio_control_snapshot_published_generation = staging->generation;
    g_audio_control_snapshot_pending_valid = 1U;
    g_audio_control_snapshot_publish_count++;
    __set_PRIMASK(primask);
    return 1U;
}

void audio_control_snapshot_force_apply_latest(void)
{
    if (g_audio_control_snapshot_pending_valid == 0U)
    {
        return;
    }

    g_audio_control_snapshot_active_index = g_audio_control_snapshot_pending_index;
    g_audio_control_snapshot_applied_generation =
        g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index].generation;
    g_audio_control_snapshot_pending_valid = 0U;
    g_audio_control_snapshot_apply_count++;
}

void audio_control_snapshot_apply_pending_from_audio(void)
{
    if (g_audio_control_snapshot_pending_valid != 0U)
    {
        g_audio_control_snapshot_active_index = g_audio_control_snapshot_pending_index;
        g_audio_control_snapshot_applied_generation =
            g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index].generation;
        g_audio_control_snapshot_pending_valid = 0U;
        g_audio_control_snapshot_apply_count++;
        return;
    }

    if (g_audio_control_snapshot_applied_generation != g_audio_control_snapshot_published_generation)
    {
        g_audio_control_snapshot_retained_old_generation_blocks++;
    }
}

const audio_control_snapshot_t *audio_control_snapshot_get_active(void)
{
    return &g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index];
}

uint32_t audio_control_snapshot_get_active_track_runtime_revision(void)
{
    return g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index].track_runtime_revision;
}

const track_runtime_ctx_t *audio_control_snapshot_get_track_ctx(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    return &g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index].runtime[track];
}

uint8_t audio_control_snapshot_is_audio_routable(uint8_t track)
{
    const track_runtime_ctx_t *const ctx = audio_control_snapshot_get_track_ctx(track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    return (ctx->mix_track_id < MIXER_MAX_TRACKS) ? 1U : 0U;
}

void audio_control_snapshot_get_synth_usage(track_runtime_synth_usage_t *out_usage)
{
    if (out_usage == NULL)
    {
        return;
    }

    *out_usage = g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index].synth_usage;
}

uint8_t audio_control_snapshot_get_logical_track_for_mix_track(uint8_t mix_track, uint8_t *out_track)
{
    if ((out_track == NULL) || (mix_track >= MIXER_MAX_TRACKS))
    {
        return 0U;
    }

    const uint8_t track =
        g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index].logical_track_by_mix_track[mix_track];
    if (track < SEQ_TRACK_COUNT)
    {
        *out_track = track;
        return 1U;
    }

    return 0U;
}

uint8_t audio_control_snapshot_get_midi_channel_zero_based(uint8_t track)
{
    const track_runtime_ctx_t *const ctx = audio_control_snapshot_get_track_ctx(track);
    if (ctx == NULL)
    {
        return 0U;
    }

    const uint8_t channel = ctx->midi_channel_1_16;
    const uint8_t clamped = (channel < 1U) ? 1U : ((channel > 16U) ? 16U : channel);
    return (uint8_t)(clamped - 1U);
}

uint8_t audio_control_snapshot_resolve_track(uint8_t track, track_runtime_resolved_track_t *out_resolved)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_resolved == NULL))
    {
        return 0U;
    }

    memset(out_resolved, 0, sizeof(*out_resolved));
    out_resolved->track_id = track;
    out_resolved->midi_source = TRACK_RUNTIME_MIDI_SOURCE_ALL;

    const track_runtime_ctx_t *const ctx = audio_control_snapshot_get_track_ctx(track);
    if (ctx == NULL)
    {
        return 0U;
    }

    out_resolved->descriptor.family = (track_runtime_family_t)ctx->family;
    out_resolved->descriptor.type = (track_runtime_type_t)ctx->type;
    out_resolved->descriptor.engine = (track_runtime_engine_t)ctx->engine;
    out_resolved->descriptor.bind_state = ctx->bind_state;
    out_resolved->descriptor.bind_reason = ctx->bind_reason;
    out_resolved->descriptor.instance_id = ctx->instance_id;
    out_resolved->descriptor.mix_track_id = ctx->mix_track_id;
    out_resolved->descriptor.flags = ctx->flags;
    out_resolved->descriptor.midi_channel_1_16 = (uint8_t)(audio_control_snapshot_get_midi_channel_zero_based(track) + 1U);
    out_resolved->midi_channel_zero_based = audio_control_snapshot_get_midi_channel_zero_based(track);
    out_resolved->midi_source = audio_control_snapshot_midi_source_from_ctx(ctx);

    if (audio_control_snapshot_is_audio_routable(track) != 0U)
    {
        out_resolved->has_mix_target = 1U;
        out_resolved->mix_track_id = ctx->mix_track_id;
    }

    if (((ctx->flags & AUDIO_CONTROL_SNAPSHOT_FLAG_CAN_FILTER) != 0U) && (out_resolved->has_mix_target != 0U))
    {
        out_resolved->has_filter_target = 1U;
        out_resolved->filter_track_id = ctx->mix_track_id;
    }

    out_resolved->supports_vca_gate = audio_control_snapshot_supports_vca_gate(ctx);
    return 1U;
}

track_runtime_param_status_t audio_control_snapshot_get_effective_param_status(uint8_t track, param_id_t param)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= PARAM_COUNT))
    {
        return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }

    const track_runtime_ctx_t *const ctx = audio_control_snapshot_get_track_ctx(track);
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    switch (rule.resource)
    {
        case TRACK_RUNTIME_RESOURCE_PLAY:
        {
            if ((ctx == NULL)
                    || ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                        && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                        && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)
                        && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_MIDI)))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }

            const uint8_t voice = audio_control_snapshot_param_play_voice_index(param);
            if ((voice != 0xFFU) && (voice >= audio_control_snapshot_play_voice_count(ctx)))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return TRACK_RUNTIME_PARAM_ALLOWED;
        }

        case TRACK_RUNTIME_RESOURCE_MIX:
            return (audio_control_snapshot_is_audio_routable(track) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        case TRACK_RUNTIME_RESOURCE_FILTER:
            return ((ctx != NULL)
                    && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                    && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_OFF))
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        case TRACK_RUNTIME_RESOURCE_SYNTH:
            return ((ctx != NULL)
                    && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                    && ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                        || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)))
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        case TRACK_RUNTIME_RESOURCE_NONE:
            return rule.status;

        default:
            return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }
}

uint8_t audio_control_snapshot_get_voice_group_role(uint8_t track, uint8_t *out_role)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_role == NULL))
    {
        return 0U;
    }

    *out_role = g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index].voice_group_role[track];
    return 1U;
}

uint8_t audio_control_snapshot_collect_voice_group_members(uint8_t master_track,
                                                           uint8_t *out_members,
                                                           uint8_t out_members_capacity,
                                                           uint8_t *out_count)
{
    if ((master_track >= SEQ_TRACK_COUNT) || (out_count == NULL))
    {
        return 0U;
    }

    const audio_control_snapshot_t *const snapshot = audio_control_snapshot_get_active();
    if (snapshot->voice_group_role[master_track] != (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t track = master_track; track < SEQ_TRACK_COUNT; ++track)
    {
        const uint8_t role = snapshot->voice_group_role[track];
        if (track == master_track)
        {
            if (role != (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
            {
                return 0U;
            }
        }
        else if (role != (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
        {
            break;
        }

        if ((out_members != NULL) && (count < out_members_capacity))
        {
            out_members[count] = track;
        }
        ++count;
    }

    if ((out_members != NULL) && (count > out_members_capacity))
    {
        return 0U;
    }

    *out_count = count;
    return 1U;
}

uint8_t audio_control_snapshot_get_lfo_settings(uint8_t track, uint8_t lfo_index, track_mod_lfo_state_t *out_settings)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= 2U) || (out_settings == NULL))
    {
        return 0U;
    }

    *out_settings = g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index].mod[track].mod_lfo[lfo_index];
    return 1U;
}

uint8_t audio_control_snapshot_get_lfo_base(uint8_t track, uint8_t lfo_index, param_id_t dest, float *out_base)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= 2U) || (out_base == NULL))
    {
        return 0U;
    }

    const audio_control_mod_snapshot_t *const mod =
        &g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index].mod[track];
    if ((mod->lfo_base_valid[lfo_index] == 0U) || (mod->lfo_dest[lfo_index] != dest))
    {
        return 0U;
    }

    *out_base = mod->lfo_base[lfo_index];
    return 1U;
}

uint8_t audio_control_snapshot_get_master_fx(uint8_t track, audio_control_master_fx_snapshot_t *out_fx)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_fx == NULL))
    {
        return 0U;
    }

    *out_fx = g_audio_control_snapshot_buffers[g_audio_control_snapshot_active_index].master_fx[track];
    return 1U;
}

void audio_control_snapshot_diag_snapshot(audio_control_snapshot_diag_t *out_diag)
{
    if (out_diag == NULL)
    {
        return;
    }

    out_diag->published_generation = g_audio_control_snapshot_published_generation;
    out_diag->applied_generation = g_audio_control_snapshot_applied_generation;
    out_diag->publish_count = g_audio_control_snapshot_publish_count;
    out_diag->apply_count = g_audio_control_snapshot_apply_count;
    out_diag->retained_old_generation_blocks = g_audio_control_snapshot_retained_old_generation_blocks;
    out_diag->publish_skipped_pending_count = g_audio_control_snapshot_publish_skipped_pending_count;
}
