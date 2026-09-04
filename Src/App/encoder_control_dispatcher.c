#include "App/encoder_control_dispatcher.h"

#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "Param/live_parameter_migration.h"
#include "Param/param_registry.h"
#include "Track/track_mute.h"
#include "UI/ui_param.h"
#include "ui_event.h"
#include "encoders.h"
#include "main.h"

#define ENCODER_CONTROL_DISPATCH_MAX_EVENTS_PER_TICK ENCODER_DETENT_QUEUE_CAPACITY

void encoder_control_dispatcher_init(void)
{
    live_parameter_audio_publication_init();
}

uint8_t encoder_control_dispatcher_service(void)
{
    encoder_detent_event_t detent;
    uint8_t submitted = 0U;

    for (uint8_t i = 0U; i < ENCODER_CONTROL_DISPATCH_MAX_EVENTS_PER_TICK; ++i)
    {
        if (encoder_detent_event_pop(&detent) == 0U)
        {
            break;
        }

        if ((detent.encoder_id >= (uint8_t)ENC_COUNT)
                || (detent.direction == 0)
                || (detent.encoder_id >= ENCODER_BINDING_ENCODER_COUNT))
        {
            continue;
        }

        const uint32_t binding = detent.binding.entry[detent.encoder_id];
        if (encoder_binding_valid(binding) == 0U)
        {
            continue;
        }
        if (encoder_binding_route(binding) != ENCODER_BINDING_ROUTE_AUDIO)
        {
            /* Navigation and CONTROL-owned bindings stay on the UI path. */
            (void)ui_event_push_encoder(
                detent.encoder_id, detent.direction, detent.capture_tick,
                detent.ingress_serial,
                encoder_binding_shift_down(binding),
                encoder_binding_track(binding));
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

        const uint8_t value_track = (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
            ? track : 0U;
        float current_value = 0.0f;
        if (ui_param_get_audio_owned_command_value(parameter,
                                                   value_track,
                                                   &current_value) == 0U)
            continue;

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
                                                         current_value,
                                                         &target) == 0U)
        {
            continue;
        }

        float command_value = target.value;
        float global_canonical = target.value;
        param_registry_prepared_track_target_t prepared_track;
        param_registry_prepared_value_t prepared_global;
        audio_fx_control_prepare_context_t audio_fx_context = {0};
        const uint8_t track_scope = (uint8_t)(
            target.scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK);
        float global_model;
        if (track_scope == 0U)
        {
            if (param_registry_prepare_value(target.parameter_id, target.value,
                    &prepared_global) == 0U) continue;
            global_canonical = prepared_global.value;
            if (target.parameter_id == PARAM_MODFX_MODEL)
                global_model = global_canonical;
            else if (param_registry_query_global(PARAM_MODFX_MODEL,
                         &global_model) == 0U)
                continue;
        }
        if (((track_scope == 0U)
                && (param_registry_prepare_global_audio_command(
                    target.parameter_id, global_canonical,
                    (uint8_t)(global_model + 0.5f), &command_value) == 0U))
                || ((track_scope != 0U)
                    && (param_registry_prepare_track_control_target(
                        target.parameter_id, target.track, target.value,
                        &audio_fx_context, &prepared_track) == 0U)))
        {
            continue;
        }
        if (track_scope != 0U) command_value = prepared_track.canonical_value;

        if ((track_scope != 0U)
                && (target.parameter_id == PARAM_MIX_MUTE))
        {
            if (track_mute_set(target.track,
                    (prepared_track.canonical_value >= 0.5f) ? 1U : 0U) == 0U)
                continue;
            ui_param_note_user_tweak(detent.encoder_id, target.parameter_id);
            submitted++;
            continue;
        }

        const live_parameter_audio_bulk_t bulk = {
            .capture_tick = detent.capture_tick,
            .count = 1U,
            .item = {{
                .parameter_id = target.parameter_id,
                .scope = target.scope,
                .track = target.track,
                .slot = target.slot,
                .flags = LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
                .value = live_parameter_event_encode_float(command_value)
            }}
        };

        if (live_parameter_audio_publication_submit_bulk(&bulk))
        {
            const uint8_t installed = (track_scope != 0U)
                ? param_registry_install_prepared_track_control_target(&prepared_track)
                : param_registry_install_prepared_global_control_target(
                    target.parameter_id, global_canonical);
            if (installed == 0U) Error_Handler();
            ui_param_note_user_tweak(detent.encoder_id, target.parameter_id);
            submitted++;
        }
    }
    return submitted;
}
