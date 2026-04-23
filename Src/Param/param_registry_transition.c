#include "Param/param_registry.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "Mod/mod_lfo_v1.h"
#include "Param/param_filter.h"
#include "Param/param_registry_backends.h"
#include "Param/param_registry_runtime_state.h"
#include "Seq/seq_runtime.h"
#include "Storage/memory_layout.h"
#include "UI/ui_track_catalog.h"
#include "mixer.h"

#define FILTER_RUNTIME_REBIND_NONE 0xFFU

static volatile uint8_t g_param_registry_track_structure_transition_depth = 0U;

static void param_registry_track_structure_transition_begin(void)
{
    if (g_param_registry_track_structure_transition_depth < 255U)
    {
        g_param_registry_track_structure_transition_depth++;
    }
}

static void param_registry_track_structure_transition_end(void)
{
    if (g_param_registry_track_structure_transition_depth > 0U)
    {
        g_param_registry_track_structure_transition_depth--;
    }
}

uint8_t param_registry_track_structure_transition_is_active(void)
{
    return (g_param_registry_track_structure_transition_depth != 0U) ? 1U : 0U;
}

static void param_registry_capture_runtime_mix_targets(uint8_t *out_mix_tracks)
{
    if (out_mix_tracks == NULL)
    {
        return;
    }

    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        out_mix_tracks[track] = FILTER_RUNTIME_REBIND_NONE;

        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL)
                || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                || (ctx->mix_track_id >= MIXER_MAX_TRACKS))
        {
            continue;
        }

        out_mix_tracks[track] = ctx->mix_track_id;
    }
}

static void param_registry_mark_runtime_global_dirty(void)
{
    track_runtime_invalidate_all();
}

static uint8_t param_registry_get_reapply_lane_bound_track_value(param_id_t id,
                                                                 uint8_t track,
                                                                 float *out_value)
{
    if ((id >= PARAM_COUNT) || (track >= SEQ_TRACK_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    if (param_filter_is_param(id) != 0U)
    {
        /* FILTER, MIX and VCA authority is shadow-state per logical track, not runtime cache. */
        return param_registry_get_track_value(id, track, out_value);
    }

    if (param_registry_get_track_value(id, track, out_value) != 0U)
    {
        return 1U;
    }

    /*
     * Lane-bound reapply runs after mixer lane rebind already restored runtime states.
     * On non-FILTER/VCA params, a cache miss is not authoritative and must not promote
     * descriptor defaults that would overwrite the freshly rebound runtime values.
     */
    return param_registry_runtime_cache_get(track, id, out_value);
}

static void param_registry_reapply_lane_bound_runtime_for_changed_tracks(const uint8_t *previous_mix_tracks)
{
    static const param_id_t k_lane_bound_params[] = {
        PARAM_FILTER_TYPE,
        PARAM_FILTER_CUTOFF,
        PARAM_FILTER_RESONANCE,
        PARAM_FILTER_EG_AMT,
        PARAM_FILTER_ATTACK,
        PARAM_FILTER_DECAY,
        PARAM_FILTER_SUSTAIN,
        PARAM_FILTER_RELEASE,
        PARAM_FILTER_KEYTRK,
        PARAM_FILTER_ENVRST,
        PARAM_FILTER_ENVDLY,
        PARAM_FILTER_EQ_LOW,
        PARAM_FILTER_EQ_MID,
        PARAM_FILTER_EQ_HIGH,
        PARAM_FILTER_DRIVE,
        PARAM_FILTER_DECIMATOR_BITS,
        PARAM_FILTER_DECIMATOR_RATE,
        PARAM_FILTER_DECIMATOR_RATE2,
        PARAM_MIX_LEVEL,
        PARAM_MIX_PAN,
        PARAM_MIX_SEND1,
        PARAM_MIX_SEND2,
        PARAM_MIX_MUTE,
        PARAM_HYBRID_GATE,
        PARAM_VCA_ATTACK,
        PARAM_VCA_DECAY,
        PARAM_VCA_SUSTAIN,
        PARAM_VCA_RELEASE
    };

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint8_t previous_mix_track = FILTER_RUNTIME_REBIND_NONE;
        uint8_t current_mix_track = FILTER_RUNTIME_REBIND_NONE;
        uint8_t migrated_existing_lane = 0U;

        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx != NULL)
                && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (ctx->mix_track_id < MIXER_MAX_TRACKS))
        {
            current_mix_track = ctx->mix_track_id;
        }

        if (previous_mix_tracks != NULL)
        {
            previous_mix_track = previous_mix_tracks[track];

            if (previous_mix_track == current_mix_track)
            {
                continue;
            }

            migrated_existing_lane = ((previous_mix_track < MIXER_MAX_TRACKS)
                    && (current_mix_track < MIXER_MAX_TRACKS)) ? 1U : 0U;
        }

        for (uint8_t i = 0U; i < (uint8_t)(sizeof(k_lane_bound_params) / sizeof(k_lane_bound_params[0])); ++i)
        {
            if ((migrated_existing_lane != 0U)
                    && (param_filter_is_param(k_lane_bound_params[i]) != 0U))
            {
                /*
                 * Runtime FILTER state for lane migrations is already moved by
                 * mixer_rebind_track_states(previous_mix_tracks, next_mix_tracks,...).
                 * Reapplying from FILTER shadow here can overwrite that good runtime state.
                 */
                continue;
            }

            float value = 0.0f;
            if (param_registry_get_reapply_lane_bound_track_value(k_lane_bound_params[i], track, &value) == 0U)
            {
                continue;
            }

            (void)param_registry_apply_track_value(k_lane_bound_params[i], track, value);
        }
    }
}

static void param_registry_rebind_lane_runtime(const uint8_t *previous_mix_tracks)
{
    uint8_t next_mix_tracks[SEQ_TRACK_COUNT];

    if (previous_mix_tracks == NULL)
    {
        return;
    }

    param_registry_capture_runtime_mix_targets(next_mix_tracks);
    mixer_rebind_track_states(previous_mix_tracks, next_mix_tracks, SEQ_TRACK_COUNT);
    param_registry_mark_runtime_global_dirty();
}

static void param_registry_neutralize_filter_runtime_if_invalid(uint8_t track)
{
    uint8_t filter_track = 0U;
    uint8_t mix_track = 0U;

    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    track_runtime_refresh_track(track);
    if (track_runtime_resolve_filter_target_track(track, &filter_track) != 0U)
    {
        return;
    }

    if (track_runtime_is_audio_routable(track) == 0U)
    {
        return;
    }

    if (track_runtime_get_mix_target_track(track, &mix_track) == 0U)
    {
        return;
    }

    mixer_set_track_filter_type((uint32_t)mix_track, MIXER_TRACK_FILTER_OFF);
    mixer_track_filter_all_notes_off((uint32_t)mix_track);
}

static void param_registry_neutralize_vca_runtime_if_invalid(uint8_t track)
{
    uint8_t mix_track = 0U;

    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    track_runtime_refresh_track(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx != NULL)
            && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)
                || ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_HYBRID))))
    {
        return;
    }

    if (track_runtime_is_audio_routable(track) == 0U)
    {
        return;
    }

    if (track_runtime_get_mix_target_track(track, &mix_track) == 0U)
    {
        return;
    }

    mixer_track_vca_all_notes_off((uint32_t)mix_track);
    mixer_set_track_vca_enabled((uint32_t)mix_track, 0U);
}

static void param_registry_finalize_track_structure_change(const uint8_t *previous_mix_tracks)
{
    if (previous_mix_tracks == NULL)
    {
        return;
    }

    param_registry_rebind_lane_runtime(previous_mix_tracks);

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        param_registry_neutralize_filter_runtime_if_invalid(track);
        param_registry_neutralize_vca_runtime_if_invalid(track);
    }

    param_registry_reapply_lane_bound_runtime_for_changed_tracks(previous_mix_tracks);
    param_registry_mark_runtime_global_dirty();
}

static uint8_t param_registry_apply_track_structure_transition_mutate(void *ctx)
{
    const param_registry_track_structure_transition_cmd_t *const cmd =
            (const param_registry_track_structure_transition_cmd_t *)ctx;

    if ((cmd == NULL) || (cmd->mutation_fn == NULL))
    {
        return 0U;
    }

    cmd->mutation_fn(cmd->mutation_ctx);
    return 1U;
}

uint8_t param_registry_run_track_transition_pipeline(const param_registry_track_transition_pipeline_cmd_t *cmd)
{
    uint8_t previous_mix_tracks[SEQ_TRACK_COUNT];

    if ((cmd == NULL) || (cmd->mutate_fn == NULL))
    {
        return 0U;
    }

    param_registry_capture_runtime_mix_targets(previous_mix_tracks);
    param_registry_track_structure_transition_begin();

    uint8_t ok = 1U;
    if ((cmd->prepare_fn != NULL) && (cmd->prepare_fn(cmd->ctx) == 0U))
    {
        ok = 0U;
    }

    if ((ok != 0U) && (cmd->mutate_fn(cmd->ctx) == 0U))
    {
        ok = 0U;
    }

    if (ok != 0U)
    {
        param_registry_finalize_track_structure_change(previous_mix_tracks);
    }

    param_registry_track_structure_transition_end();

    if (ok == 0U)
    {
        return 0U;
    }

    if ((cmd->reapply_fn != NULL) && (cmd->reapply_fn(cmd->ctx) == 0U))
    {
        return 0U;
    }

    if ((cmd->seq_runtime_sync_fn != NULL) && (cmd->seq_runtime_sync_fn(cmd->ctx) == 0U))
    {
        return 0U;
    }

    if ((cmd->ui_sync_fn != NULL) && (cmd->ui_sync_fn(cmd->ctx) == 0U))
    {
        return 0U;
    }

    if ((cmd->resume_fn != NULL) && (cmd->resume_fn(cmd->ctx) == 0U))
    {
        return 0U;
    }

    return 1U;
}

void param_registry_apply_track_structure_transition(const param_registry_track_structure_transition_cmd_t *cmd)
{
    const param_registry_track_transition_pipeline_cmd_t pipeline_cmd = {
        .prepare_fn = NULL,
        .mutate_fn = param_registry_apply_track_structure_transition_mutate,
        .reapply_fn = NULL,
        .seq_runtime_sync_fn = NULL,
        .ui_sync_fn = NULL,
        .resume_fn = NULL,
        .ctx = (void *)cmd
    };

    (void)param_registry_run_track_transition_pipeline(&pipeline_cmd);
}
