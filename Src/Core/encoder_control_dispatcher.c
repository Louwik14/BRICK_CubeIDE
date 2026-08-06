#include "Core/encoder_control_dispatcher.h"

#include "Core/live_parameter_audio_queue.h"
#include "Core/live_parameter_event.h"
#include "Core/track_runtime.h"
#include "encoders.h"

#define ENCODER_CONTROL_DISPATCH_MAX_EVENTS_PER_TICK ENCODER_DETENT_QUEUE_CAPACITY
#define ENCODER_CONTROL_SHADOW_COUNT 4U

typedef struct
{
    param_id_t parameter_id;
    float value;
    uint8_t valid;
} encoder_control_shadow_t;

static uint8_t encoder_control_parameter_is_global_audio(param_id_t parameter)
{
    switch (parameter)
    {
        case PARAM_MIX_SEND0_FX:
        case PARAM_MIX_SEND1_FX:
        case PARAM_EQ_LOW_DB:
        case PARAM_EQ_MID_DB:
        case PARAM_EQ_HIGH_DB:
        case PARAM_SAT_TONE:
        case PARAM_SAT_BIAS:
        case PARAM_SAT_DRIVE:
        case PARAM_SAT_MIX:
        case PARAM_BUS_COMP_THRESHOLD_DB:
        case PARAM_BUS_COMP_RATIO:
        case PARAM_BUS_COMP_ATTACK_INDEX:
        case PARAM_BUS_COMP_RELEASE_INDEX:
        case PARAM_BUS_COMP_MAKEUP_DB:
        case PARAM_BUS_COMP_AUTO_MAKEUP:
        case PARAM_BUS_COMP_DRYWET:
        case PARAM_BUS_COMP_HPF_HZ:
        case PARAM_COMP_MODEL:
        case PARAM_COMP_DETECT:
        case PARAM_COMP_KNEE_DB:
        case PARAM_COMP_DELUGE_SAT:
        case PARAM_MASTER_GAIN:
        case PARAM_POST_GAIN:
        case PARAM_OUTPUT_COMP:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t encoder_control_parameter_is_audio(param_id_t parameter)
{
    if (parameter >= PARAM_COUNT)
    {
        return 0U;
    }

    if (encoder_control_parameter_is_global_audio(parameter) != 0U)
    {
        return 1U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(parameter);
    if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
    {
        return 1U;
    }

    return (uint8_t)((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
                     || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
                     || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD)
                     || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX));
}

static void encoder_control_shadow_reset(encoder_control_shadow_t *shadow)
{
    for (uint8_t i = 0U; i < ENCODER_CONTROL_SHADOW_COUNT; ++i)
    {
        shadow[i].parameter_id = PARAM_COUNT;
        shadow[i].value = 0.0f;
        shadow[i].valid = 0U;
    }
}

static encoder_control_shadow_t *encoder_control_shadow_find(encoder_control_shadow_t *shadow,
                                                               param_id_t parameter)
{
    for (uint8_t i = 0U; i < ENCODER_CONTROL_SHADOW_COUNT; ++i)
    {
        if ((shadow[i].valid != 0U) && (shadow[i].parameter_id == parameter))
        {
            return &shadow[i];
        }
    }

    for (uint8_t i = 0U; i < ENCODER_CONTROL_SHADOW_COUNT; ++i)
    {
        if (shadow[i].valid == 0U)
        {
            shadow[i].parameter_id = parameter;
            return &shadow[i];
        }
    }

    return &shadow[0];
}

void encoder_control_dispatcher_init(void)
{
    live_parameter_event_init();
    live_parameter_audio_queue_init();
}

uint8_t encoder_control_dispatcher_service(const ui_param_encoder_context_t *context)
{
    encoder_control_shadow_t shadow[ENCODER_CONTROL_SHADOW_COUNT];
    encoder_detent_event_t detent;
    uint8_t submitted = 0U;

    encoder_control_shadow_reset(shadow);

    for (uint8_t i = 0U; i < ENCODER_CONTROL_DISPATCH_MAX_EVENTS_PER_TICK; ++i)
    {
        if (encoder_detent_event_pop(&detent) == 0U)
        {
            break;
        }

        if ((context == 0)
                || (context->valid == 0U)
                || (detent.encoder_id <= (uint8_t)ENC_PAGE)
                || (detent.encoder_id >= (uint8_t)ENC_COUNT)
                || (detent.direction == 0))
        {
            /* ENC_PAGE is navigation only; it must never become a DSP command. */
            continue;
        }

        const param_id_t parameter = context->bank.params[detent.encoder_id];
        if (encoder_control_parameter_is_audio(parameter) == 0U)
        {
            /* Structural, sequence and navigation controls stay on the legacy UI path. */
            continue;
        }

        encoder_control_shadow_t *const current = encoder_control_shadow_find(shadow, parameter);
        if (current->valid == 0U)
        {
            current->value = ui_param_get_active_track_display_value(parameter,
                                                                       context->active_track);
            current->valid = 1U;
        }

        int8_t direction = detent.direction;
        if (detent.encoder_id == (uint8_t)ENC_PARAM_A)
        {
            direction = (int8_t)-direction;
        }

        ui_param_encoder_target_t target;
        if (ui_param_resolve_encoder_detent(context,
                                            detent.encoder_id,
                                            direction,
                                            current->value,
                                            &target) == 0U)
        {
            continue;
        }

        current->parameter_id = target.parameter_id;
        current->value = target.value;
        current->valid = 1U;

        const live_parameter_event_t event = {
            .capture_tick = detent.capture_tick,
            .ingress_serial = detent.ingress_serial,
            .parameter_id = target.parameter_id,
            .source = LIVE_PARAMETER_EVENT_SOURCE_ENCODER,
            .scope = target.scope,
            .track = target.track,
            .slot = target.slot,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS
                                | ((uint16_t)detent.encoder_id << LIVE_PARAMETER_EVENT_FLAG_ENCODER_SHIFT)),
            .value = live_parameter_event_encode_float(target.value)
        };

        if (live_parameter_event_submit(&event))
        {
            submitted++;
        }
    }

    /* Handoff is bounded and keeps capture conversion out of the encoder/UI
     * resolver.  The converted events belong to the audio-owned schedule. */
    (void)live_parameter_audio_queue_drain();
    return submitted;
}
