#include "Core/encoder_control_dispatcher.h"

#include "Core/live_parameter_audio_queue.h"
#include "Core/live_parameter_audio_runtime.h"
#include "Core/live_parameter_event.h"
#include "encoders.h"

#define ENCODER_CONTROL_DISPATCH_MAX_EVENTS_PER_TICK ENCODER_DETENT_QUEUE_CAPACITY
#define ENCODER_CONTROL_SHADOW_COUNT 4U

typedef struct
{
    param_id_t parameter_id;
    float value;
    uint8_t valid;
} encoder_control_shadow_t;

static uint8_t encoder_control_parameter_is_migrated_audio(param_id_t parameter)
{
    switch (parameter)
    {
        /* Active VCA/filter contracts. */
        case PARAM_VCA_ATTACK:
        case PARAM_VCA_DECAY:
        case PARAM_VCA_SUSTAIN:
        case PARAM_VCA_RELEASE:
        case PARAM_FILTER_CUTOFF:
        case PARAM_FILTER_RESONANCE:

        /* Track mix controls. */
        case PARAM_MIX_LEVEL:
        case PARAM_MIX_PAN:
        case PARAM_MIX_SEND1:
        case PARAM_MIX_SEND2:

        /* Continuous Wave position, Prism and Stack controls. */
        case PARAM_WAVE_OSC1_POS:
        case PARAM_WAVE_OSC2_POS:
        case PARAM_PRISM_FINE:
        case PARAM_PRISM_COARSE:
        case PARAM_PRISM_FM:
        case PARAM_PRISM_TIMBRE:
        case PARAM_PRISM_MODULATION:
        case PARAM_PRISM_COLOR:
        case PARAM_PRISM_LEVEL:
        case PARAM_PRISM_OSC2_FINE:
        case PARAM_PRISM_OSC2_COARSE:
        case PARAM_PRISM_OSC2_FM:
        case PARAM_PRISM_OSC2_TIMBRE:
        case PARAM_PRISM_OSC2_MODULATION:
        case PARAM_PRISM_OSC2_COLOR:
        case PARAM_PRISM_OSC2_LEVEL:
        case PARAM_STACK_OSC1_LEVEL:
        case PARAM_STACK_OSC2_LEVEL:
        case PARAM_STACK_OSC3_LEVEL:
        case PARAM_STACK_NOISE_LEVEL:
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC1_TIMBRE:
        case PARAM_STACK_OSC1_COLOR:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC2_TIMBRE:
        case PARAM_STACK_OSC2_COLOR:
        case PARAM_STACK_OSC3_TUNE:
        case PARAM_STACK_OSC3_TIMBRE:
        case PARAM_STACK_OSC3_COLOR:
        case PARAM_STACK_OSC_DETUNE:

        /* Continuous delay/reverb controls.  Type/routing/time selectors stay
         * on their structural command path. */
        case PARAM_MIX_REVERB_WET:
        case PARAM_MIX_REVERB_ROOM_SIZE:
        case PARAM_MIX_REVERB_DAMPING:
        case PARAM_MIX_REVERB_WIDTH:
        case PARAM_MIX_REVERB_HPF:
        case PARAM_MIX_REVERB_LPF:
        case PARAM_MIX_DELAY_WIDTH:
        case PARAM_MIX_DELAY_FEEDBACK:
        case PARAM_MIX_DELAY_SPECTRAL_POSITION:
        case PARAM_MIX_DELAY_SPECTRAL_WIDTH:
        case PARAM_MIX_DELAY_FBW:
        case PARAM_MIX_DELAY_MOD:
        case PARAM_MIX_DELAY_MOD_RATE:
        case PARAM_MIX_DELAY_REV:
        case PARAM_MIX_DELAY_VOL:
            return 1U;
        default:
            return 0U;
    }
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
    live_parameter_audio_runtime_init();
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
        if (encoder_control_parameter_is_migrated_audio(parameter) == 0U)
        {
            /* Unmigrated, structural, sequence and navigation controls stay on
             * their existing UI/structural command paths. */
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
