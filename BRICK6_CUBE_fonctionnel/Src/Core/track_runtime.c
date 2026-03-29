#include "Core/track_runtime.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "ui_core.h"

#define TRACK_RUNTIME_FLAG_CAN_FILTER  (1U << 0)
#define TRACK_RUNTIME_FLAG_CAN_SYNTH   (1U << 1)
#define TRACK_RUNTIME_FLAG_CAN_PLAY    (1U << 2)
#define TRACK_RUNTIME_INSTANCE_NONE    0xFFU
#define TRACK_RUNTIME_DX7_MAX_INSTANCES 2U
#define TRACK_RUNTIME_MONOB_MAX_INSTANCES 8U

SEQ_STATE_D2 static track_runtime_ctx_t g_track_runtime_ctx[SEQ_TRACK_COUNT];

typedef struct
{
    uint8_t dx7_used;
    uint8_t monob_used;
} track_runtime_allocator_state_t;

static track_runtime_family_t track_runtime_family_from_ui(ui_track_family_t family)
{
    if (family == UI_TRACK_FAMILY_OFF)
    {
        return TRACK_RUNTIME_FAMILY_OFF;
    }

    if (family == UI_TRACK_FAMILY_SYNTH)
    {
        return TRACK_RUNTIME_FAMILY_SYNTH;
    }

    if (ui_track_family_is_input(family) != 0)
    {
        return TRACK_RUNTIME_FAMILY_INPUT;
    }

    return TRACK_RUNTIME_FAMILY_OTHER;
}

static track_runtime_type_t track_runtime_type_from_ui(ui_track_type_t type)
{
    switch (type)
    {
        case UI_TRACK_TYPE_AUDIO:
            return TRACK_RUNTIME_TYPE_AUDIO;

        case UI_TRACK_TYPE_HYBRID:
            return TRACK_RUNTIME_TYPE_HYBRID;

        case UI_TRACK_TYPE_DX7:
            return TRACK_RUNTIME_TYPE_DX7;

        case UI_TRACK_TYPE_MONOB:
            return TRACK_RUNTIME_TYPE_MONOB;

        default:
            return TRACK_RUNTIME_TYPE_OTHER;
    }
}

static uint8_t track_runtime_compute_flags(track_runtime_family_t family,
                                           track_runtime_type_t type)
{
    uint8_t flags = 0U;

    if (family != TRACK_RUNTIME_FAMILY_OFF)
    {
        flags |= TRACK_RUNTIME_FLAG_CAN_FILTER;
    }

    if (family == TRACK_RUNTIME_FAMILY_SYNTH)
    {
        flags |= TRACK_RUNTIME_FLAG_CAN_SYNTH;
        flags |= TRACK_RUNTIME_FLAG_CAN_PLAY;
    }

    if ((family == TRACK_RUNTIME_FAMILY_INPUT) && (type == TRACK_RUNTIME_TYPE_HYBRID))
    {
        flags |= TRACK_RUNTIME_FLAG_CAN_FILTER;
    }

    return flags;
}

static void track_runtime_set_unbound(track_runtime_ctx_t *ctx, track_runtime_bind_reason_t reason)
{
    ctx->engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    ctx->instance_id = TRACK_RUNTIME_INSTANCE_NONE;
    ctx->bind_state = TRACK_RUNTIME_BIND_UNBOUND;
    ctx->bind_reason = reason;
}

static void track_runtime_set_quota_blocked(track_runtime_ctx_t *ctx)
{
    ctx->engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    ctx->instance_id = TRACK_RUNTIME_INSTANCE_NONE;
    ctx->bind_state = TRACK_RUNTIME_BIND_QUOTA_BLOCKED;
    ctx->bind_reason = TRACK_RUNTIME_BIND_REASON_QUOTA_EXCEEDED;
}

static void track_runtime_set_bound(track_runtime_ctx_t *ctx,
                                    track_runtime_engine_t engine,
                                    uint8_t instance_id)
{
    ctx->engine = (uint8_t)engine;
    ctx->instance_id = instance_id;
    ctx->bind_state = TRACK_RUNTIME_BIND_BOUND;
    ctx->bind_reason = TRACK_RUNTIME_BIND_REASON_NONE;
}

static void track_runtime_bind_ctx(track_runtime_ctx_t *ctx,
                                   track_runtime_allocator_state_t *allocator)
{
    const track_runtime_family_t family = (track_runtime_family_t)ctx->family;
    const track_runtime_type_t type = (track_runtime_type_t)ctx->type;

    if (family == TRACK_RUNTIME_FAMILY_OFF)
    {
        track_runtime_set_unbound(ctx, TRACK_RUNTIME_BIND_REASON_TRACK_OFF);
        return;
    }

    if (family == TRACK_RUNTIME_FAMILY_INPUT)
    {
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_AUDIO_TRACK, ctx->track_id);
        return;
    }

    if (family != TRACK_RUNTIME_FAMILY_SYNTH)
    {
        track_runtime_set_unbound(ctx, TRACK_RUNTIME_BIND_REASON_UNSUPPORTED);
        return;
    }

    if (type == TRACK_RUNTIME_TYPE_DX7)
    {
        if (allocator->dx7_used >= TRACK_RUNTIME_DX7_MAX_INSTANCES)
        {
            track_runtime_set_quota_blocked(ctx);
            return;
        }

        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_DX7, allocator->dx7_used);
        allocator->dx7_used++;
        return;
    }

    if (type == TRACK_RUNTIME_TYPE_MONOB)
    {
        if (allocator->monob_used >= TRACK_RUNTIME_MONOB_MAX_INSTANCES)
        {
            track_runtime_set_quota_blocked(ctx);
            return;
        }

        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_MONOB, allocator->monob_used);
        allocator->monob_used++;
        return;
    }

    track_runtime_set_unbound(ctx, TRACK_RUNTIME_BIND_REASON_UNSUPPORTED);
}

void track_runtime_init(void)
{
    memset(&g_track_runtime_ctx, 0, sizeof(g_track_runtime_ctx));
    track_runtime_refresh_all();
}

void track_runtime_refresh_all(void)
{
    track_runtime_allocator_state_t allocator = { 0U, 0U };

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        const ui_track_family_t ui_family = ui_get_track_family(track);
        const ui_track_type_t ui_type = ui_get_track_type(track);

        const track_runtime_family_t family = track_runtime_family_from_ui(ui_family);
        const track_runtime_type_t type = track_runtime_type_from_ui(ui_type);

        ctx->track_id = track;
        ctx->family = (uint8_t)family;
        ctx->type = (uint8_t)type;
        ctx->flags = track_runtime_compute_flags(family, type);
        ctx->engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
        ctx->instance_id = TRACK_RUNTIME_INSTANCE_NONE;
        ctx->bind_state = TRACK_RUNTIME_BIND_UNBOUND;
        ctx->bind_reason = TRACK_RUNTIME_BIND_REASON_NONE;

        track_runtime_bind_ctx(ctx, &allocator);
    }
}

void track_runtime_refresh_track(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    /* Quotas are global to all tracks, so one-track refresh recomputes a full pass. */
    track_runtime_refresh_all();
}

const track_runtime_ctx_t *track_runtime_get_ctx(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0;
    }

    return &g_track_runtime_ctx[track];
}

track_runtime_param_rule_t track_runtime_get_param_rule(param_id_t param)
{
    track_runtime_param_rule_t rule = {
        .domain = TRACK_RUNTIME_PARAM_DOMAIN_NONE,
        .resource = TRACK_RUNTIME_RESOURCE_NONE,
        .cardinality = TRACK_RUNTIME_CARDINALITY_PER_TRACK,
        .status = TRACK_RUNTIME_PARAM_ALLOWED
    };

    switch (param)
    {
        case PARAM_FILTER_TYPE:
        case PARAM_FILTER_CUTOFF:
        case PARAM_FILTER_RESONANCE:
        case PARAM_FILTER_KEYTRK:
        case PARAM_FILTER_ENVRST:
        case PARAM_FILTER_ENVDLY:
        case PARAM_FILTER_DRIVE:
        case PARAM_FILTER_EQ_LOW:
        case PARAM_FILTER_EQ_MID:
        case PARAM_FILTER_EQ_HIGH:
        case PARAM_MONOB_FILTER_TYPE:
        case PARAM_MONOB_FILTER_CUTOFF:
        case PARAM_MONOB_FILTER_RESONANCE:
        case PARAM_MONOB_FILTER_EG_AMT:
        case PARAM_MONOB_FILTER_ATTACK:
        case PARAM_MONOB_FILTER_DECAY:
        case PARAM_MONOB_FILTER_SUSTAIN:
        case PARAM_MONOB_FILTER_RELEASE:
        case PARAM_MONOB_FILTER_KEYTRK:
        case PARAM_MONOB_FILTER_ENVRST:
        case PARAM_MONOB_FILTER_ENVDLY:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_COLORS;
            rule.resource = TRACK_RUNTIME_RESOURCE_FILTER;
            return rule;

        case PARAM_DX7_ALGORITHM:
        case PARAM_DX7_FEEDBACK:
        case PARAM_DX7_TRANSPOSE:
        case PARAM_DX7_LFO_SPEED:
        case PARAM_DX7_LFO_DELAY:
        case PARAM_DX7_LFO_PITCH_MOD_DEPTH:
        case PARAM_DX7_LFO_AMP_MOD_DEPTH:
        case PARAM_DX7_PITCH_BEND_RANGE:
        case PARAM_DX7_PORTAMENTO_TIME:
        case PARAM_DX7_MONO_MODE:
        case PARAM_DX7_OPERATOR_MASK:
        case PARAM_DX7_OPERATOR_1_LEVEL:
        case PARAM_DX7_OPERATOR_2_LEVEL:
        case PARAM_DX7_OPERATOR_3_LEVEL:
        case PARAM_DX7_OPERATOR_4_LEVEL:
        case PARAM_MONOB_OSC1_WAVE:
        case PARAM_MONOB_OSC2_WAVE:
        case PARAM_MONOB_OSC3_WAVE:
        case PARAM_MONOB_SUB_WAVE:
        case PARAM_MONOB_OSC1_RANGE:
        case PARAM_MONOB_OSC2_RANGE:
        case PARAM_MONOB_OSC3_RANGE:
        case PARAM_MONOB_SUB_OCTAVE:
        case PARAM_MONOB_OSC1_DETUNE:
        case PARAM_MONOB_OSC2_DETUNE:
        case PARAM_MONOB_OSC3_DETUNE:
        case PARAM_MONOB_OSC1_MIX:
        case PARAM_MONOB_OSC2_MIX:
        case PARAM_MONOB_OSC3_MIX:
        case PARAM_MONOB_SUB_MIX:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_TONE;
            rule.resource = TRACK_RUNTIME_RESOURCE_SYNTH;
            return rule;

        case PARAM_SEQ_PLAY_V1_NOTE:
        case PARAM_SEQ_PLAY_V1_VEL:
        case PARAM_SEQ_PLAY_V1_LEN:
        case PARAM_SEQ_PLAY_V1_MICTIM:
        case PARAM_SEQ_PLAY_V2_NOTE:
        case PARAM_SEQ_PLAY_V2_VEL:
        case PARAM_SEQ_PLAY_V2_LEN:
        case PARAM_SEQ_PLAY_V2_MICTIM:
        case PARAM_SEQ_PLAY_V3_NOTE:
        case PARAM_SEQ_PLAY_V3_VEL:
        case PARAM_SEQ_PLAY_V3_LEN:
        case PARAM_SEQ_PLAY_V3_MICTIM:
        case PARAM_SEQ_PLAY_V4_NOTE:
        case PARAM_SEQ_PLAY_V4_VEL:
        case PARAM_SEQ_PLAY_V4_LEN:
        case PARAM_SEQ_PLAY_V4_MICTIM:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_PLAY;
            rule.resource = TRACK_RUNTIME_RESOURCE_PLAY;
            return rule;

        case PARAM_MASTER_GAIN:
        case PARAM_POST_GAIN:
        case PARAM_OUTPUT_COMP:
            rule.cardinality = TRACK_RUNTIME_CARDINALITY_GLOBAL;
            rule.status = TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED;
            return rule;

        default:
            return rule;
    }
}

track_runtime_param_status_t track_runtime_get_effective_param_status(uint8_t track, param_id_t param)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= PARAM_COUNT))
    {
        return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }

    track_runtime_refresh_track(track);

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
    {
        return TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if (ctx == 0)
    {
        return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }

    switch (rule.resource)
    {
        case TRACK_RUNTIME_RESOURCE_NONE:
            return TRACK_RUNTIME_PARAM_ALLOWED;

        case TRACK_RUNTIME_RESOURCE_FILTER:
            if (ctx->bind_state == TRACK_RUNTIME_BIND_QUOTA_BLOCKED)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_FILTER) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        case TRACK_RUNTIME_RESOURCE_SYNTH:
            if (ctx->bind_state == TRACK_RUNTIME_BIND_QUOTA_BLOCKED)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_SYNTH) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        case TRACK_RUNTIME_RESOURCE_PLAY:
            return ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_PLAY) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        default:
            return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }
}
