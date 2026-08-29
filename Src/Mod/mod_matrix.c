#include "Mod/mod_matrix.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/audio_fx_runtime.h"
#include "Audio/fx_audio_drift.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/drum_synth.h"
#include "Audio/md_model.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include <string.h>

#include "Audio/mixer.h"
#include "Core/track_sound_state.h"
#include "Core/entity_topology.h"
#include "Audio/control_audio_command.h"
#include "Core/live_clock.h"
#include "Core/live_parameter_audio_publication.h"
#include "Core/live_parameter_event.h"
#include "Core/track_state.h"
#include "Mod/mod_destination_catalog.h"
#include "Mod/mod_lfo_v1.h"
#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"
#include "Seq/seq_types.h"

/* Runtime remains entity-scoped; GROUP control state is owned by the master. */
#undef SEQ_TRACK_COUNT
#define SEQ_TRACK_COUNT SEQ_LANE_CAPACITY


typedef struct
{
    uint8_t valid;
    uint8_t modulation_active;
    uint16_t destination;
    float base_value;
    float sum;
    float sum_end;
    float min_value;
    float max_value;
    mod_destination_ramp_t ramp;
    mod_destination_prepared_t prepared;
} mod_matrix_runtime_destination_t;

typedef struct
{
    mod_matrix_runtime_destination_t destinations[MOD_MATRIX_SLOT_COUNT];
} mod_matrix_runtime_track_t;

typedef struct
{
    uint8_t any_route;
    uint16_t required_source_mask;
} mod_matrix_route_cache_t;

typedef struct
{
    uint8_t source;
    uint8_t destination_index;
    float scale;
} mod_matrix_track_route_t;

typedef struct
{
    uint8_t runtime_destination_index;
    uint16_t discontinuity_source_mask;
} mod_matrix_track_destination_t;

typedef struct
{
    uint8_t route_count;
    uint8_t destination_count;
    mod_matrix_track_route_t routes[MOD_MATRIX_SLOT_COUNT];
    mod_matrix_track_destination_t destinations[MOD_MATRIX_SLOT_COUNT];
} mod_matrix_track_plan_t;

typedef struct
{
    float multi[2];
    float slew[2];
    uint8_t multi_valid[2];
    uint8_t slew_valid[2];
} mod_matrix_operator_runtime_t;

typedef struct
{
    float slew_amount[2];
    uint8_t multi_source[2][2];
    uint8_t slew_source[2];
    uint8_t drum_md_slot_count;
} mod_matrix_operator_config_t;

typedef struct
{
    track_mod_matrix_slot_t slots[MOD_MATRIX_SLOT_COUNT];
    uint8_t multi_source[2][2];
    uint8_t slew_source[2];
    float slew_amount[2];
} mod_matrix_audio_state_t;

static mod_matrix_runtime_track_t g_mod_matrix_runtime[SEQ_TRACK_COUNT];
static mod_matrix_operator_config_t g_mod_matrix_operator_config[SEQ_TRACK_COUNT];
static mod_matrix_route_cache_t g_mod_matrix_route_cache[SEQ_TRACK_COUNT];
typedef struct
{
    uint8_t source;
    uint8_t destination_index;
    float scale;
} mod_matrix_poly_route_t;

typedef struct
{
    uint8_t runtime_destination_index;
} mod_matrix_poly_destination_t;

typedef struct
{
    uint8_t source_mask;
    uint8_t route_count;
    uint8_t destination_count;
    mod_matrix_poly_route_t routes[MOD_MATRIX_SLOT_COUNT];
    mod_matrix_poly_destination_t destinations[MOD_MATRIX_SLOT_COUNT];
} mod_matrix_poly_plan_t;

static mod_matrix_poly_plan_t g_mod_matrix_poly_plan[SEQ_TRACK_COUNT];
static mod_matrix_track_plan_t g_mod_matrix_track_plan[SEQ_TRACK_COUNT];
static mod_matrix_operator_runtime_t g_mod_matrix_operator_runtime[SEQ_TRACK_COUNT];
static mod_matrix_audio_state_t g_mod_matrix_audio_state[SEQ_TRACK_COUNT];
static uint16_t g_mod_matrix_audio_dirty_mask;
static uint8_t g_mod_matrix_any_route = 0U;

static uint8_t mod_matrix_audio_resolve_owner(uint8_t track, uint8_t *out_owner)
{
    track_audio_runtime_ctx_t ctx;
    if ((out_owner == NULL) || !audio_note_engine_adapter_current_ctx(track, &ctx))
        return 0U;
    if ((ctx.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD) == 0U)
    { *out_owner = track; return 1U; }
    for (uint8_t entity = 0U; entity < SEQ_TRACK_COUNT; ++entity)
        if (audio_note_engine_adapter_current_ctx(entity, &ctx)
                && ((ctx.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U))
        { *out_owner = entity; return 1U; }
    return 0U;
}

/* Read the model authorities that the AUDIO engines actually execute.  This is
 * a short-lived projection used while building a Matrix plan, never Matrix
 * state. */
static void mod_matrix_audio_read_models(
    uint8_t track, const track_audio_runtime_ctx_t *ctx,
    mod_destination_audio_models_t *out)
{
    memset(out, 0, sizeof(*out));
    out->audio_fx_model[0] = audio_fx_runtime_get_model(track, AUDIO_FX_SLOT_A);
    out->audio_fx_model[1] = audio_fx_runtime_get_model(track, AUDIO_FX_SLOT_B);
    if ((ctx == NULL) || (ctx->program_route.active == 0U)) return;
    const uint8_t instance = ctx->program_route.instance_id;
    if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
        for (uint8_t osc = 0U; osc < 2U; ++osc)
            out->prism_model[osc] = brick6_braids_runtime_get_osc_model(instance, osc);
    else if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
        for (uint8_t slot = 0U; slot < 3U; ++slot)
            out->stack_model[slot] = (uint8_t)brick6_stack_runtime_get_slot_model(instance, slot);
    else if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM
             && ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_MD)
        out->drum_md_slot_count = md_model_profile_get(
            drum_synth_get_md_model_for_instance(instance))->slot_count;
}


static float mod_matrix_clampf(float v, float lo, float hi)
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

static track_mod_matrix_slot_t *mod_matrix_track_slot_mut(uint8_t track, uint8_t slot)
{
    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U)
    {
        return NULL;
    }
    track_sound_state_t *const state = track_sound_state_get(owner);
    if ((state == NULL) || (slot >= MOD_MATRIX_SLOT_COUNT))
    {
        return NULL;
    }
    track_mod_matrix_slot_t *const matrix_slot = &state->mod_matrix[slot];
    if (matrix_slot->destination < (mod_destination_address_t)PARAM_COUNT)
        matrix_slot->destination = mod_destination_address_make(
            owner, (param_id_t)matrix_slot->destination);
    return matrix_slot;
}

static const track_mod_matrix_slot_t *mod_matrix_track_slot_const(uint8_t track, uint8_t slot)
{
    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U)
    {
        return NULL;
    }
    const track_sound_state_t *const state = track_sound_state_get_const(owner);
    if ((state == NULL) || (slot >= MOD_MATRIX_SLOT_COUNT))
    {
        return NULL;
    }
    return &state->mod_matrix[slot];
}

static track_sound_state_t *mod_matrix_control_state(uint8_t track)
{
    brick_entity_id_t owner = track;
    return (entity_topology_mod_owner(track, &owner) != 0U)
        ? track_sound_state_get(owner) : NULL;
}

static const track_sound_state_t *mod_matrix_control_state_const(uint8_t track)
{
    brick_entity_id_t owner = track;
    return (entity_topology_mod_owner(track, &owner) != 0U)
        ? track_sound_state_get_const(owner) : NULL;
}

static void mod_matrix_recompute_global_route_flag(void)
{
    g_mod_matrix_any_route = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (g_mod_matrix_route_cache[track].any_route != 0U)
        {
            g_mod_matrix_any_route = 1U;
            return;
        }
    }
}

static ui_track_family_t mod_matrix_ui_family_from_ctx(const track_audio_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return UI_TRACK_FAMILY_OFF;
    }

    switch ((track_runtime_family_t)ctx->family)
    {
        case TRACK_RUNTIME_FAMILY_SYNTH:
            return UI_TRACK_FAMILY_SYNTH;
        case TRACK_RUNTIME_FAMILY_SAMPLER:
            return UI_TRACK_FAMILY_SAMPLER;
        case TRACK_RUNTIME_FAMILY_DRUM:
            return UI_TRACK_FAMILY_DRUM;
            return UI_TRACK_FAMILY_OFF;
        case TRACK_RUNTIME_FAMILY_MIDI:
            return UI_TRACK_FAMILY_MIDI;
        case TRACK_RUNTIME_FAMILY_EXTERNAL:
            return UI_TRACK_FAMILY_EXTERNAL;
        case TRACK_RUNTIME_FAMILY_OFF:
        default:
            return UI_TRACK_FAMILY_OFF;
    }
}

static ui_track_type_t mod_matrix_ui_type_from_ctx(const track_audio_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return UI_TRACK_TYPE_NONE;
    }

    switch ((track_runtime_type_t)ctx->type)
    {
        case TRACK_RUNTIME_TYPE_RAM:
            return UI_TRACK_TYPE_RAM;
        case TRACK_RUNTIME_TYPE_PRISM:
            return UI_TRACK_TYPE_PRISM;
        case TRACK_RUNTIME_TYPE_WAVE:
            return UI_TRACK_TYPE_WAVE;
        case TRACK_RUNTIME_TYPE_STACK:
            return UI_TRACK_TYPE_STACK;
        case TRACK_RUNTIME_TYPE_DRUM_MD:
            return UI_TRACK_TYPE_DRUM_MD;
        case TRACK_RUNTIME_TYPE_MIDI:
            return UI_TRACK_TYPE_MIDI;
        case TRACK_RUNTIME_TYPE_EXTERNAL:
            return UI_TRACK_TYPE_EXTERNAL;
        case TRACK_RUNTIME_TYPE_STREAM:
            return UI_TRACK_TYPE_STREAM;
        case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
            return UI_TRACK_TYPE_DRUM_BD_ANALOG;
        case TRACK_RUNTIME_TYPE_LOOPER:
            return UI_TRACK_TYPE_LOOPER;
        case TRACK_RUNTIME_TYPE_MULTI:
            return UI_TRACK_TYPE_MULTI;
        case TRACK_RUNTIME_TYPE_NONE:
        default:
            return UI_TRACK_TYPE_NONE;
    }
}

static uint8_t mod_matrix_track_selected_slot(uint8_t track, uint8_t *out_slot)
{
    const track_sound_state_t *const state = mod_matrix_control_state_const(track);
    if ((state == NULL) || (out_slot == NULL))
    {
        return 0U;
    }

    *out_slot = (state->mod_matrix_selected_slot < MOD_MATRIX_SLOT_COUNT) ? state->mod_matrix_selected_slot : 0U;
    return 1U;
}

static uint8_t mod_matrix_slot_is_effective(uint8_t track,
                                            const track_mod_matrix_slot_t *slot,
                                            ui_track_family_t family,
                                            ui_track_type_t type,
                                            const track_audio_runtime_ctx_t *ctx)
{
    uint8_t target = 0U;
    param_id_t destination = PARAM_COUNT;
    const uint8_t is_group_master = (uint8_t)((ctx != NULL)
        && ((ctx->flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U));
    if ((slot == NULL)
            || (slot->enabled == 0U)
            || (slot->source == (uint8_t)MOD_MATRIX_SOURCE_NONE)
            || (slot->source >= MOD_MATRIX_SOURCE_COUNT)
            || (mod_destination_address_resolve(slot->destination,
                                                &target, &destination) == 0U)
            || (slot->depth == 0.0f))
    {
        return 0U;
    }

    switch ((mod_matrix_source_t)slot->source)
    {
        case MOD_MATRIX_SOURCE_LFO1:
        case MOD_MATRIX_SOURCE_LFO2:
        case MOD_MATRIX_SOURCE_LFO3:
        case MOD_MATRIX_SOURCE_ENV3:
        case MOD_MATRIX_SOURCE_MULTI1:
        case MOD_MATRIX_SOURCE_MULTI2:
        case MOD_MATRIX_SOURCE_SLEW1:
        case MOD_MATRIX_SOURCE_SLEW2:
            break;

        case MOD_MATRIX_SOURCE_ENV_VCA:
            if (is_group_master != 0U)
            {
                return 0U;
            }
            if ((ctx == NULL)
                    || (ctx->program_route.mix_track_id >= MIXER_MAX_TRACKS)
                    || (audio_note_engine_adapter_ctx_supports_vca_gate(ctx) == 0U))
            {
                return 0U;
            }
            break;

        case MOD_MATRIX_SOURCE_ENV_FLT:
            if (is_group_master != 0U)
            {
                return 0U;
            }
            if ((ctx == NULL)
                    || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
                    || ((ctx->flags & CONTROL_AUDIO_PROGRAM_FLAG_CAN_FILTER) == 0U)
                    || (ctx->has_filter_target == 0U))
            {
                return 0U;
            }
            break;

        default:
            return 0U;
    }

    track_audio_runtime_ctx_t target_ctx_value;
    const track_audio_runtime_ctx_t *const target_ctx =
        (audio_note_engine_adapter_current_ctx(
            target, &target_ctx_value) != 0U) ? &target_ctx_value : NULL;
    mod_destination_audio_models_t models;
    mod_matrix_audio_read_models(target, target_ctx, &models);
    return mod_destination_catalog_supported_audio(
        target, destination,
        mod_matrix_ui_family_from_ctx(target_ctx),
        mod_matrix_ui_type_from_ctx(target_ctx), target_ctx,
        &models);
}

static void mod_matrix_release_destination(mod_matrix_runtime_destination_t *dst)
{
    if ((dst == NULL) || (dst->valid == 0U))
    {
        return;
    }

    (void)mod_destination_catalog_apply_prepared(&dst->prepared,
                                                 dst->base_value);

    dst->valid = 0U;
    dst->modulation_active = 0U;
    dst->destination = MOD_DESTINATION_NONE;
    dst->base_value = 0.0f;
    dst->sum = 0.0f;
    dst->sum_end = 0.0f;
    dst->min_value = 0.0f;
    dst->max_value = 127.0f;
    dst->ramp = (mod_destination_ramp_t){0};
    dst->prepared = (mod_destination_prepared_t){0};
}

static mod_matrix_runtime_destination_t *mod_matrix_find_runtime_destination(mod_matrix_runtime_track_t *rt,
                                                                            mod_destination_address_t destination)
{
    if (rt == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        if ((rt->destinations[i].valid != 0U)
                && (rt->destinations[i].destination == destination))
        {
            return &rt->destinations[i];
        }
    }
    return NULL;
}

static mod_matrix_runtime_destination_t *mod_matrix_alloc_runtime_destination(mod_matrix_runtime_track_t *rt)
{
    if (rt == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        if (rt->destinations[i].valid == 0U)
        {
            return &rt->destinations[i];
        }
    }
    return NULL;
}

static uint8_t mod_matrix_runtime_destination_prepare(uint8_t track,
                                                      mod_matrix_runtime_track_t *rt,
                                                      mod_destination_address_t destination,
                                                      float min_value,
                                                      float max_value,
                                                      mod_matrix_runtime_destination_t **out_dst)
{
    uint8_t target = 0U;
    param_id_t param = PARAM_COUNT;
    if ((track >= SEQ_TRACK_COUNT) || (rt == NULL) || (out_dst == NULL)
            || (mod_destination_address_resolve(destination, &target, &param) == 0U))
    {
        return 0U;
    }

    mod_matrix_runtime_destination_t *dst = mod_matrix_find_runtime_destination(rt, destination);
    track_audio_runtime_ctx_t target_ctx_value;
    const track_audio_runtime_ctx_t *const target_ctx =
        (audio_note_engine_adapter_current_ctx(
            target, &target_ctx_value) != 0U) ? &target_ctx_value : NULL;
    mod_destination_prepared_t prepared;
    mod_destination_audio_models_t models;
    mod_matrix_audio_read_models(target, target_ctx, &models);
    if ((target_ctx == NULL)
            || (mod_destination_catalog_prepare(target, param, target_ctx,
                                                &models,
                                                &prepared) == 0U))
        return 0U;
    if (dst == NULL)
    {
        dst = mod_matrix_alloc_runtime_destination(rt);
        if (dst == NULL)
        {
            return 0U;
        }

        /* Temporary initialization only; the CONTROL parameter command that
         * accompanies a newly routed destination seeds the authoritative
         * value before the next render. */
        float base_value = param_registry[param].default_value;
        dst->valid = 1U;
        dst->modulation_active = 0U;
        dst->destination = destination;
        dst->base_value = base_value;
        dst->sum = 0.0f;
        dst->sum_end = 0.0f;
        dst->min_value = min_value;
        dst->max_value = max_value;
        dst->ramp = (mod_destination_ramp_t){0};
    }

    dst->prepared = prepared;

    *out_dst = dst;
    return 1U;
}

static void mod_matrix_restore_destination_value(
    mod_matrix_runtime_destination_t *dst)
{
    if ((dst == NULL) || (dst->valid == 0U))
    {
        return;
    }

    (void)mod_destination_catalog_apply_prepared(&dst->prepared,
                                                 dst->base_value);
}

static void mod_matrix_rebuild_track_plan(uint8_t track,
                                          ui_track_family_t family,
                                          ui_track_type_t type,
                                          const track_audio_runtime_ctx_t *ctx,
                                          const mod_matrix_audio_state_t *state)
{
    typedef struct
    {
        uint8_t source;
        uint16_t destination;
        float depth;
        float min_value;
        float max_value;
    } mod_matrix_track_candidate_t;

    mod_matrix_track_candidate_t candidates[MOD_MATRIX_SLOT_COUNT];
    mod_matrix_track_plan_t *const plan = &g_mod_matrix_track_plan[track];
    mod_matrix_runtime_track_t *const rt = &g_mod_matrix_runtime[track];
    uint8_t candidate_count = 0U;

    memset(plan, 0, sizeof(*plan));

    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        const track_mod_matrix_slot_t *const s = &state->slots[slot];
        if ((mod_matrix_slot_is_effective(track, s, family, type, ctx) != 0U)
                && (candidate_count < MOD_MATRIX_SLOT_COUNT))
        {
            candidates[candidate_count].source = s->source;
            candidates[candidate_count].destination = s->destination;
            candidates[candidate_count].depth = s->depth;
            uint8_t target = 0U;
            param_id_t destination = PARAM_COUNT;
            candidates[candidate_count].min_value = 0.0f;
            candidates[candidate_count].max_value = 127.0f;
            if (mod_destination_address_resolve(s->destination,
                                                &target, &destination) != 0U)
            {
                candidates[candidate_count].min_value = param_registry[destination].min;
                candidates[candidate_count].max_value = param_registry[destination].max;
                if ((destination == PARAM_AUDIO_FX_P1)
                        || (destination == PARAM_AUDIO_FX_B_P1))
                {
                    const uint8_t model = (destination == PARAM_AUDIO_FX_B_P1)
                        ? audio_fx_runtime_get_model(target, AUDIO_FX_SLOT_B)
                        : audio_fx_runtime_get_model(target, AUDIO_FX_SLOT_A);
                    if (model == AUDIO_FX_MODEL_DRIFT)
                        candidates[candidate_count].max_value =
                            FX_AUDIO_DRIFT_DELAY_MOD_MAX_CONTROL;
                }
            }
            ++candidate_count;
        }
    }

    for (uint8_t runtime_index = 0U; runtime_index < MOD_MATRIX_SLOT_COUNT; ++runtime_index)
    {
        mod_matrix_runtime_destination_t *const dst = &rt->destinations[runtime_index];
        uint8_t still_used = 0U;
        if ((dst->valid != 0U) && (dst->destination != MOD_DESTINATION_NONE))
        {
            for (uint8_t candidate = 0U; candidate < candidate_count; ++candidate)
            {
                if (candidates[candidate].destination == dst->destination)
                {
                    still_used = 1U;
                    break;
                }
            }
        }
        if ((dst->valid != 0U) && (still_used == 0U))
        {
            mod_matrix_release_destination(dst);
        }
    }

    for (uint8_t candidate = 0U; candidate < candidate_count; ++candidate)
    {
        uint8_t destination_index = plan->destination_count;
        for (uint8_t i = 0U; i < plan->destination_count; ++i)
        {
            const uint8_t runtime_index =
                plan->destinations[i].runtime_destination_index;
            if ((runtime_index < MOD_MATRIX_SLOT_COUNT)
                    && (rt->destinations[runtime_index].destination
                        == candidates[candidate].destination))
            {
                destination_index = i;
                break;
            }
        }

        if (destination_index == plan->destination_count)
        {
            mod_matrix_runtime_destination_t *dst = NULL;
            if ((plan->destination_count >= MOD_MATRIX_SLOT_COUNT)
                    || (mod_matrix_runtime_destination_prepare(
                            track,
                            rt,
                            candidates[candidate].destination,
                            candidates[candidate].min_value,
                            candidates[candidate].max_value,
                            &dst) == 0U))
            {
                continue;
            }

            const uint8_t runtime_index = (uint8_t)(dst - &rt->destinations[0]);
            if (runtime_index >= MOD_MATRIX_SLOT_COUNT)
            {
                continue;
            }

            destination_index = plan->destination_count;
            plan->destinations[destination_index].runtime_destination_index = runtime_index;
            ++plan->destination_count;
        }

        if (plan->route_count >= MOD_MATRIX_SLOT_COUNT)
        {
            continue;
        }

        mod_matrix_track_route_t *const route = &plan->routes[plan->route_count++];
        route->source = candidates[candidate].source;
        route->destination_index = destination_index;
        const uint8_t runtime_index =
            plan->destinations[destination_index].runtime_destination_index;
        route->scale = (candidates[candidate].depth / 127.0f)
            * (rt->destinations[runtime_index].max_value
               - rt->destinations[runtime_index].min_value);
        plan->destinations[destination_index].discontinuity_source_mask |=
            (uint16_t)(1U << candidates[candidate].source);
    }
}

void mod_matrix_set_defaults(track_mod_matrix_slot_t slots[MOD_MATRIX_SLOT_COUNT], uint8_t *selected_slot)
{
    if (slots != NULL)
    {
        for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
        {
            slots[slot].enabled = 0U;
            slots[slot].source = (uint8_t)MOD_MATRIX_SOURCE_NONE;
            slots[slot].destination = (uint16_t)MOD_DESTINATION_NONE;
            slots[slot].depth = 0.0f;
        }
        slots[0].source = (uint8_t)MOD_MATRIX_SOURCE_LFO1;
        slots[1].source = (uint8_t)MOD_MATRIX_SOURCE_LFO2;
        slots[2].source = (uint8_t)MOD_MATRIX_SOURCE_LFO3;
        slots[3].source = (uint8_t)MOD_MATRIX_SOURCE_ENV3;
    }

    if (selected_slot != NULL)
    {
        *selected_slot = 0U;
    }
}

void audio_mod_matrix_init(void)
{
    memset(g_mod_matrix_runtime, 0, sizeof(g_mod_matrix_runtime));
    memset(g_mod_matrix_operator_config, 0, sizeof(g_mod_matrix_operator_config));
    memset(g_mod_matrix_route_cache, 0, sizeof(g_mod_matrix_route_cache));
    memset(g_mod_matrix_track_plan, 0, sizeof(g_mod_matrix_track_plan));
    memset(g_mod_matrix_poly_plan, 0, sizeof(g_mod_matrix_poly_plan));
    memset(g_mod_matrix_operator_runtime, 0, sizeof(g_mod_matrix_operator_runtime));
    memset(g_mod_matrix_audio_state, 0, sizeof(g_mod_matrix_audio_state));
    g_mod_matrix_audio_dirty_mask = 0U;
    g_mod_matrix_any_route = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint8_t selected = 0U;
        mod_matrix_set_defaults(g_mod_matrix_audio_state[track].slots, &selected);
        g_mod_matrix_audio_state[track].multi_source[0][0] =
            (uint8_t)param_registry[PARAM_MOD_MULTI_1_A].default_value;
        g_mod_matrix_audio_state[track].multi_source[0][1] =
            (uint8_t)param_registry[PARAM_MOD_MULTI_1_B].default_value;
        g_mod_matrix_audio_state[track].multi_source[1][0] =
            (uint8_t)param_registry[PARAM_MOD_MULTI_2_A].default_value;
        g_mod_matrix_audio_state[track].multi_source[1][1] =
            (uint8_t)param_registry[PARAM_MOD_MULTI_2_B].default_value;
        g_mod_matrix_audio_state[track].slew_source[0] =
            (uint8_t)param_registry[PARAM_MOD_SLEW_1_SOURCE].default_value;
        g_mod_matrix_audio_state[track].slew_source[1] =
            (uint8_t)param_registry[PARAM_MOD_SLEW_2_SOURCE].default_value;
        g_mod_matrix_audio_state[track].slew_amount[0] =
            param_registry[PARAM_MOD_SLEW_1_AMOUNT].default_value;
        g_mod_matrix_audio_state[track].slew_amount[1] =
            param_registry[PARAM_MOD_SLEW_2_AMOUNT].default_value;
        for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
        {
            g_mod_matrix_runtime[track].destinations[i].destination = (uint16_t)MOD_DESTINATION_NONE;
            g_mod_matrix_runtime[track].destinations[i].max_value = 127.0f;
        }
    }
    audio_mod_destination_catalog_reset_runtime();
}

static uint16_t mod_matrix_required_mask_expand(uint8_t track,
                                                uint8_t source,
                                                uint8_t depth)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (source == (uint8_t)MOD_MATRIX_SOURCE_NONE)
            || (source >= (uint8_t)MOD_MATRIX_SOURCE_COUNT)
            || (depth >= 4U))
        return 0U;

    const uint16_t own = (uint16_t)(1U << source);
    const mod_matrix_operator_config_t *const config =
        &g_mod_matrix_operator_config[track];
    if ((source == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
            || (source == (uint8_t)MOD_MATRIX_SOURCE_MULTI2))
    {
        const uint8_t op = (source == (uint8_t)MOD_MATRIX_SOURCE_MULTI1) ? 0U : 1U;
        const uint8_t a = config->multi_source[op][0];
        const uint8_t b = config->multi_source[op][1];
        if ((a == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                || (a == (uint8_t)MOD_MATRIX_SOURCE_MULTI2)
                || (b == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                || (b == (uint8_t)MOD_MATRIX_SOURCE_MULTI2))
            return own;
        return (uint16_t)(own
            | mod_matrix_required_mask_expand(track, a, (uint8_t)(depth + 1U))
            | mod_matrix_required_mask_expand(track, b, (uint8_t)(depth + 1U)));
    }
    if ((source == (uint8_t)MOD_MATRIX_SOURCE_SLEW1)
            || (source == (uint8_t)MOD_MATRIX_SOURCE_SLEW2))
    {
        const uint8_t op = (source == (uint8_t)MOD_MATRIX_SOURCE_SLEW1) ? 0U : 1U;
        const uint8_t input = config->slew_source[op];
        const uint8_t self = source;
        const uint8_t other = (op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW2
                                         : (uint8_t)MOD_MATRIX_SOURCE_SLEW1;
        if ((input == self)
                || ((input == other) && (config->slew_source[1U - op] == self)))
            return own;
        return (uint16_t)(own | mod_matrix_required_mask_expand(
            track, input, (uint8_t)(depth + 1U)));
    }
    return own;
}

static void mod_matrix_audio_rebuild_route_cache_track(
    uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    mod_matrix_poly_plan_t *const plan = &g_mod_matrix_poly_plan[track];
    memset(plan, 0, sizeof(*plan));
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    const ui_track_family_t family = mod_matrix_ui_family_from_ctx(ctx);
    const ui_track_type_t type = mod_matrix_ui_type_from_ctx(ctx);
    const mod_matrix_audio_state_t *const state = &g_mod_matrix_audio_state[track];
    mod_matrix_rebuild_track_plan(track, family, type, ctx, state);
    if (ctx != NULL)
    {
        for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
        {
            const track_mod_matrix_slot_t *const s = &state->slots[slot];
            uint8_t target = 0U;
            param_id_t destination = PARAM_COUNT;
            if ((mod_matrix_slot_is_effective(track, s, family, type, ctx) == 0U)
                    || (mod_destination_address_resolve(
                        s->destination, &target, &destination) == 0U)
                    || (target != track)
                    || (s->source < (uint8_t)MOD_MATRIX_SOURCE_LFO1)
                    || (s->source > (uint8_t)MOD_MATRIX_SOURCE_LFO3)) continue;
            const uint8_t lfo = (uint8_t)(s->source - (uint8_t)MOD_MATRIX_SOURCE_LFO1);
            const mod_lfo_trig_mode_t trig = mod_lfo_v1_effective_trig(track, lfo);
            if ((trig < MOD_LFO_TRIG_POLY_TRIG)
                    || (mod_destination_catalog_poly_voice_supported(
                        destination, ctx) == 0U)) continue;

            uint8_t dst_index = plan->destination_count;
            for (uint8_t i = 0U; i < plan->destination_count; ++i)
            {
                const uint8_t runtime_index =
                    plan->destinations[i].runtime_destination_index;
                if ((runtime_index < MOD_MATRIX_SLOT_COUNT)
                        && (g_mod_matrix_runtime[track].destinations[runtime_index].destination
                            == s->destination))
                {
                    dst_index = i;
                    break;
                }
            }
            if (dst_index == plan->destination_count)
            {
                mod_matrix_runtime_destination_t *const runtime_destination =
                    mod_matrix_find_runtime_destination(
                        &g_mod_matrix_runtime[track], s->destination);
                if (runtime_destination == NULL)
                    continue;
                const uint8_t runtime_index = (uint8_t)(runtime_destination
                    - &g_mod_matrix_runtime[track].destinations[0]);
                if (runtime_index >= MOD_MATRIX_SLOT_COUNT)
                    continue;
                plan->destinations[dst_index].runtime_destination_index = runtime_index;
                plan->destination_count++;
            }
            const uint8_t runtime_index =
                plan->destinations[dst_index].runtime_destination_index;
            mod_matrix_poly_route_t *const route = &plan->routes[plan->route_count++];
            route->source = s->source;
            route->destination_index = dst_index;
            route->scale = (s->depth / 127.0f)
                * (g_mod_matrix_runtime[track].destinations[runtime_index].max_value
                    - g_mod_matrix_runtime[track].destinations[runtime_index].min_value);
            plan->source_mask |= (uint8_t)(1U << lfo);
        }
    }
    uint16_t required_source_mask = 0U;
    const mod_matrix_track_plan_t *const track_plan = &g_mod_matrix_track_plan[track];
    for (uint8_t route = 0U; route < track_plan->route_count; ++route)
        required_source_mask |= mod_matrix_required_mask_expand(
            track, track_plan->routes[route].source, 0U);
    g_mod_matrix_route_cache[track].required_source_mask = required_source_mask;
    g_mod_matrix_route_cache[track].any_route =
        (track_plan->route_count != 0U) ? 1U : 0U;
    mixer_invalidate_external_poly_track(track);
    mod_matrix_recompute_global_route_flag();
}

uint8_t audio_mod_matrix_apply_param(uint8_t track, uint8_t slot,
                                     param_id_t id, float value)
{
    if (track >= SEQ_TRACK_COUNT) return 0U;
    mod_matrix_audio_state_t *const state = &g_mod_matrix_audio_state[track];
    if (slot < MOD_MATRIX_SLOT_COUNT)
    {
        track_mod_matrix_slot_t *const s = &state->slots[slot];
        if (id == PARAM_MOD_MATRIX_SOURCE)
            s->source = (uint8_t)mod_matrix_clampf(value, 0.0f,
                (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
        else if (id == PARAM_MOD_MATRIX_DEST)
            s->destination = (mod_destination_address_t)value;
        else if (id == PARAM_MOD_MATRIX_DEPTH)
            s->depth = mod_matrix_clampf(value, -127.0f, 127.0f);
        else if (id == PARAM_MOD_MATRIX_SLOT)
            s->enabled = (value >= 0.5f) ? 1U : 0U;
        else return 0U;
    }
    else
    {
        uint8_t op = 0U;
        if (id == PARAM_MOD_MULTI_1_A) state->multi_source[0][0] = (uint8_t)value;
        else if (id == PARAM_MOD_MULTI_1_B) state->multi_source[0][1] = (uint8_t)value;
        else if (id == PARAM_MOD_MULTI_2_A) state->multi_source[1][0] = (uint8_t)value;
        else if (id == PARAM_MOD_MULTI_2_B) state->multi_source[1][1] = (uint8_t)value;
        else if ((id == PARAM_MOD_SLEW_1_SOURCE)
                || (id == PARAM_MOD_SLEW_2_SOURCE))
        {
            op = (id == PARAM_MOD_SLEW_2_SOURCE) ? 1U : 0U;
            const uint8_t source = (uint8_t)value;
            if (state->slew_source[op] != source)
            {
                state->slew_source[op] = source;
                g_mod_matrix_operator_runtime[track].slew[op] = 0.0f;
                g_mod_matrix_operator_runtime[track].slew_valid[op] = 0U;
            }
        }
        else if (id == PARAM_MOD_SLEW_1_AMOUNT) state->slew_amount[0] = value;
        else if (id == PARAM_MOD_SLEW_2_AMOUNT) state->slew_amount[1] = value;
        else return 0U;
    }
    g_mod_matrix_audio_dirty_mask |= (uint16_t)(1U << track);
    return 1U;
}

void audio_mod_matrix_rebuild_track(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT) return;
    for (uint8_t owner = 0U; owner < SEQ_TRACK_COUNT; ++owner)
    {
        uint8_t depends = (owner == track) ? 1U : 0U;
        for (uint8_t slot = 0U; (slot < MOD_MATRIX_SLOT_COUNT) && !depends; ++slot)
        {
            uint8_t target = 0U; param_id_t destination = PARAM_COUNT;
            const track_mod_matrix_slot_t *const s =
                &g_mod_matrix_audio_state[owner].slots[slot];
            if (s->enabled && mod_destination_address_resolve(
                    s->destination, &target, &destination) && (target == track))
                depends = 1U;
        }
        if (depends) g_mod_matrix_audio_dirty_mask |= (uint16_t)(1U << owner);
    }
}

void audio_mod_matrix_finalize_dirty(void)
{
    const uint16_t dirty = g_mod_matrix_audio_dirty_mask;
    g_mod_matrix_audio_dirty_mask = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if ((dirty & (uint16_t)(1U << track)) == 0U) continue;
        mod_matrix_operator_config_t *const config = &g_mod_matrix_operator_config[track];
        const mod_matrix_audio_state_t *const state = &g_mod_matrix_audio_state[track];
        for (uint8_t op = 0U; op < 2U; ++op)
        {
            config->multi_source[op][0] = state->multi_source[op][0];
            config->multi_source[op][1] = state->multi_source[op][1];
            config->slew_source[op] = state->slew_source[op];
            config->slew_amount[op] = state->slew_amount[op];
        }
        track_audio_runtime_ctx_t ctx;
        config->drum_md_slot_count = 0U;
        if ((audio_note_engine_adapter_current_ctx(track, &ctx) != 0U)
                && (ctx.type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_MD))
            config->drum_md_slot_count = md_model_profile_get(
                drum_synth_get_md_model_for_instance(
                    ctx.program_route.instance_id))->slot_count;
        mod_matrix_audio_rebuild_route_cache_track(track);
    }
}

uint8_t mod_matrix_poly_route_mask(uint8_t track)
{
    return (track < SEQ_TRACK_COUNT) ? g_mod_matrix_poly_plan[track].source_mask : 0U;
}

uint8_t mod_matrix_set_selected_slot(uint8_t track, float value)
{
    track_sound_state_t *const state = mod_matrix_control_state(track);
    if (state == NULL)
    {
        return 0U;
    }

    state->mod_matrix_selected_slot = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SLOT_COUNT - 1U));
    return 1U;
}

uint8_t mod_matrix_get_selected_slot(uint8_t track, float *out_value)
{
    uint8_t slot = 0U;
    if ((out_value == NULL) || (mod_matrix_track_selected_slot(track, &slot) == 0U))
    {
        return 0U;
    }

    *out_value = (float)slot;
    return 1U;
}

uint8_t mod_matrix_set_selected_slot_destination_index(uint8_t track, float value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_set_slot_destination_index(track, slot, value)
        : 0U;
}

uint8_t mod_matrix_set_selected_slot_depth(uint8_t track, float value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_set_slot_depth(track, slot, value)
        : 0U;
}

uint8_t mod_matrix_set_selected_slot_source(uint8_t track, float value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_set_slot_source(track, slot, value)
        : 0U;
}

uint8_t mod_matrix_get_selected_slot_destination_index(uint8_t track, float *out_value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_get_slot_destination_index(track, slot, out_value)
        : 0U;
}

uint8_t mod_matrix_get_selected_slot_depth(uint8_t track, float *out_value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_get_slot_depth(track, slot, out_value)
        : 0U;
}

uint8_t mod_matrix_get_selected_slot_source(uint8_t track, float *out_value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_get_slot_source(track, slot, out_value)
        : 0U;
}

enum { MOD_MATRIX_FIELD_SOURCE = 1U, MOD_MATRIX_FIELD_DEST = 2U,
       MOD_MATRIX_FIELD_DEPTH = 4U, MOD_MATRIX_FIELD_ENABLED = 8U };

static uint8_t mod_matrix_publish_slot_fields(uint8_t track, uint8_t slot,
                                              uint8_t fields)
{
    brick_entity_id_t owner = track;
    if ((entity_topology_mod_owner(track, &owner) == 0U)
            || (owner >= SEQ_TRACK_COUNT) || (slot >= MOD_MATRIX_SLOT_COUNT)) return 0U;
    const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(owner, slot);
    if (s == NULL) return 0U;
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK, .count = 0U
    };
#define ADD_MATRIX_FIELD(condition, parameter, field_value) do { \
    if (condition) bulk.item[bulk.count++] = (live_parameter_audio_bulk_item_t){ \
        .parameter_id = (uint16_t)(parameter), .scope = LIVE_PARAMETER_EVENT_SCOPE_SLOT, \
        .track = owner, .slot = slot, \
        .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET \
            | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS), \
        .value = live_parameter_event_encode_float(field_value) }; \
} while (0)
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_SOURCE) != 0U,
                     PARAM_MOD_MATRIX_SOURCE, (float)s->source);
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_DEST) != 0U,
                     PARAM_MOD_MATRIX_DEST, (float)s->destination);
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_DEPTH) != 0U,
                     PARAM_MOD_MATRIX_DEPTH, s->depth);
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_ENABLED) != 0U,
                     PARAM_MOD_MATRIX_SLOT, (float)(s->enabled != 0U));
#undef ADD_MATRIX_FIELD
    return live_parameter_audio_publication_submit_bulk(&bulk) ? 1U : 0U;
}

static uint8_t mod_matrix_publish_operator(uint8_t track, param_id_t id,
                                           float value)
{
    brick_entity_id_t owner = track;
    if ((entity_topology_mod_owner(track, &owner) == 0U)
            || (owner >= SEQ_TRACK_COUNT)) return 0U;
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK, .count = 1U,
        .item = {{ .parameter_id = (uint16_t)id,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK, .track = owner,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
            .value = live_parameter_event_encode_float(value) }}
    };
    return live_parameter_audio_publication_submit_bulk(&bulk) ? 1U : 0U;
}

uint8_t mod_matrix_set_slot_destination_index(uint8_t track, uint8_t slot, float value)
{
    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U)
    {
        return 0U;
    }
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL)
    {
        return 0U;
    }

    const uint16_t max_index = (uint16_t)(mod_destination_catalog_count(owner) - 1U);
    const uint16_t index = (uint16_t)mod_matrix_clampf(value, 0.0f, (float)max_index);
    const mod_destination_address_t new_destination =
        mod_destination_catalog_address_from_index(owner, index);
    const uint8_t enabled = ((new_destination != MOD_DESTINATION_NONE)
                  && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)) ? 1U : 0U;
    if ((s->destination == new_destination) && (s->enabled == enabled))
        return 1U;
    s->destination = new_destination;
    s->enabled = enabled;
    uint8_t target = 0U;
    param_id_t destination = PARAM_COUNT;
    float base = 0.0f;
    if ((mod_destination_address_resolve(s->destination, &target, &destination) != 0U)
            && (param_registry_get_track_value(destination, target, &base) != 0U))
        (void)param_registry_project_track_base_audio(destination, target, base);
    return mod_matrix_publish_slot_fields(owner, slot,
        MOD_MATRIX_FIELD_DEST | MOD_MATRIX_FIELD_ENABLED);
}

uint8_t mod_matrix_set_slot_depth(uint8_t track, uint8_t slot, float value)
{
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL)
    {
        return 0U;
    }

    const float depth = mod_matrix_clampf(value, -127.0f, 127.0f);
    if (s->depth == depth)
        return 1U;
    s->depth = depth;
    return mod_matrix_publish_slot_fields(track, slot, MOD_MATRIX_FIELD_DEPTH);
}

uint8_t mod_matrix_set_slot_source(uint8_t track, uint8_t slot, float value)
{
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL)
    {
        return 0U;
    }

    const uint8_t source = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
    const uint8_t enabled = ((s->destination != (uint16_t)MOD_DESTINATION_NONE)
                  && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)) ? 1U : 0U;
    if ((s->source == source) && (s->enabled == enabled))
        return 1U;
    s->source = source;
    s->enabled = ((s->destination != (uint16_t)MOD_DESTINATION_NONE)
                  && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)) ? 1U : 0U;
    return mod_matrix_publish_slot_fields(track, slot,
        MOD_MATRIX_FIELD_SOURCE | MOD_MATRIX_FIELD_ENABLED);
}

uint8_t mod_matrix_set_slot_enabled(uint8_t track, uint8_t slot, float value)
{
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL) return 0U;
    const uint8_t enabled = ((value >= 0.5f)
            && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)
            && (s->destination != MOD_DESTINATION_NONE)) ? 1U : 0U;
    if (s->enabled == enabled)
        return 1U;
    s->enabled = enabled;
    return mod_matrix_publish_slot_fields(track, slot, MOD_MATRIX_FIELD_ENABLED);
}

uint8_t mod_matrix_set_slot_state(uint8_t track,
                                  uint8_t slot,
                                  uint8_t source,
                                  mod_destination_address_t destination,
                                  float depth,
                                  uint8_t enabled)
{
    brick_entity_id_t owner = track;
    if ((source >= MOD_MATRIX_SOURCE_COUNT)
            || (entity_topology_mod_owner(track, &owner) == 0U)
            || ((destination != MOD_DESTINATION_NONE)
                && (mod_destination_catalog_index_from_address(owner, destination) == UINT16_MAX)))
    {
        return 0U;
    }
    track_mod_matrix_slot_t *const state = mod_matrix_track_slot_mut(track, slot);
    if (state == NULL)
    {
        return 0U;
    }
    const float clamped_depth = mod_matrix_clampf(depth, -127.0f, 127.0f);
    const uint8_t effective_enabled = ((enabled != 0U)
            && (source != (uint8_t)MOD_MATRIX_SOURCE_NONE)
            && (destination != MOD_DESTINATION_NONE)) ? 1U : 0U;
    if ((state->source == source) && (state->destination == destination)
            && (state->depth == clamped_depth)
            && (state->enabled == effective_enabled))
        return 1U;
    state->source = source;
    state->destination = destination;
    state->depth = clamped_depth;
    state->enabled = effective_enabled;
    return mod_matrix_publish_slot_fields(owner, slot,
        MOD_MATRIX_FIELD_SOURCE | MOD_MATRIX_FIELD_DEST
        | MOD_MATRIX_FIELD_DEPTH | MOD_MATRIX_FIELD_ENABLED);
}

uint8_t mod_matrix_get_slot_destination_index(uint8_t track, uint8_t slot, float *out_value)
{
    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U)
    {
        return 0U;
    }
    const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
    if ((s == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = (float)mod_destination_catalog_index_from_address(owner, s->destination);
    return 1U;
}

uint8_t mod_matrix_get_slot_depth(uint8_t track, uint8_t slot, float *out_value)
{
    const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
    if ((s == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = s->depth;
    return 1U;
}

uint8_t mod_matrix_get_slot_source(uint8_t track, uint8_t slot, float *out_value)
{
    const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
    if ((s == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = (float)s->source;
    return 1U;
}

uint8_t mod_matrix_set_multi_source(uint8_t track, uint8_t op, uint8_t input, float value)
{
    if ((op >= 2U) || (input >= 2U))
    {
        return 0U;
    }

    const uint8_t source = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
    static const param_id_t ids[2][2] = {
        { PARAM_MOD_MULTI_1_A, PARAM_MOD_MULTI_1_B },
        { PARAM_MOD_MULTI_2_A, PARAM_MOD_MULTI_2_B }
    };
    float current = 0.0f;
    if ((param_registry_control_value_get(track, ids[op][input], &current) != 0U)
            && ((uint8_t)(current + 0.5f) == source)) return 1U;
    param_registry_control_value_set(track, ids[op][input], (float)source);
    return mod_matrix_publish_operator(track, ids[op][input], (float)source);
}

uint8_t mod_matrix_get_multi_source(uint8_t track, uint8_t op, uint8_t input, float *out_value)
{
    if ((op >= 2U) || (input >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    static const param_id_t ids[2][2] = {
        { PARAM_MOD_MULTI_1_A, PARAM_MOD_MULTI_1_B },
        { PARAM_MOD_MULTI_2_A, PARAM_MOD_MULTI_2_B } };
    return param_registry_control_value_get(track, ids[op][input], out_value);
}

uint8_t mod_matrix_set_slew_source(uint8_t track, uint8_t op, float value)
{
    if (op >= 2U)
    {
        return 0U;
    }

    const uint8_t source = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
    const param_id_t id = (op == 0U) ? PARAM_MOD_SLEW_1_SOURCE
                                      : PARAM_MOD_SLEW_2_SOURCE;
    float current = 0.0f;
    if ((param_registry_control_value_get(track, id, &current) != 0U)
            && ((uint8_t)(current + 0.5f) == source)) return 1U;
    param_registry_control_value_set(track, id, (float)source);
    return mod_matrix_publish_operator(track, id, (float)source);
}

uint8_t mod_matrix_get_slew_source(uint8_t track, uint8_t op, float *out_value)
{
    if ((op >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    return param_registry_control_value_get(track,
        (op == 0U) ? PARAM_MOD_SLEW_1_SOURCE : PARAM_MOD_SLEW_2_SOURCE,
        out_value);
}

uint8_t mod_matrix_set_slew_amount(uint8_t track, uint8_t op, float value)
{
    if (op >= 2U)
    {
        return 0U;
    }

    const float amount = mod_matrix_clampf(value, 0.0f, 1.0f);
    const param_id_t id = (op == 0U) ? PARAM_MOD_SLEW_1_AMOUNT
                                      : PARAM_MOD_SLEW_2_AMOUNT;
    float current = 0.0f;
    if ((param_registry_control_value_get(track, id, &current) != 0U)
            && (current == amount)) return 1U;
    param_registry_control_value_set(track, id, amount);
    return mod_matrix_publish_operator(track, id, amount);
}

uint8_t mod_matrix_get_slew_amount(uint8_t track, uint8_t op, float *out_value)
{
    if ((op >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    return param_registry_control_value_get(track,
        (op == 0U) ? PARAM_MOD_SLEW_1_AMOUNT : PARAM_MOD_SLEW_2_AMOUNT,
        out_value);
}

uint8_t mod_matrix_has_any_configured_route(void)
{
    return g_mod_matrix_any_route;
}

uint8_t mod_matrix_track_has_configured_route(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    return g_mod_matrix_route_cache[track].any_route;
}

uint16_t mod_matrix_required_source_mask(uint8_t track)
{
    return (track < SEQ_TRACK_COUNT)
        ? g_mod_matrix_route_cache[track].required_source_mask : 0U;
}

static uint8_t mod_matrix_audio_slew_direct_cycle(uint8_t track, uint8_t op)
{
    if ((track >= SEQ_TRACK_COUNT) || (op >= 2U))
    {
        return 1U;
    }
    const mod_matrix_operator_config_t *const config =
        &g_mod_matrix_operator_config[track];
    const uint8_t src = config->slew_source[op];
    const uint8_t self = (op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1
                                    : (uint8_t)MOD_MATRIX_SOURCE_SLEW2;
    const uint8_t other = (op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW2
                                     : (uint8_t)MOD_MATRIX_SOURCE_SLEW1;
    if (src == self)
        return 1U;
    if (src == other)
    {
        if (config->slew_source[1U - op] == self)
            return 1U;
    }
    return 0U;
}

static uint8_t mod_matrix_track_has_operator_route(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    const uint16_t mask = g_mod_matrix_route_cache[track].required_source_mask;
    return ((mask & ((uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                    | (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_MULTI2)
                    | (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_SLEW1)
                    | (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_SLEW2))) != 0U) ? 1U : 0U;
}

static uint8_t mod_matrix_get_operator_source_value(const mod_matrix_operator_runtime_t *rt,
                                                    const float source_values[MOD_MATRIX_SOURCE_COUNT],
                                                    const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                                    uint8_t source,
                                                    float *out)
{
    if ((out == NULL) || (source >= (uint8_t)MOD_MATRIX_SOURCE_COUNT))
    {
        return 0U;
    }

    switch ((mod_matrix_source_t)source)
    {
        case MOD_MATRIX_SOURCE_MULTI1:
        case MOD_MATRIX_SOURCE_MULTI2:
        {
            const uint8_t op = (source == (uint8_t)MOD_MATRIX_SOURCE_MULTI1) ? 0U : 1U;
            if ((rt == NULL) || (rt->multi_valid[op] == 0U))
            {
                return 0U;
            }
            *out = rt->multi[op];
            return 1U;
        }
        case MOD_MATRIX_SOURCE_SLEW1:
        case MOD_MATRIX_SOURCE_SLEW2:
        {
            const uint8_t op = (source == (uint8_t)MOD_MATRIX_SOURCE_SLEW1) ? 0U : 1U;
            if ((rt == NULL) || (rt->slew_valid[op] == 0U))
            {
                return 0U;
            }
            *out = rt->slew[op];
            return 1U;
        }
        default:
            if ((source_valid == NULL) || (source_values == NULL) || (source_valid[source] == 0U))
            {
                return 0U;
            }
            *out = source_values[source];
            return 1U;
    }
}

void mod_matrix_process_operators_ramped(uint8_t track,
                                         float source_start[MOD_MATRIX_SOURCE_COUNT],
                                         float source_end[MOD_MATRIX_SOURCE_COUNT],
                                         uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                         uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT],
                                         uint32_t elapsed_frames)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (source_start == NULL)
            || (source_end == NULL)
            || (source_valid == NULL)
            || (source_discontinuous == NULL))
    {
        return;
    }
    if (mod_matrix_track_has_operator_route(track) == 0U)
    {
        return;
    }

    const mod_matrix_operator_config_t *const config =
        &g_mod_matrix_operator_config[track];
    mod_matrix_operator_runtime_t *const rt = &g_mod_matrix_operator_runtime[track];
    const uint16_t required_mask =
        g_mod_matrix_route_cache[track].required_source_mask;

    for (uint8_t op = 0U; op < 2U; ++op)
    {
        const uint8_t output = (op == 0U)
            ? (uint8_t)MOD_MATRIX_SOURCE_MULTI1
            : (uint8_t)MOD_MATRIX_SOURCE_MULTI2;
        if ((required_mask & (uint16_t)(1U << output)) == 0U)
        {
            rt->multi_valid[op] = 0U;
            source_valid[output] = 0U;
            source_discontinuous[output] = 0U;
            continue;
        }
        const uint8_t src_a = config->multi_source[op][0];
        const uint8_t src_b = config->multi_source[op][1];
        float a_start = 0.0f;
        float b_start = 0.0f;
        float a_end = 0.0f;
        float b_end = 0.0f;
        const uint8_t invalid_src = (uint8_t)((src_a == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                                             || (src_a == (uint8_t)MOD_MATRIX_SOURCE_MULTI2)
                                             || (src_b == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                                             || (src_b == (uint8_t)MOD_MATRIX_SOURCE_MULTI2));
        if ((invalid_src == 0U)
                && (mod_matrix_get_operator_source_value(rt, source_start, source_valid, src_a, &a_start) != 0U)
                && (mod_matrix_get_operator_source_value(rt, source_start, source_valid, src_b, &b_start) != 0U)
                && (mod_matrix_get_operator_source_value(rt, source_end, source_valid, src_a, &a_end) != 0U)
                && (mod_matrix_get_operator_source_value(rt, source_end, source_valid, src_b, &b_end) != 0U))
        {
            const float start = mod_matrix_clampf(a_start * b_start, -1.0f, 1.0f);
            const float end = mod_matrix_clampf(a_end * b_end, -1.0f, 1.0f);
            rt->multi[op] = end;
            rt->multi_valid[op] = 1U;
            source_start[output] = start;
            source_end[output] = end;
            source_valid[output] = 1U;
            source_discontinuous[output] = (uint8_t)(source_discontinuous[src_a]
                || source_discontinuous[src_b]);
        }
        else
        {
            rt->multi_valid[op] = 0U;
            source_discontinuous[(op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_MULTI1 : (uint8_t)MOD_MATRIX_SOURCE_MULTI2] = 0U;
        }
    }

    for (uint8_t op = 0U; op < 2U; ++op)
    {
        const uint8_t output = (op == 0U)
            ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1
            : (uint8_t)MOD_MATRIX_SOURCE_SLEW2;
        if ((required_mask & (uint16_t)(1U << output)) == 0U)
        {
            rt->slew_valid[op] = 0U;
            source_valid[output] = 0U;
            source_discontinuous[output] = 0U;
            continue;
        }
        const uint8_t src = config->slew_source[op];
        float input_start = 0.0f;
        float input_end = 0.0f;
        if ((mod_matrix_audio_slew_direct_cycle(track, op) == 0U)
                && (mod_matrix_get_operator_source_value(rt, source_start, source_valid, src, &input_start) != 0U)
                && (mod_matrix_get_operator_source_value(rt, source_end, source_valid, src, &input_end) != 0U))
        {
            const float amount = mod_matrix_clampf(config->slew_amount[op], 0.0f, 1.0f);
            const float tau_frames = 16.0f + (amount * amount * 48000.0f);
            const float elapsed = (elapsed_frames == 0U) ? 1.0f : (float)elapsed_frames;
            const float coeff = (amount <= 0.0f) ? 1.0f : (elapsed / (tau_frames + elapsed));
            float start = input_start;
            if (rt->slew_valid[op] == 0U)
            {
                rt->slew[op] = input_start;
            }
            else
            {
                start = rt->slew[op];
            }
            const float end = mod_matrix_clampf(start + (input_end - start) * coeff, -1.0f, 1.0f);
            rt->slew[op] = end;
            rt->slew_valid[op] = 1U;
            source_start[output] = start;
            source_end[output] = end;
            source_valid[output] = 1U;
            source_discontinuous[output] = source_discontinuous[src];
        }
        else
        {
            rt->slew_valid[op] = 0U;
            source_discontinuous[(op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1 : (uint8_t)MOD_MATRIX_SOURCE_SLEW2] = 0U;
        }
    }
}

void mod_matrix_process_operators(uint8_t track,
                                  float source_values[MOD_MATRIX_SOURCE_COUNT],
                                  uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                  uint32_t elapsed_frames)
{
    if ((source_values == NULL) || (source_valid == NULL))
    {
        return;
    }

    float source_end[MOD_MATRIX_SOURCE_COUNT];
    uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT] = {0U};
    memcpy(source_end, source_values, sizeof(source_end));
    mod_matrix_process_operators_ramped(track,
                                        source_values,
                                        source_end,
                                        source_valid,
                                        source_discontinuous,
                                        elapsed_frames);
}

void mod_matrix_process_track_ramped(uint8_t track,
                                     const track_audio_runtime_ctx_t *ctx,
                                     const float source_start[MOD_MATRIX_SOURCE_COUNT],
                                     const float source_end[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT],
                                     uint32_t elapsed_frames)
{
    (void)ctx;
    if ((track >= SEQ_TRACK_COUNT)
            || (source_start == NULL)
            || (source_end == NULL)
            || (source_valid == NULL)
            || (source_discontinuous == NULL))
    {
        return;
    }
    const mod_matrix_track_plan_t *const plan = &g_mod_matrix_track_plan[track];
    if (plan->route_count == 0U)
    {
        return;
    }

    mod_matrix_runtime_track_t *const rt = &g_mod_matrix_runtime[track];
    uint8_t touched[MOD_MATRIX_SLOT_COUNT] = {0};
    uint16_t discontinuity_source_mask = 0U;

    for (uint8_t source = 0U; source < MOD_MATRIX_SOURCE_COUNT; ++source)
    {
        if (source_discontinuous[source] != 0U)
        {
            discontinuity_source_mask |= (uint16_t)(1U << source);
        }
    }

    for (uint8_t i = 0U; i < plan->destination_count; ++i)
    {
        const uint8_t runtime_index = plan->destinations[i].runtime_destination_index;
        if (runtime_index >= MOD_MATRIX_SLOT_COUNT)
        {
            continue;
        }
        rt->destinations[runtime_index].sum = 0.0f;
        rt->destinations[runtime_index].sum_end = 0.0f;
    }

    for (uint8_t i = 0U; i < plan->route_count; ++i)
    {
        const mod_matrix_track_route_t *const route = &plan->routes[i];
        if ((route->source >= MOD_MATRIX_SOURCE_COUNT)
                || (source_valid[route->source] == 0U)
                || (route->destination_index >= plan->destination_count))
        {
            continue;
        }

        const uint8_t runtime_index = plan->destinations[route->destination_index].runtime_destination_index;
        if (runtime_index >= MOD_MATRIX_SLOT_COUNT)
        {
            continue;
        }

        mod_matrix_runtime_destination_t *const dst = &rt->destinations[runtime_index];
        touched[route->destination_index] = 1U;
        dst->modulation_active = 1U;
        dst->sum += source_start[route->source] * route->scale;
        dst->sum_end += source_end[route->source] * route->scale;
    }

    for (uint8_t i = 0U; i < plan->destination_count; ++i)
    {
        const mod_matrix_track_destination_t *const planned = &plan->destinations[i];
        const uint8_t runtime_index = planned->runtime_destination_index;
        if (runtime_index >= MOD_MATRIX_SLOT_COUNT)
        {
            continue;
        }

        mod_matrix_runtime_destination_t *const dst = &rt->destinations[runtime_index];
        if (touched[i] == 0U)
        {
            if (dst->modulation_active != 0U)
            {
                mod_matrix_restore_destination_value(dst);
                dst->modulation_active = 0U;
            }
            dst->sum = 0.0f;
            dst->sum_end = 0.0f;
            dst->ramp = (mod_destination_ramp_t){0};
            continue;
        }

        const float start = mod_matrix_clampf(dst->base_value + dst->sum,
                                              dst->min_value,
                                              dst->max_value);
        const float end = mod_matrix_clampf(dst->base_value + dst->sum_end,
                                            dst->min_value,
                                            dst->max_value);
        const uint8_t discontinuous =
            ((planned->discontinuity_source_mask & discontinuity_source_mask) != 0U) ? 1U : 0U;
        mod_destination_ramp_prepare(start,
                                     end,
                                     elapsed_frames,
                                     discontinuous,
                                     &dst->ramp);
        (void)mod_destination_catalog_apply_ramp_prepared(
            &dst->prepared, &dst->ramp);
    }
}

void mod_matrix_process_track(uint8_t track,
                              const track_audio_runtime_ctx_t *ctx,
                              const float source_values[MOD_MATRIX_SOURCE_COUNT],
                              const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT])
{
    if ((source_values == NULL) || (source_valid == NULL))
    {
        return;
    }

    float source_end[MOD_MATRIX_SOURCE_COUNT];
    uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT] = {0U};
    memcpy(source_end, source_values, sizeof(source_end));
    mod_matrix_process_track_ramped(track,
                                    ctx,
                                    source_values,
                                    source_end,
                                    source_valid,
                                    source_discontinuous,
                                    1U);
}

void mod_matrix_process_poly_voice_ramped(uint8_t track,
                                          uint8_t voice_slot,
                                          const track_audio_runtime_ctx_t *ctx,
                                          const float source_start[MOD_MATRIX_SOURCE_COUNT],
                                          const float source_end[MOD_MATRIX_SOURCE_COUNT],
                                          const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT])
{
    (void)ctx;
    if ((track >= SEQ_TRACK_COUNT) || (source_start == NULL)
            || (source_end == NULL) || (source_valid == NULL))
    {
        return;
    }

    const mod_matrix_poly_plan_t *const plan = &g_mod_matrix_poly_plan[track];
    if (plan->route_count == 0U) return;
    float sums[MOD_MATRIX_SLOT_COUNT] = {0.0f};

    for (uint8_t i = 0U; i < plan->route_count; ++i)
    {
        const mod_matrix_poly_route_t *const route = &plan->routes[i];
        if (source_valid[route->source] != 0U)
            sums[route->destination_index] += source_end[route->source] * route->scale;
    }

    for (uint8_t i = 0U; i < plan->destination_count; ++i)
    {
        const mod_matrix_poly_destination_t *const planned = &plan->destinations[i];
        const uint8_t runtime_index = planned->runtime_destination_index;
        const mod_matrix_runtime_destination_t *const destination =
            &g_mod_matrix_runtime[track].destinations[runtime_index];
        const float value = mod_matrix_clampf(destination->base_value + sums[i],
                                              destination->min_value,
                                              destination->max_value);
        (void)mod_destination_catalog_apply_poly_prepared(
            &destination->prepared, voice_slot, value);
    }
}

void mod_matrix_reset_poly_voice(uint8_t track,
                                 uint8_t voice_slot,
                                 const track_audio_runtime_ctx_t *ctx)
{
    (void)ctx;
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    const mod_matrix_poly_plan_t *const plan = &g_mod_matrix_poly_plan[track];
    for (uint8_t i = 0U; i < plan->destination_count; ++i)
    {
        const mod_matrix_poly_destination_t *const destination = &plan->destinations[i];
        const uint8_t runtime_index = destination->runtime_destination_index;
        const mod_matrix_runtime_destination_t *const runtime_destination =
            &g_mod_matrix_runtime[track].destinations[runtime_index];
        (void)mod_destination_catalog_apply_poly_prepared(
            &runtime_destination->prepared, voice_slot,
            runtime_destination->base_value);
    }
}

uint8_t mod_matrix_get_destination_ramp(uint8_t track,
                                        param_id_t destination,
                                        mod_destination_ramp_t *out_ramp)
{
    if ((track >= SEQ_TRACK_COUNT) || (destination >= PARAM_COUNT) || (out_ramp == NULL))
    {
        return 0U;
    }

    uint8_t owner = 0U;
    if (mod_matrix_audio_resolve_owner(track, &owner) == 0U)
    {
        return 0U;
    }
    const mod_matrix_runtime_destination_t *const dst =
        mod_matrix_find_runtime_destination(&g_mod_matrix_runtime[owner],
            mod_destination_address_make(track, destination));
    if ((dst == NULL) || (dst->valid == 0U))
    {
        return 0U;
    }

    *out_ramp = dst->ramp;
    return 1U;
}

void audio_mod_matrix_base_update(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    audio_mod_destination_catalog_invalidate_runtime_value(track, id);
    uint8_t owner = 0U;
    if (mod_matrix_audio_resolve_owner(track, &owner) == 0U) return;
    const mod_destination_address_t address = mod_destination_address_make(track, id);
    mod_matrix_runtime_destination_t *const dst =
        mod_matrix_find_runtime_destination(&g_mod_matrix_runtime[owner], address);
    if (dst != NULL)
    {
        dst->base_value = value;
    }
}
