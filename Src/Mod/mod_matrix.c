#include "Mod/mod_matrix.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/control_audio_queue.h"
#include "Storage/memory_layout.h"

#include <string.h>

#include "Audio/mixer.h"
#include "Core/track_sound_state.h"
#include "Core/entity_topology.h"
#include "Core/live_clock.h"
#include "Mod/mod_destination_catalog.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_lfo_v1.h"
#include "Param/param_registry.h"
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
} mod_matrix_runtime_destination_t;

typedef struct
{
    mod_matrix_runtime_destination_t destinations[MOD_MATRIX_SLOT_COUNT];
} mod_matrix_runtime_track_t;

typedef struct
{
    uint8_t any_route;
    uint16_t source_mask;
} mod_matrix_route_cache_t;

typedef struct
{
    uint8_t source;
    uint8_t destination_index;
    float scale;
} mod_matrix_track_route_t;

typedef struct
{
    uint16_t destination;
    uint8_t runtime_destination_index;
    float min_value;
    float max_value;
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
    uint8_t valid;
    uint16_t destination;
    float value;
} mod_matrix_base_override_t;

static mod_matrix_runtime_track_t g_mod_matrix_runtime[SEQ_TRACK_COUNT];
static mod_matrix_route_cache_t g_mod_matrix_route_cache[SEQ_TRACK_COUNT];
typedef struct
{
    uint8_t source;
    uint8_t destination_index;
    float scale;
} mod_matrix_poly_route_t;

typedef struct
{
    uint16_t destination;
    float base_value;
    float min_value;
    float max_value;
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
static modulation_publication_t g_mod_matrix_audio_publication[SEQ_TRACK_COUNT];
CONTROL_STATE_SDRAM static modulation_publication_t
    g_mod_matrix_control_publication[SEQ_TRACK_COUNT][MODULATION_PUBLICATION_SLOT_COUNT];
CONTROL_STATE_SDRAM static uint32_t g_mod_matrix_control_generation[SEQ_TRACK_COUNT];
static mod_matrix_base_override_t
    g_mod_matrix_base_overrides[SEQ_TRACK_COUNT][MOD_MATRIX_SLOT_COUNT];
static uint8_t g_mod_matrix_any_route = 0U;

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
    entity_topology_descriptor_t topology;
    const uint8_t is_group_master = (uint8_t)(
        (entity_topology_get(track, &topology) != 0U)
        && (topology.role == ENTITY_ROLE_GROUP_MASTER));
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
                    || (ctx->audio_binding.mix_track_id >= MIXER_MAX_TRACKS)
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
            if (track_runtime_get_effective_param_status(track, PARAM_FILTER_ATTACK) != TRACK_RUNTIME_PARAM_ALLOWED)
            {
                return 0U;
            }
            break;

        default:
            return 0U;
    }

    const track_audio_runtime_ctx_t *const target_ctx =
        audio_note_engine_adapter_audio_ctx(target);
    return mod_destination_catalog_supported_fast(
        target, destination,
        mod_matrix_ui_family_from_ctx(target_ctx),
        mod_matrix_ui_type_from_ctx(target_ctx), target_ctx);
}

static void mod_matrix_release_destination(uint8_t track,
                                           mod_matrix_runtime_destination_t *dst,
                                           ui_track_family_t family,
                                           ui_track_type_t type,
                                           const track_audio_runtime_ctx_t *ctx)
{
    (void)track;
    (void)family;
    (void)type;
    (void)ctx;
    if ((dst == NULL) || (dst->valid == 0U))
    {
        return;
    }

    uint8_t target = 0U;
    param_id_t destination = PARAM_COUNT;
    const track_audio_runtime_ctx_t *target_ctx = NULL;
    if ((mod_destination_address_resolve(dst->destination,
                                         &target, &destination) != 0U)
            && ((target_ctx = audio_note_engine_adapter_audio_ctx(target)) != NULL))
    {
        (void)mod_destination_catalog_apply_rt(target, destination,
                                               target_ctx, dst->base_value);
    }

    dst->valid = 0U;
    dst->modulation_active = 0U;
    dst->destination = MOD_DESTINATION_NONE;
    dst->base_value = 0.0f;
    dst->sum = 0.0f;
    dst->sum_end = 0.0f;
    dst->min_value = 0.0f;
    dst->max_value = 127.0f;
    dst->ramp = (mod_destination_ramp_t){0};
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

static mod_matrix_base_override_t *mod_matrix_find_base_override(uint8_t track,
                                                                 param_id_t destination)
{
    if ((track >= SEQ_TRACK_COUNT) || (destination >= PARAM_COUNT))
    {
        return NULL;
    }
    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        mod_matrix_base_override_t *const entry =
            &g_mod_matrix_base_overrides[track][i];
        if ((entry->valid != 0U)
                && (entry->destination == (uint16_t)destination))
        {
            return entry;
        }
    }
    return NULL;
}

static mod_matrix_base_override_t *mod_matrix_alloc_base_override(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }
    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        if (g_mod_matrix_base_overrides[track][i].valid == 0U)
        {
            return &g_mod_matrix_base_overrides[track][i];
        }
    }
    return NULL;
}

static void mod_matrix_restore_destination_value(uint8_t track,
                                                 mod_matrix_runtime_destination_t *dst,
                                                 ui_track_family_t family,
                                                 ui_track_type_t type,
                                                 const track_audio_runtime_ctx_t *ctx)
{
    (void)track;
    (void)family;
    (void)type;
    (void)ctx;
    if ((dst == NULL) || (dst->valid == 0U))
    {
        return;
    }

    uint8_t target = 0U;
    param_id_t destination = PARAM_COUNT;
    const track_audio_runtime_ctx_t *target_ctx = NULL;
    if ((mod_destination_address_resolve(dst->destination,
                                         &target, &destination) != 0U)
            && ((target_ctx = audio_note_engine_adapter_audio_ctx(target)) != NULL))
    {
        (void)mod_destination_catalog_apply_rt(target,
                                               destination,
                                               target_ctx,
                                               dst->base_value);
    }
}

#if 0
static void mod_matrix_rebuild_track_plan(uint8_t track,
                                          ui_track_family_t family,
                                          ui_track_type_t type,
                                          const track_audio_runtime_ctx_t *ctx)
{
    typedef struct
    {
        uint8_t source;
        uint16_t destination;
        float depth;
    } mod_matrix_track_candidate_t;

    mod_matrix_track_candidate_t candidates[MOD_MATRIX_SLOT_COUNT];
    mod_matrix_track_plan_t *const plan = &g_mod_matrix_track_plan[track];
    mod_matrix_runtime_track_t *const rt = &g_mod_matrix_runtime[track];
    uint8_t candidate_count = 0U;

    memset(plan, 0, sizeof(*plan));

    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        const track_mod_matrix_slot_t *const s = mod_matrix_audio_slot_const(track, slot);
        if ((mod_matrix_slot_is_effective(track, s, family, type, ctx) != 0U)
                && (candidate_count < MOD_MATRIX_SLOT_COUNT))
        {
            candidates[candidate_count].source = s->source;
            candidates[candidate_count].destination = s->destination;
            candidates[candidate_count].depth = s->depth;
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
            mod_matrix_release_destination(track, dst, family, type, ctx);
        }
    }

    for (uint8_t candidate = 0U; candidate < candidate_count; ++candidate)
    {
        uint8_t destination_index = plan->destination_count;
        for (uint8_t i = 0U; i < plan->destination_count; ++i)
        {
            if (plan->destinations[i].destination == candidates[candidate].destination)
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
            plan->destinations[destination_index].destination = candidates[candidate].destination;
            plan->destinations[destination_index].runtime_destination_index = runtime_index;
            plan->destinations[destination_index].min_value = dst->min_value;
            plan->destinations[destination_index].max_value = dst->max_value;
            ++plan->destination_count;
        }

        if (plan->route_count >= MOD_MATRIX_SLOT_COUNT)
        {
            continue;
        }

        mod_matrix_track_route_t *const route = &plan->routes[plan->route_count++];
        route->source = candidates[candidate].source;
        route->destination_index = destination_index;
        route->scale = (candidates[candidate].depth / 127.0f)
            * (plan->destinations[destination_index].max_value
               - plan->destinations[destination_index].min_value);
        plan->destinations[destination_index].discontinuity_source_mask |=
            (uint16_t)(1U << candidates[candidate].source);
    }
}
#endif

void mod_matrix_publish_control_snapshot(uint8_t track)
{
    brick_entity_id_t owner = track;
    if ((track >= SEQ_TRACK_COUNT)
            || (entity_topology_mod_owner(track, &owner) == 0U)
            || (owner >= SEQ_TRACK_COUNT))
    {
        return;
    }

    const track_sound_state_t *const state = track_sound_state_get_const(owner);
    if (state == NULL)
    {
        return;
    }

    uint32_t generation = g_mod_matrix_control_generation[owner] + 1U;
    if (generation == 0U)
    {
        generation = 1U;
    }
    g_mod_matrix_control_generation[owner] = generation;
    modulation_publication_t *const publication =
        &g_mod_matrix_control_publication[owner]
            [generation % MODULATION_PUBLICATION_SLOT_COUNT];
    memset(publication, 0, sizeof(*publication));
    publication->generation = generation;

    for (uint8_t lfo = 0U; lfo < MODULATION_PUBLICATION_LFO_COUNT; ++lfo)
    {
        publication->lfo[lfo].rate = state->mod_lfo[lfo].rate;
        publication->lfo[lfo].phase = state->mod_lfo[lfo].phase;
        publication->lfo[lfo].shape = (uint8_t)(state->mod_lfo[lfo].shape + 0.5f);
        publication->lfo[lfo].trig = (uint8_t)(state->mod_lfo[lfo].trig + 0.5f);
    }
    publication->env3.attack = state->mod_env3.attack;
    publication->env3.decay = state->mod_env3.decay;
    publication->env3.sustain = state->mod_env3.sustain;
    publication->env3.release = state->mod_env3.release;
    publication->env3.retrigger_hard = (state->env_retrig_mod >= 0.5f) ? 1U : 0U;
    publication->multi_source[0][0] = state->mod_multi[0].source_a;
    publication->multi_source[0][1] = state->mod_multi[0].source_b;
    publication->multi_source[1][0] = state->mod_multi[1].source_a;
    publication->multi_source[1][1] = state->mod_multi[1].source_b;
    publication->slew_source[0] = state->mod_slew[0].source;
    publication->slew_source[1] = state->mod_slew[1].source;
    publication->slew_amount[0] = state->mod_slew[0].amount;
    publication->slew_amount[1] = state->mod_slew[1].amount;

    const track_audio_runtime_ctx_t *const ctx =
        audio_note_engine_adapter_audio_ctx(owner);
    const ui_track_family_t family = mod_matrix_ui_family_from_ctx(ctx);
    const ui_track_type_t type = mod_matrix_ui_type_from_ctx(ctx);
    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        const track_mod_matrix_slot_t *const source = &state->mod_matrix[slot];
        if ((mod_matrix_slot_is_effective(owner, source, family, type, ctx) == 0U)
                || (publication->route_count >= MODULATION_PUBLICATION_ROUTE_COUNT))
        {
            continue;
        }

        uint8_t target = 0U;
        param_id_t destination = PARAM_COUNT;
        if (mod_destination_address_resolve(source->destination,
                                            &target, &destination) == 0U)
        {
            continue;
        }

        uint8_t destination_index = publication->destination_count;
        for (uint8_t i = 0U; i < publication->destination_count; ++i)
        {
            if (publication->destinations[i].destination == source->destination)
            {
                destination_index = i;
                break;
            }
        }
        if (destination_index == publication->destination_count)
        {
            if (publication->destination_count >= MODULATION_PUBLICATION_ROUTE_COUNT)
            {
                continue;
            }
            float base = param_registry[destination].default_value;
            (void)param_registry_get_track_value(destination, target, &base);
            publication->destinations[destination_index].destination = source->destination;
            publication->destinations[destination_index].base_value = base;
            publication->destinations[destination_index].min_value = param_registry[destination].min;
            publication->destinations[destination_index].max_value = param_registry[destination].max;
            publication->destination_count++;
        }

        modulation_route_publication_t *const route =
            &publication->routes[publication->route_count++];
        route->source = source->source;
        route->destination_index = destination_index;
        route->scale = (source->depth / 127.0f)
            * (publication->destinations[destination_index].max_value
               - publication->destinations[destination_index].min_value);
        publication->source_mask |= (uint16_t)(1U << source->source);

        uint8_t poly = 0U;
        if ((source->source >= (uint8_t)MOD_MATRIX_SOURCE_LFO1)
                && (source->source <= (uint8_t)MOD_MATRIX_SOURCE_LFO3)
                && (target == owner)
                && (state->mod_lfo[source->source - (uint8_t)MOD_MATRIX_SOURCE_LFO1].trig
                    >= (float)MOD_LFO_TRIG_POLY_TRIG)
                && (mod_destination_catalog_poly_voice_supported(destination, ctx) != 0U))
        {
            poly = 1U;
            publication->poly_source_mask |= (uint8_t)(
                1U << (source->source - (uint8_t)MOD_MATRIX_SOURCE_LFO1));
        }
        route->flags = poly;
    }

    const uint8_t buffer = (uint8_t)(generation % MODULATION_PUBLICATION_SLOT_COUNT);
    uint64_t due_sample = 0U;
    if (!live_clock_read_audio_sample(&due_sample))
    {
        return;
    }
    const control_audio_event_t event = {
        .due_sample = due_sample,
        .source_generation = publication->generation,
        .entity_id = owner,
        .kind = (uint8_t)CONTROL_AUDIO_EVENT_MODULATION_SNAPSHOT,
        .note = buffer
    };
    (void)control_audio_queue_publish(&event);
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

void mod_matrix_init(void)
{
    mod_matrix_reset_runtime();
}

void mod_matrix_reset_runtime(void)
{
    memset(g_mod_matrix_runtime, 0, sizeof(g_mod_matrix_runtime));
    memset(g_mod_matrix_track_plan, 0, sizeof(g_mod_matrix_track_plan));
    memset(g_mod_matrix_operator_runtime, 0, sizeof(g_mod_matrix_operator_runtime));
    memset(g_mod_matrix_audio_publication, 0, sizeof(g_mod_matrix_audio_publication));
    memset(g_mod_matrix_control_publication, 0, sizeof(g_mod_matrix_control_publication));
    memset(g_mod_matrix_control_generation, 0, sizeof(g_mod_matrix_control_generation));
    memset(g_mod_matrix_base_overrides, 0, sizeof(g_mod_matrix_base_overrides));
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
        {
            g_mod_matrix_runtime[track].destinations[i].destination = (uint16_t)MOD_DESTINATION_NONE;
            g_mod_matrix_runtime[track].destinations[i].max_value = 127.0f;
        }
    }
}

#if 0
static void mod_matrix_audio_rebuild_route_cache_track(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    uint16_t source_mask = 0U;
    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        (void)slot;
    }

    g_mod_matrix_route_cache[track].source_mask = source_mask;
    g_mod_matrix_route_cache[track].any_route = (source_mask != 0U) ? 1U : 0U;

    mod_matrix_poly_plan_t *const plan = &g_mod_matrix_poly_plan[track];
    memset(plan, 0, sizeof(*plan));
    const track_audio_runtime_ctx_t *const ctx = audio_note_engine_adapter_audio_ctx(track);
    const ui_track_family_t family = mod_matrix_ui_family_from_ctx(ctx);
    const ui_track_type_t type = mod_matrix_ui_type_from_ctx(ctx);
    mod_matrix_rebuild_track_plan(track, family, type, ctx);
    if (ctx != NULL)
    {
        for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
        {
            const track_mod_matrix_slot_t *const s = mod_matrix_audio_slot_const(track, slot);
            uint8_t target = 0U;
            param_id_t destination = PARAM_COUNT;
            if ((mod_matrix_slot_is_configured(s) == 0U)
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
                if (plan->destinations[i].destination == s->destination)
                {
                    dst_index = i;
                    break;
                }
            }
            if (dst_index == plan->destination_count)
            {
                const param_desc_t *const desc = &param_registry[destination];
                plan->destinations[dst_index].destination = s->destination;
                const mod_matrix_base_override_t *const override =
                    mod_matrix_find_base_override(track, destination);
                if (override != NULL)
                    plan->destinations[dst_index].base_value = override->value;
                else
                    (void)param_registry_get_track_value(destination, track,
                        &plan->destinations[dst_index].base_value);
                plan->destinations[dst_index].min_value = desc->min;
                plan->destinations[dst_index].max_value = desc->max;
                plan->destination_count++;
            }
            mod_matrix_poly_route_t *const route = &plan->routes[plan->route_count++];
            route->source = s->source;
            route->destination_index = dst_index;
            route->scale = (s->depth / 127.0f)
                * (plan->destinations[dst_index].max_value
                    - plan->destinations[dst_index].min_value);
            plan->source_mask |= (uint8_t)(1U << lfo);
        }
    }
    mixer_invalidate_external_poly_track(track);
    mod_matrix_recompute_global_route_flag();
}
#endif

static void mod_matrix_audio_rebuild_route_cache_track(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_mod_matrix_route_cache[track].source_mask =
        g_mod_matrix_audio_publication[track].source_mask;
    g_mod_matrix_route_cache[track].any_route =
        (g_mod_matrix_audio_publication[track].route_count != 0U) ? 1U : 0U;
    mod_matrix_recompute_global_route_flag();
}

void mod_matrix_rebuild_route_cache_track(uint8_t track)
{
    brick_entity_id_t owner = track;
    if ((entity_topology_mod_owner(track, &owner) == 0U)
            || (owner >= SEQ_TRACK_COUNT))
        return;
    track = owner;

    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        (void)mod_matrix_track_slot_mut(track, slot);
    }
    mod_matrix_publish_control_snapshot(track);
}

void mod_matrix_audio_apply_publication(uint8_t track,
                                        const modulation_publication_t *publication)
{
    if ((track >= SEQ_TRACK_COUNT) || (publication == NULL)
            || (publication->generation == 0U))
    {
        return;
    }

    g_mod_matrix_audio_publication[track] = *publication;
    mod_matrix_runtime_track_t *const runtime = &g_mod_matrix_runtime[track];
    const track_audio_runtime_ctx_t *const ctx =
        audio_note_engine_adapter_audio_ctx(track);
    mod_matrix_release_track(track,
                             mod_matrix_ui_family_from_ctx(ctx),
                             mod_matrix_ui_type_from_ctx(ctx), ctx);

    mod_matrix_track_plan_t *const plan = &g_mod_matrix_track_plan[track];
    memset(plan, 0, sizeof(*plan));
    for (uint8_t i = 0U; i < publication->destination_count; ++i)
    {
        plan->destinations[i].destination = publication->destinations[i].destination;
        plan->destinations[i].runtime_destination_index = i;
        plan->destinations[i].min_value = publication->destinations[i].min_value;
        plan->destinations[i].max_value = publication->destinations[i].max_value;
        ++plan->destination_count;
        runtime->destinations[i].valid = 1U;
        runtime->destinations[i].destination = publication->destinations[i].destination;
        runtime->destinations[i].base_value = publication->destinations[i].base_value;
        runtime->destinations[i].min_value = publication->destinations[i].min_value;
        runtime->destinations[i].max_value = publication->destinations[i].max_value;
    }
    for (uint8_t i = 0U; i < publication->route_count; ++i)
    {
        plan->routes[i].source = publication->routes[i].source;
        plan->routes[i].destination_index = publication->routes[i].destination_index;
        plan->routes[i].scale = publication->routes[i].scale;
        ++plan->route_count;
        if (publication->routes[i].destination_index < MOD_MATRIX_SLOT_COUNT)
        {
            plan->destinations[publication->routes[i].destination_index]
                .discontinuity_source_mask |= (uint16_t)(
                    1U << publication->routes[i].source);
        }
    }

    memset(&g_mod_matrix_poly_plan[track], 0, sizeof(g_mod_matrix_poly_plan[track]));
    mod_matrix_poly_plan_t *const poly = &g_mod_matrix_poly_plan[track];
    uint8_t poly_destination_map[MOD_MATRIX_SLOT_COUNT];
    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        poly_destination_map[i] = UINT8_MAX;
    }
    for (uint8_t i = 0U; i < publication->route_count; ++i)
    {
        const modulation_route_publication_t *const source = &publication->routes[i];
        if ((source->flags & 1U) == 0U)
        {
            continue;
        }
        if (source->destination_index >= publication->destination_count)
        {
            continue;
        }
        uint8_t poly_destination = poly_destination_map[source->destination_index];
        if (poly_destination == UINT8_MAX)
        {
            if (poly->destination_count >= MOD_MATRIX_SLOT_COUNT)
            {
                continue;
            }
            poly_destination = poly->destination_count++;
            poly_destination_map[source->destination_index] = poly_destination;
            poly->destinations[poly_destination] = (mod_matrix_poly_destination_t){
                .destination = publication->destinations[source->destination_index].destination,
                .base_value = publication->destinations[source->destination_index].base_value,
                .min_value = publication->destinations[source->destination_index].min_value,
                .max_value = publication->destinations[source->destination_index].max_value
            };
        }
        if (poly->route_count >= MOD_MATRIX_SLOT_COUNT)
        {
            break;
        }
        poly->routes[poly->route_count++] = (mod_matrix_poly_route_t){
            .source = source->source,
            .destination_index = poly_destination,
            .scale = source->scale
        };
        poly->source_mask |= (uint8_t)(
            1U << (source->source - (uint8_t)MOD_MATRIX_SOURCE_LFO1));
    }
    for (uint8_t lfo = 0U; lfo < MODULATION_PUBLICATION_LFO_COUNT; ++lfo)
    {
        mod_lfo_v1_audio_apply_config(track, lfo, &publication->lfo[lfo]);
    }
    mod_env3_audio_apply_config(track, &publication->env3);
    g_mod_matrix_route_cache[track].source_mask = publication->source_mask;
    g_mod_matrix_route_cache[track].any_route =
        (publication->route_count != 0U) ? 1U : 0U;
    mod_matrix_recompute_global_route_flag();
}

void audio_mod_matrix_apply_snapshot(const control_audio_event_t *event)
{
    if ((event == NULL) || (event->entity_id >= SEQ_TRACK_COUNT)
            || (event->note >= MODULATION_PUBLICATION_SLOT_COUNT))
        return;

    const modulation_publication_t *const publication =
        &g_mod_matrix_control_publication[event->entity_id][event->note];
    if (publication->generation != event->source_generation)
    {
        return;
    }
    mod_matrix_audio_apply_publication(event->entity_id, publication);
}

void audio_mod_matrix_rebuild_track(uint8_t track)
{
    mod_matrix_audio_rebuild_route_cache_track(track);
}

void mod_matrix_rebuild_route_cache_all(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        brick_entity_id_t owner = track;
        if ((entity_topology_mod_owner(track, &owner) != 0U) && (owner == track))
            mod_matrix_rebuild_route_cache_track(track);
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
    s->destination = mod_destination_catalog_address_from_index(owner, index);
    s->enabled = ((s->destination != MOD_DESTINATION_NONE)
                  && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)) ? 1U : 0U;
    mod_matrix_rebuild_route_cache_track(track);
    return 1U;
}

uint8_t mod_matrix_set_slot_depth(uint8_t track, uint8_t slot, float value)
{
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL)
    {
        return 0U;
    }

    s->depth = mod_matrix_clampf(value, -127.0f, 127.0f);
    mod_matrix_rebuild_route_cache_track(track);
    return 1U;
}

uint8_t mod_matrix_set_slot_source(uint8_t track, uint8_t slot, float value)
{
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL)
    {
        return 0U;
    }

    s->source = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
    s->enabled = ((s->destination != (uint16_t)MOD_DESTINATION_NONE)
                  && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)) ? 1U : 0U;
    mod_matrix_rebuild_route_cache_track(track);
    return 1U;
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
    state->source = source;
    state->destination = destination;
    state->depth = mod_matrix_clampf(depth, -127.0f, 127.0f);
    state->enabled = ((enabled != 0U)
            && (source != (uint8_t)MOD_MATRIX_SOURCE_NONE)
            && (destination != MOD_DESTINATION_NONE)) ? 1U : 0U;
    mod_matrix_rebuild_route_cache_track(owner);
    return 1U;
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
    track_sound_state_t *const state = mod_matrix_control_state(track);
    if ((state == NULL) || (op >= 2U) || (input >= 2U))
    {
        return 0U;
    }

    const uint8_t source = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
    if (input == 0U)
    {
        state->mod_multi[op].source_a = source;
    }
    else
    {
        state->mod_multi[op].source_b = source;
    }
    mod_matrix_publish_control_snapshot(track);
    return 1U;
}

uint8_t mod_matrix_get_multi_source(uint8_t track, uint8_t op, uint8_t input, float *out_value)
{
    const track_sound_state_t *const state = mod_matrix_control_state_const(track);
    if ((state == NULL) || (op >= 2U) || (input >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = (float)((input == 0U) ? state->mod_multi[op].source_a : state->mod_multi[op].source_b);
    return 1U;
}

static void mod_matrix_reset_slew_runtime(uint8_t track, uint8_t op)
{
    if ((track >= SEQ_TRACK_COUNT) || (op >= 2U))
    {
        return;
    }

    g_mod_matrix_operator_runtime[track].slew[op] = 0.0f;
    g_mod_matrix_operator_runtime[track].slew_valid[op] = 0U;
}

uint8_t mod_matrix_set_slew_source(uint8_t track, uint8_t op, float value)
{
    track_sound_state_t *const state = mod_matrix_control_state(track);
    if ((state == NULL) || (op >= 2U))
    {
        return 0U;
    }

    const uint8_t source = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
    if (state->mod_slew[op].source != source)
    {
        state->mod_slew[op].source = source;
        mod_matrix_reset_slew_runtime(track, op);
    }
    mod_matrix_publish_control_snapshot(track);
    return 1U;
}

uint8_t mod_matrix_get_slew_source(uint8_t track, uint8_t op, float *out_value)
{
    const track_sound_state_t *const state = mod_matrix_control_state_const(track);
    if ((state == NULL) || (op >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = (float)state->mod_slew[op].source;
    return 1U;
}

uint8_t mod_matrix_set_slew_amount(uint8_t track, uint8_t op, float value)
{
    track_sound_state_t *const state = mod_matrix_control_state(track);
    if ((state == NULL) || (op >= 2U))
    {
        return 0U;
    }

    const float amount = mod_matrix_clampf(value, 0.0f, 1.0f);
    if (state->mod_slew[op].amount != amount)
    {
        state->mod_slew[op].amount = amount;
    }
    mod_matrix_publish_control_snapshot(track);
    return 1U;
}

uint8_t mod_matrix_get_slew_amount(uint8_t track, uint8_t op, float *out_value)
{
    const track_sound_state_t *const state = mod_matrix_control_state_const(track);
    if ((state == NULL) || (op >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = state->mod_slew[op].amount;
    return 1U;
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

uint8_t mod_matrix_track_has_configured_source(uint8_t track, mod_matrix_source_t source)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (source == MOD_MATRIX_SOURCE_NONE)
            || ((uint8_t)source >= MOD_MATRIX_SOURCE_COUNT))
    {
        return 0U;
    }

    return ((g_mod_matrix_route_cache[track].source_mask & (uint16_t)(1U << (uint8_t)source)) != 0U) ? 1U : 0U;
}

static uint8_t mod_matrix_slew_direct_cycle(const modulation_publication_t *state, uint8_t op);

static uint8_t mod_matrix_source_depends_on(const modulation_publication_t *state,
                                            uint8_t root_source,
                                            uint8_t target_source,
                                            uint8_t depth)
{
    if ((state == NULL)
            || (root_source >= (uint8_t)MOD_MATRIX_SOURCE_COUNT)
            || (target_source >= (uint8_t)MOD_MATRIX_SOURCE_COUNT)
            || (depth >= 4U))
    {
        return 0U;
    }

    if (root_source == target_source)
    {
        return 1U;
    }

    switch ((mod_matrix_source_t)root_source)
    {
        case MOD_MATRIX_SOURCE_MULTI1:
        case MOD_MATRIX_SOURCE_MULTI2:
        {
            const uint8_t op = (root_source == (uint8_t)MOD_MATRIX_SOURCE_MULTI1) ? 0U : 1U;
            const uint8_t src_a = state->multi_source[op][0];
            const uint8_t src_b = state->multi_source[op][1];
            if ((src_a == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                    || (src_a == (uint8_t)MOD_MATRIX_SOURCE_MULTI2)
                    || (src_b == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                    || (src_b == (uint8_t)MOD_MATRIX_SOURCE_MULTI2))
            {
                return 0U;
            }
            return ((mod_matrix_source_depends_on(state, src_a, target_source, (uint8_t)(depth + 1U)) != 0U)
                    || (mod_matrix_source_depends_on(state, src_b, target_source, (uint8_t)(depth + 1U)) != 0U))
                ? 1U
                : 0U;
        }
        case MOD_MATRIX_SOURCE_SLEW1:
        case MOD_MATRIX_SOURCE_SLEW2:
        {
            const uint8_t op = (root_source == (uint8_t)MOD_MATRIX_SOURCE_SLEW1) ? 0U : 1U;
            if (mod_matrix_slew_direct_cycle(state, op) != 0U)
            {
                return 0U;
            }
            return mod_matrix_source_depends_on(state,
                                                state->slew_source[op],
                                                target_source,
                                                (uint8_t)(depth + 1U));
        }
        default:
            return 0U;
    }
}

static uint8_t mod_matrix_track_has_operator_route(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    const uint16_t mask = g_mod_matrix_route_cache[track].source_mask;
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

static uint8_t mod_matrix_slew_direct_cycle(const modulation_publication_t *state, uint8_t op)
{
    if ((state == NULL) || (op >= 2U))
    {
        return 1U;
    }

    const uint8_t src = state->slew_source[op];
    const uint8_t self = (op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1 : (uint8_t)MOD_MATRIX_SOURCE_SLEW2;
    const uint8_t other = (op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW2 : (uint8_t)MOD_MATRIX_SOURCE_SLEW1;
    if (src == self)
    {
        return 1U;
    }
    if (src == other)
    {
        const uint8_t other_src = state->slew_source[1U - op];
        if (other_src == self)
        {
            return 1U;
        }
    }
    return 0U;
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

    const modulation_publication_t *const state = &g_mod_matrix_audio_publication[track];
    mod_matrix_operator_runtime_t *const rt = &g_mod_matrix_operator_runtime[track];
    if (state == NULL)
    {
        return;
    }

    for (uint8_t op = 0U; op < 2U; ++op)
    {
        const uint8_t src_a = state->multi_source[op][0];
        const uint8_t src_b = state->multi_source[op][1];
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
            const uint8_t output = (op == 0U)
                ? (uint8_t)MOD_MATRIX_SOURCE_MULTI1
                : (uint8_t)MOD_MATRIX_SOURCE_MULTI2;
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
        const uint8_t src = state->slew_source[op];
        float input_start = 0.0f;
        float input_end = 0.0f;
        if ((mod_matrix_slew_direct_cycle(state, op) == 0U)
                && (mod_matrix_get_operator_source_value(rt, source_start, source_valid, src, &input_start) != 0U)
                && (mod_matrix_get_operator_source_value(rt, source_end, source_valid, src, &input_end) != 0U))
        {
            const float amount = mod_matrix_clampf(state->slew_amount[op], 0.0f, 1.0f);
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
            const uint8_t output = (op == 0U)
                ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1
                : (uint8_t)MOD_MATRIX_SOURCE_SLEW2;
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

uint8_t mod_matrix_source_has_active_route(uint8_t track,
                                           mod_matrix_source_t source,
                                           ui_track_family_t family,
                                           ui_track_type_t type,
                                           const track_audio_runtime_ctx_t *ctx)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (source == MOD_MATRIX_SOURCE_NONE)
            || ((uint8_t)source >= MOD_MATRIX_SOURCE_COUNT))
    {
        return 0U;
    }

    (void)family;
    (void)type;
    (void)ctx;
    for (uint8_t i = 0U; i < g_mod_matrix_audio_publication[track].route_count; ++i)
    {
        const uint8_t route_source = g_mod_matrix_audio_publication[track].routes[i].source;
        if ((route_source == (uint8_t)source)
                || (mod_matrix_source_depends_on(
                    &g_mod_matrix_audio_publication[track],
                    route_source, (uint8_t)source, 0U) != 0U))
        {
            return 1U;
        }
    }
    return 0U;
}

void mod_matrix_process_track_ramped(uint8_t track,
                                     const track_audio_runtime_ctx_t *ctx,
                                     const float source_start[MOD_MATRIX_SOURCE_COUNT],
                                     const float source_end[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT],
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
    const mod_matrix_track_plan_t *const plan = &g_mod_matrix_track_plan[track];
    if (plan->route_count == 0U)
    {
        return;
    }

    mod_matrix_runtime_track_t *const rt = &g_mod_matrix_runtime[track];
    const ui_track_family_t family = mod_matrix_ui_family_from_ctx(ctx);
    const ui_track_type_t type = mod_matrix_ui_type_from_ctx(ctx);
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
                mod_matrix_restore_destination_value(track,
                                                     dst,
                                                     family,
                                                     type,
                                                     ctx);
                dst->modulation_active = 0U;
            }
            dst->sum = 0.0f;
            dst->sum_end = 0.0f;
            dst->ramp = (mod_destination_ramp_t){0};
            continue;
        }

        const float start = mod_matrix_clampf(dst->base_value + dst->sum,
                                              planned->min_value,
                                              planned->max_value);
        const float end = mod_matrix_clampf(dst->base_value + dst->sum_end,
                                            planned->min_value,
                                            planned->max_value);
        const uint8_t discontinuous =
            ((planned->discontinuity_source_mask & discontinuity_source_mask) != 0U) ? 1U : 0U;
        mod_destination_ramp_prepare(start,
                                     end,
                                     elapsed_frames,
                                     discontinuous,
                                     &dst->ramp);
        uint8_t target = 0U;
        param_id_t destination = PARAM_COUNT;
        if (mod_destination_address_resolve(dst->destination,
                                            &target, &destination) != 0U)
        {
            const track_audio_runtime_ctx_t *const target_ctx =
                audio_note_engine_adapter_audio_ctx(target);
            (void)mod_destination_catalog_apply_ramp_rt(target,
                                                        destination,
                                                        target_ctx,
                                                        &dst->ramp);
        }
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
    if ((track >= SEQ_TRACK_COUNT) || (ctx == NULL) || (source_start == NULL)
            || (source_end == NULL) || (source_valid == NULL)
            || (mod_matrix_track_has_configured_route(track) == 0U))
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
        const float value = mod_matrix_clampf(planned->base_value + sums[i],
                                              planned->min_value, planned->max_value);
        uint8_t target = 0U;
        param_id_t destination = PARAM_COUNT;
        if ((mod_destination_address_resolve(planned->destination,
                                             &target, &destination) != 0U)
                && (target == track))
            (void)mod_destination_catalog_apply_poly_voice_rt(track, voice_slot,
                                                              destination,
                                                              ctx, value);
    }
}

void mod_matrix_reset_poly_voice(uint8_t track,
                                 uint8_t voice_slot,
                                 const track_audio_runtime_ctx_t *ctx)
{
    if ((track >= SEQ_TRACK_COUNT) || (ctx == NULL)
            || (mod_matrix_track_has_configured_route(track) == 0U))
    {
        return;
    }

    const mod_matrix_poly_plan_t *const plan = &g_mod_matrix_poly_plan[track];
    for (uint8_t i = 0U; i < plan->destination_count; ++i)
    {
        const mod_matrix_poly_destination_t *const destination = &plan->destinations[i];
        uint8_t target = 0U;
        param_id_t param = PARAM_COUNT;
        if ((mod_destination_address_resolve(destination->destination,
                                             &target, &param) != 0U)
                && (target == track))
            (void)mod_destination_catalog_apply_poly_voice_rt(track,
                                                              voice_slot,
                                                              param,
                                                              ctx,
                                                              destination->base_value);
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

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U)
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

void mod_matrix_release_track(uint8_t track,
                              ui_track_family_t family,
                              ui_track_type_t type,
                              const track_audio_runtime_ctx_t *ctx)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        mod_matrix_release_destination(track, &g_mod_matrix_runtime[track].destinations[i], family, type, ctx);
    }
    g_mod_matrix_track_plan[track].route_count = 0U;
    g_mod_matrix_track_plan[track].destination_count = 0U;
}

void mod_matrix_resync_base_on_authoritative_write(uint8_t track, param_id_t id, float value)
{
    (void)value;
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return;
    if (mod_matrix_find_base_override(track, id) == NULL)
    {
        mod_matrix_publish_control_snapshot(owner);
    }
}

void mod_matrix_set_runtime_base_override(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return;
    const mod_destination_address_t address = mod_destination_address_make(track, id);
    uint8_t is_matrix_destination = 0U;
    for (uint8_t slot = 0U;
         slot < g_mod_matrix_audio_publication[owner].destination_count;
         ++slot)
    {
        if (g_mod_matrix_audio_publication[owner].destinations[slot].destination == address)
        {
            is_matrix_destination = 1U;
            break;
        }
    }
    if (is_matrix_destination == 0U)
    {
        return;
    }

    mod_matrix_base_override_t *entry = mod_matrix_find_base_override(track, id);
    if (entry == NULL)
    {
        entry = mod_matrix_alloc_base_override(track);
    }
    if (entry == NULL)
    {
        return;
    }

    entry->valid = 1U;
    entry->destination = (uint16_t)id;
    entry->value = value;

    mod_matrix_runtime_destination_t *const dst =
        mod_matrix_find_runtime_destination(&g_mod_matrix_runtime[owner], address);
    if (dst != NULL)
    {
        dst->base_value = value;
    }
    mod_matrix_poly_plan_t *const plan = &g_mod_matrix_poly_plan[owner];
    for (uint8_t i = 0U; i < plan->destination_count; ++i)
        if (plan->destinations[i].destination == address)
            plan->destinations[i].base_value = value;
}

void mod_matrix_clear_runtime_base_override(uint8_t track,
                                            param_id_t id,
                                            float base_value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return;
    const mod_destination_address_t address = mod_destination_address_make(track, id);
    mod_matrix_base_override_t *const entry =
        mod_matrix_find_base_override(track, id);
    if (entry != NULL)
    {
        entry->valid = 0U;
        entry->destination = (uint16_t)MOD_DESTINATION_NONE;
        entry->value = 0.0f;
    }

    mod_matrix_runtime_destination_t *const dst =
        mod_matrix_find_runtime_destination(&g_mod_matrix_runtime[owner], address);
    if (dst != NULL)
    {
        dst->base_value = base_value;
    }
    mod_matrix_poly_plan_t *const plan = &g_mod_matrix_poly_plan[owner];
    for (uint8_t i = 0U; i < plan->destination_count; ++i)
        if (plan->destinations[i].destination == address)
            plan->destinations[i].base_value = base_value;
}
