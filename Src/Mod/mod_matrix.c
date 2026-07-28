#include "Mod/mod_matrix.h"

#include <string.h>

#include "Audio/mixer.h"
#include "Core/track_sound_state.h"
#include "Mod/mod_destination_catalog.h"
#include "Param/param_registry.h"

typedef struct
{
    uint8_t valid;
    uint16_t destination;
    float base_value;
    float sum;
    float min_value;
    float max_value;
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
    float multi[2];
    float slew[2];
    uint8_t multi_valid[2];
    uint8_t slew_valid[2];
} mod_matrix_operator_runtime_t;

static mod_matrix_runtime_track_t g_mod_matrix_runtime[SEQ_TRACK_COUNT];
static mod_matrix_route_cache_t g_mod_matrix_route_cache[SEQ_TRACK_COUNT];
static mod_matrix_operator_runtime_t g_mod_matrix_operator_runtime[SEQ_TRACK_COUNT];
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
    track_sound_state_t *const state = track_sound_state_get(track);
    if ((state == NULL) || (slot >= MOD_MATRIX_SLOT_COUNT))
    {
        return NULL;
    }
    return &state->mod_matrix[slot];
}

static const track_mod_matrix_slot_t *mod_matrix_track_slot_const(uint8_t track, uint8_t slot)
{
    const track_sound_state_t *const state = track_sound_state_get_const(track);
    if ((state == NULL) || (slot >= MOD_MATRIX_SLOT_COUNT))
    {
        return NULL;
    }
    return &state->mod_matrix[slot];
}

static uint8_t mod_matrix_slot_is_configured(const track_mod_matrix_slot_t *slot)
{
    return ((slot != NULL)
            && (slot->enabled != 0U)
            && (slot->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)
            && (slot->source < (uint8_t)MOD_MATRIX_SOURCE_COUNT)
            && (slot->destination < (uint16_t)PARAM_COUNT)
            && (slot->depth != 0.0f)) ? 1U : 0U;
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

static ui_track_family_t mod_matrix_ui_family_from_ctx(const track_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return UI_TRACK_FAMILY_OFF;
    }

    switch ((track_runtime_family_t)ctx->family)
    {
        case TRACK_RUNTIME_FAMILY_INPUT:
            return UI_TRACK_FAMILY_INPUT1;
        case TRACK_RUNTIME_FAMILY_SYNTH:
            return UI_TRACK_FAMILY_SYNTH;
        case TRACK_RUNTIME_FAMILY_SAMPLER:
            return UI_TRACK_FAMILY_SAMPLER;
        case TRACK_RUNTIME_FAMILY_DRUM:
            return UI_TRACK_FAMILY_DRUM;
        case TRACK_RUNTIME_FAMILY_MASTER:
            return UI_TRACK_FAMILY_MASTER;
        case TRACK_RUNTIME_FAMILY_MIDI:
            return UI_TRACK_FAMILY_MIDI;
        case TRACK_RUNTIME_FAMILY_OFF:
        default:
            return UI_TRACK_FAMILY_OFF;
    }
}

static ui_track_type_t mod_matrix_ui_type_from_ctx(const track_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return UI_TRACK_TYPE_AUDIO;
    }

    switch ((track_runtime_type_t)ctx->type)
    {
        case TRACK_RUNTIME_TYPE_HYBRID:
            return UI_TRACK_TYPE_HYBRID;
        case TRACK_RUNTIME_TYPE_RAM:
            return UI_TRACK_TYPE_RAM;
        case TRACK_RUNTIME_TYPE_PRISM:
            return UI_TRACK_TYPE_PRISM;
        case TRACK_RUNTIME_TYPE_WAVE:
            return UI_TRACK_TYPE_WAVE;
        case TRACK_RUNTIME_TYPE_STACK:
            return UI_TRACK_TYPE_STACK;
        case TRACK_RUNTIME_TYPE_DRUM_TRX_BD:
            return UI_TRACK_TYPE_DRUM_TRX_BD;
        case TRACK_RUNTIME_TYPE_MIDI:
            return UI_TRACK_TYPE_MIDI;
        case TRACK_RUNTIME_TYPE_STREAM:
            return UI_TRACK_TYPE_STREAM;
        case TRACK_RUNTIME_TYPE_MASTER_FX:
            return UI_TRACK_TYPE_MASTER_FX;
        case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
            return UI_TRACK_TYPE_DRUM_BD_ANALOG;
        case TRACK_RUNTIME_TYPE_LOOPER:
            return UI_TRACK_TYPE_LOOPER;
        case TRACK_RUNTIME_TYPE_MULTI:
            return UI_TRACK_TYPE_MULTI;
        case TRACK_RUNTIME_TYPE_AUDIO:
        default:
            return UI_TRACK_TYPE_AUDIO;
    }
}

static uint8_t mod_matrix_track_selected_slot(uint8_t track, uint8_t *out_slot)
{
    const track_sound_state_t *const state = track_sound_state_get_const(track);
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
                                            const track_runtime_ctx_t *ctx)
{
    if ((slot == NULL)
            || (slot->enabled == 0U)
            || (slot->source == (uint8_t)MOD_MATRIX_SOURCE_NONE)
            || (slot->source >= MOD_MATRIX_SOURCE_COUNT)
            || (slot->destination >= (uint16_t)PARAM_COUNT)
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
            if ((ctx == NULL)
                    || (ctx->mix_track_id >= MIXER_MAX_TRACKS)
                    || (track_runtime_supports_vca_gate(ctx) == 0U))
            {
                return 0U;
            }
            break;

        case MOD_MATRIX_SOURCE_ENV_FLT:
            if (track_runtime_get_effective_param_status(track, PARAM_FILTER_ATTACK) != TRACK_RUNTIME_PARAM_ALLOWED)
            {
                return 0U;
            }
            break;

        default:
            return 0U;
    }

    return mod_destination_catalog_supported_fast(track, (param_id_t)slot->destination, family, type, ctx);
}

static void mod_matrix_release_destination(uint8_t track,
                                           mod_matrix_runtime_destination_t *dst,
                                           ui_track_family_t family,
                                           ui_track_type_t type,
                                           const track_runtime_ctx_t *ctx)
{
    if ((dst == NULL) || (dst->valid == 0U))
    {
        return;
    }

    if ((dst->destination < (uint16_t)PARAM_COUNT)
            && (mod_destination_catalog_supported_fast(track, (param_id_t)dst->destination, family, type, ctx) != 0U))
    {
        (void)mod_destination_catalog_apply_rt(track, (param_id_t)dst->destination, ctx, dst->base_value);
    }

    dst->valid = 0U;
    dst->destination = (uint16_t)MOD_DESTINATION_NONE;
    dst->base_value = 0.0f;
    dst->sum = 0.0f;
    dst->min_value = 0.0f;
    dst->max_value = 127.0f;
}

static mod_matrix_runtime_destination_t *mod_matrix_find_runtime_destination(mod_matrix_runtime_track_t *rt,
                                                                            param_id_t destination)
{
    if (rt == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        if ((rt->destinations[i].valid != 0U)
                && (rt->destinations[i].destination == (uint16_t)destination))
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
                                                      param_id_t destination,
                                                      mod_matrix_runtime_destination_t **out_dst)
{
    if ((track >= SEQ_TRACK_COUNT) || (rt == NULL) || (destination >= PARAM_COUNT) || (out_dst == NULL))
    {
        return 0U;
    }

    mod_matrix_runtime_destination_t *dst = mod_matrix_find_runtime_destination(rt, destination);
    if (dst == NULL)
    {
        dst = mod_matrix_alloc_runtime_destination(rt);
        if (dst == NULL)
        {
            return 0U;
        }

        float base = 0.0f;
        if (param_registry_get_track_value(destination, track, &base) == 0U)
        {
            return 0U;
        }

        const param_desc_t *const desc = &param_registry[destination];
        dst->valid = 1U;
        dst->destination = (uint16_t)destination;
        dst->base_value = base;
        dst->min_value = ((destination == PARAM_LFO1_RATE)
                          || (destination == PARAM_LFO2_RATE)
                          || (destination == PARAM_LFO3_RATE)) ? 0.0f : desc->min;
        dst->max_value = desc->max;
    }

    *out_dst = dst;
    return 1U;
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
    memset(g_mod_matrix_operator_runtime, 0, sizeof(g_mod_matrix_operator_runtime));
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
        {
            g_mod_matrix_runtime[track].destinations[i].destination = (uint16_t)MOD_DESTINATION_NONE;
            g_mod_matrix_runtime[track].destinations[i].max_value = 127.0f;
        }
    }
    mod_matrix_rebuild_route_cache_all();
}

void mod_matrix_rebuild_route_cache_track(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    uint16_t source_mask = 0U;
    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
        if (mod_matrix_slot_is_configured(s) != 0U)
        {
            source_mask |= (uint16_t)(1U << s->source);
        }
    }

    g_mod_matrix_route_cache[track].source_mask = source_mask;
    g_mod_matrix_route_cache[track].any_route = (source_mask != 0U) ? 1U : 0U;
    mod_matrix_recompute_global_route_flag();
}

void mod_matrix_rebuild_route_cache_all(void)
{
    g_mod_matrix_any_route = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint16_t source_mask = 0U;
        for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
        {
            const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
            if (mod_matrix_slot_is_configured(s) != 0U)
            {
                source_mask |= (uint16_t)(1U << s->source);
            }
        }
        g_mod_matrix_route_cache[track].source_mask = source_mask;
        g_mod_matrix_route_cache[track].any_route = (source_mask != 0U) ? 1U : 0U;
        if (source_mask != 0U)
        {
            g_mod_matrix_any_route = 1U;
        }
    }
}

uint8_t mod_matrix_set_selected_slot(uint8_t track, float value)
{
    track_sound_state_t *const state = track_sound_state_get(track);
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
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL)
    {
        return 0U;
    }

    track_runtime_refresh_track(track);
    const uint16_t max_index = (uint16_t)(mod_destination_catalog_count(track) - 1U);
    const uint16_t index = (uint16_t)mod_matrix_clampf(value, 0.0f, (float)max_index);
    s->destination = (uint16_t)mod_destination_catalog_param_from_index(track, index);
    s->enabled = ((s->destination != (uint16_t)MOD_DESTINATION_NONE)
                  && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)) ? 1U : 0U;
    mod_matrix_rebuild_route_cache_track(track);
    mod_matrix_release_track(track, ui_get_track_family(track), ui_get_track_type(track), track_runtime_get_ctx(track));
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
    if (s->depth == 0.0f)
    {
        mod_matrix_release_track(track, ui_get_track_family(track), ui_get_track_type(track), track_runtime_get_ctx(track));
    }
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
    mod_matrix_release_track(track, ui_get_track_family(track), ui_get_track_type(track), track_runtime_get_ctx(track));
    return 1U;
}

uint8_t mod_matrix_get_slot_destination_index(uint8_t track, uint8_t slot, float *out_value)
{
    const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
    if ((s == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = (float)mod_destination_catalog_index_from_param(track, (param_id_t)s->destination);
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
    track_sound_state_t *const state = track_sound_state_get(track);
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
    return 1U;
}

uint8_t mod_matrix_get_multi_source(uint8_t track, uint8_t op, uint8_t input, float *out_value)
{
    const track_sound_state_t *const state = track_sound_state_get_const(track);
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
    track_sound_state_t *const state = track_sound_state_get(track);
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
    return 1U;
}

uint8_t mod_matrix_get_slew_source(uint8_t track, uint8_t op, float *out_value)
{
    const track_sound_state_t *const state = track_sound_state_get_const(track);
    if ((state == NULL) || (op >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = (float)state->mod_slew[op].source;
    return 1U;
}

uint8_t mod_matrix_set_slew_amount(uint8_t track, uint8_t op, float value)
{
    track_sound_state_t *const state = track_sound_state_get(track);
    if ((state == NULL) || (op >= 2U))
    {
        return 0U;
    }

    const float amount = mod_matrix_clampf(value, 0.0f, 1.0f);
    if (state->mod_slew[op].amount != amount)
    {
        state->mod_slew[op].amount = amount;
    }
    return 1U;
}

uint8_t mod_matrix_get_slew_amount(uint8_t track, uint8_t op, float *out_value)
{
    const track_sound_state_t *const state = track_sound_state_get_const(track);
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

static uint8_t mod_matrix_slew_direct_cycle(const track_sound_state_t *state, uint8_t op);

static uint8_t mod_matrix_source_depends_on(const track_sound_state_t *state,
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
            const uint8_t src_a = state->mod_multi[op].source_a;
            const uint8_t src_b = state->mod_multi[op].source_b;
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
                                                state->mod_slew[op].source,
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

static uint8_t mod_matrix_slew_direct_cycle(const track_sound_state_t *state, uint8_t op)
{
    if ((state == NULL) || (op >= 2U))
    {
        return 1U;
    }

    const uint8_t src = state->mod_slew[op].source;
    const uint8_t self = (op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1 : (uint8_t)MOD_MATRIX_SOURCE_SLEW2;
    const uint8_t other = (op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW2 : (uint8_t)MOD_MATRIX_SOURCE_SLEW1;
    if (src == self)
    {
        return 1U;
    }
    if (src == other)
    {
        const uint8_t other_src = state->mod_slew[1U - op].source;
        if (other_src == self)
        {
            return 1U;
        }
    }
    return 0U;
}

void mod_matrix_process_operators(uint8_t track,
                                  float source_values[MOD_MATRIX_SOURCE_COUNT],
                                  uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                  uint32_t elapsed_frames)
{
    if ((track >= SEQ_TRACK_COUNT) || (source_values == NULL) || (source_valid == NULL))
    {
        return;
    }
    if (mod_matrix_track_has_operator_route(track) == 0U)
    {
        return;
    }

    const track_sound_state_t *const state = track_sound_state_get_const(track);
    mod_matrix_operator_runtime_t *const rt = &g_mod_matrix_operator_runtime[track];
    if (state == NULL)
    {
        return;
    }

    for (uint8_t op = 0U; op < 2U; ++op)
    {
        const uint8_t src_a = state->mod_multi[op].source_a;
        const uint8_t src_b = state->mod_multi[op].source_b;
        float a = 0.0f;
        float b = 0.0f;
        const uint8_t invalid_src = (uint8_t)((src_a == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                                             || (src_a == (uint8_t)MOD_MATRIX_SOURCE_MULTI2)
                                             || (src_b == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                                             || (src_b == (uint8_t)MOD_MATRIX_SOURCE_MULTI2));
        if ((invalid_src == 0U)
                && (mod_matrix_get_operator_source_value(rt, source_values, source_valid, src_a, &a) != 0U)
                && (mod_matrix_get_operator_source_value(rt, source_values, source_valid, src_b, &b) != 0U))
        {
            rt->multi[op] = mod_matrix_clampf(a * b, -1.0f, 1.0f);
            rt->multi_valid[op] = 1U;
            source_values[(op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_MULTI1 : (uint8_t)MOD_MATRIX_SOURCE_MULTI2] = rt->multi[op];
            source_valid[(op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_MULTI1 : (uint8_t)MOD_MATRIX_SOURCE_MULTI2] = 1U;
        }
        else
        {
            rt->multi_valid[op] = 0U;
        }
    }

    for (uint8_t op = 0U; op < 2U; ++op)
    {
        const uint8_t src = state->mod_slew[op].source;
        float input = 0.0f;
        if ((mod_matrix_slew_direct_cycle(state, op) == 0U)
                && (mod_matrix_get_operator_source_value(rt, source_values, source_valid, src, &input) != 0U))
        {
            const float amount = mod_matrix_clampf(state->mod_slew[op].amount, 0.0f, 1.0f);
            const float tau_frames = 16.0f + (amount * amount * 48000.0f);
            const float elapsed = (elapsed_frames == 0U) ? 1.0f : (float)elapsed_frames;
            const float coeff = (amount <= 0.0f) ? 1.0f : (elapsed / (tau_frames + elapsed));
            if (rt->slew_valid[op] == 0U)
            {
                rt->slew[op] = input;
            }
            else
            {
                rt->slew[op] += (input - rt->slew[op]) * coeff;
            }
            rt->slew[op] = mod_matrix_clampf(rt->slew[op], -1.0f, 1.0f);
            rt->slew_valid[op] = 1U;
            source_values[(op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1 : (uint8_t)MOD_MATRIX_SOURCE_SLEW2] = rt->slew[op];
            source_valid[(op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1 : (uint8_t)MOD_MATRIX_SOURCE_SLEW2] = 1U;
        }
        else
        {
            rt->slew_valid[op] = 0U;
        }
    }
}

uint8_t mod_matrix_source_has_active_route(uint8_t track,
                                           mod_matrix_source_t source,
                                           ui_track_family_t family,
                                           ui_track_type_t type,
                                           const track_runtime_ctx_t *ctx)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (source == MOD_MATRIX_SOURCE_NONE)
            || ((uint8_t)source >= MOD_MATRIX_SOURCE_COUNT))
    {
        return 0U;
    }

    if (mod_matrix_track_has_configured_source(track, source) == 0U)
    {
        const uint16_t operator_mask = (uint16_t)((uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
            | (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_MULTI2)
            | (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_SLEW1)
            | (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_SLEW2));
        if ((g_mod_matrix_route_cache[track].source_mask & operator_mask) == 0U)
        {
            return 0U;
        }
    }

    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
        if ((s != NULL)
                && (mod_matrix_slot_is_effective(track, s, family, type, ctx) != 0U))
        {
            if (s->source == (uint8_t)source)
            {
                return 1U;
            }
            if (mod_matrix_source_depends_on(track_sound_state_get_const(track),
                                             s->source,
                                             (uint8_t)source,
                                             0U) != 0U)
            {
                return 1U;
            }
        }
    }
    return 0U;
}

void mod_matrix_process_track(uint8_t track,
                              const track_runtime_ctx_t *ctx,
                              const float source_values[MOD_MATRIX_SOURCE_COUNT],
                              const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT])
{
    if ((track >= SEQ_TRACK_COUNT) || (source_values == NULL) || (source_valid == NULL))
    {
        return;
    }
    if (mod_matrix_track_has_configured_route(track) == 0U)
    {
        return;
    }

    mod_matrix_runtime_track_t *const rt = &g_mod_matrix_runtime[track];
    const ui_track_family_t family = mod_matrix_ui_family_from_ctx(ctx);
    const ui_track_type_t type = mod_matrix_ui_type_from_ctx(ctx);
    uint8_t touched[MOD_MATRIX_SLOT_COUNT] = {0};

    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        rt->destinations[i].sum = 0.0f;
    }

    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
        if (mod_matrix_slot_is_effective(track, s, family, type, ctx) == 0U)
        {
            continue;
        }

        const uint8_t source = s->source;
        if ((source >= MOD_MATRIX_SOURCE_COUNT) || (source_valid[source] == 0U))
        {
            continue;
        }

        mod_matrix_runtime_destination_t *dst = NULL;
        const param_id_t destination = (param_id_t)s->destination;
        if (mod_matrix_runtime_destination_prepare(track, rt, destination, &dst) == 0U)
        {
            continue;
        }

        const uint8_t dst_index = (uint8_t)(dst - &rt->destinations[0]);
        touched[dst_index] = 1U;
        dst->sum += source_values[source] * (s->depth / 127.0f) * (dst->max_value - dst->min_value);
    }

    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        mod_matrix_runtime_destination_t *const dst = &rt->destinations[i];
        if (dst->valid == 0U)
        {
            continue;
        }
        if (touched[i] == 0U)
        {
            mod_matrix_release_destination(track, dst, family, type, ctx);
            continue;
        }

        const float value = mod_matrix_clampf(dst->base_value + dst->sum, dst->min_value, dst->max_value);
        (void)mod_destination_catalog_apply_rt(track, (param_id_t)dst->destination, ctx, value);
    }
}

void mod_matrix_release_track(uint8_t track,
                              ui_track_family_t family,
                              ui_track_type_t type,
                              const track_runtime_ctx_t *ctx)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        mod_matrix_release_destination(track, &g_mod_matrix_runtime[track].destinations[i], family, type, ctx);
    }
}

void mod_matrix_resync_base_on_authoritative_write(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    mod_matrix_runtime_destination_t *const dst = mod_matrix_find_runtime_destination(&g_mod_matrix_runtime[track], id);
    if (dst != NULL)
    {
        dst->base_value = value;
    }
}
