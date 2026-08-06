#include "Core/encoder_control_dispatcher.h"

#include "Core/live_parameter_audio_queue.h"
#include "Core/live_parameter_audio_runtime.h"
#include "Core/live_parameter_event.h"
#include "Core/live_parameter_migration.h"
#include "encoders.h"

#define ENCODER_CONTROL_DISPATCH_MAX_EVENTS_PER_TICK ENCODER_DETENT_QUEUE_CAPACITY
#define ENCODER_CONTROL_SHADOW_COUNT ENCODER_DETENT_QUEUE_CAPACITY

typedef struct
{
    param_id_t parameter_id;
    uint8_t scope;
    uint8_t track;
    uint8_t slot;
    float value;
    uint8_t valid;
} encoder_control_shadow_t;

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
                                                               param_id_t parameter,
                                                               uint8_t scope,
                                                               uint8_t track,
                                                               uint8_t slot)
{
    for (uint8_t i = 0U; i < ENCODER_CONTROL_SHADOW_COUNT; ++i)
    {
        if ((shadow[i].valid != 0U)
                && (shadow[i].parameter_id == parameter)
                && (shadow[i].scope == scope)
                && (shadow[i].track == track)
                && (shadow[i].slot == slot))
        {
            return &shadow[i];
        }
    }

    for (uint8_t i = 0U; i < ENCODER_CONTROL_SHADOW_COUNT; ++i)
    {
        if (shadow[i].valid == 0U)
        {
            shadow[i].parameter_id = parameter;
            shadow[i].scope = scope;
            shadow[i].track = track;
            shadow[i].slot = slot;
            return &shadow[i];
        }
    }

    return &shadow[0];
}

void encoder_control_dispatcher_init(void)
{
    live_parameter_event_init();
    live_parameter_audio_queue_init();
    live_parameter_audio_runtime_init();
}

uint8_t encoder_control_dispatcher_service(void)
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

        if ((detent.encoder_id <= (uint8_t)ENC_PAGE)
                || (detent.encoder_id >= (uint8_t)ENC_COUNT)
                || (detent.direction == 0)
                || (detent.encoder_id >= ENCODER_BINDING_ENCODER_COUNT))
        {
            /* ENC_PAGE is navigation only; it must never become a DSP command. */
            continue;
        }

        const uint32_t binding = detent.binding.entry[detent.encoder_id];
        if ((encoder_binding_valid(binding) == 0U)
                || (encoder_binding_route(binding) != ENCODER_BINDING_ROUTE_AUDIO))
        {
            /* Legacy/navigation bindings stay exclusively on the UI path. */
            continue;
        }

        const param_id_t parameter = (param_id_t)encoder_binding_parameter(binding);
        const uint8_t scope = encoder_binding_scope(binding);
        const uint8_t track = encoder_binding_track(binding);
        const uint8_t slot = encoder_binding_slot(binding);
        if ((parameter >= PARAM_COUNT)
                || (live_parameter_is_audio_owned(parameter) == 0U))
        {
            continue;
        }

        encoder_control_shadow_t *const current = encoder_control_shadow_find(shadow,
                                                                                 parameter,
                                                                                 scope,
                                                                                 track,
                                                                                 slot);
        if (current->valid == 0U)
        {
            const uint8_t shadow_track = (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
                ? track : 0U;
            if (ui_param_get_audio_owned_command_value(parameter,
                                                       shadow_track,
                                                       &current->value) == 0U)
            {
                continue;
            }
            current->valid = 1U;
        }

        int8_t direction = detent.direction;
        if (detent.encoder_id == (uint8_t)ENC_PARAM_A)
        {
            direction = (int8_t)-direction;
        }

        ui_param_encoder_target_t target;
        if (ui_param_resolve_encoder_detent_from_binding(parameter,
                                                         scope,
                                                         track,
                                                         slot,
                                                         encoder_binding_shift_down(binding),
                                                         direction,
                                                         current->value,
                                                         &target) == 0U)
        {
            continue;
        }

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
            current->parameter_id = target.parameter_id;
            current->scope = target.scope;
            current->track = (target.scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
                ? target.track : 0U;
            current->slot = target.slot;
            current->value = target.value;
            current->valid = 1U;
            (void)ui_param_accept_audio_owned_command(target.parameter_id,
                                                      target.scope,
                                                      target.track,
                                                      target.value);
            submitted++;
        }
    }

    /* Handoff is bounded and keeps capture conversion out of the encoder/UI
     * resolver.  The converted events belong to the audio-owned schedule. */
    (void)live_parameter_audio_queue_drain();
    return submitted;
}
