#include "Param/param_control_backends.h"

#include "midi.h"

static float param_backend_control_clampf(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

uint8_t param_backend_is_midi_cc_id(param_id_t id)
{
    return ((id >= PARAM_MIDI_CC1_1) && (id <= PARAM_MIDI_CC3_4)) ? 1U : 0U;
}

uint8_t param_backend_midi_cc_number_from_id(param_id_t id)
{
    if (param_backend_is_midi_cc_id(id) == 0U) return 0U;
    return (uint8_t)(16U + (uint8_t)(id - PARAM_MIDI_CC1_1));
}

uint8_t param_backend_track_supports_midi_tone_ctx(const track_runtime_ctx_t *ctx)
{
    return (ctx == NULL) ? 0U
        : track_runtime_family_can_emit_external_midi(
            (track_runtime_family_t)ctx->family);
}

uint8_t param_backend_track_supports_midi_tone_descriptor(
    const track_runtime_descriptor_t *descriptor)
{
    return (descriptor == NULL) ? 0U
        : track_runtime_family_can_emit_external_midi(descriptor->family);
}

uint8_t param_backend_send_midi_cc(uint8_t track, param_id_t id, float value)
{
    if ((param_backend_is_midi_cc_id(id) == 0U)
            || (track_runtime_can_emit_external_midi(track) == 0U)) return 0U;
    midi_cc(MIDI_DEST_BOTH,
            track_runtime_get_midi_channel_zero_based(track),
            param_backend_midi_cc_number_from_id(id),
            (uint8_t)(param_backend_control_clampf(value, 0.0f, 127.0f) + 0.5f));
    return 1U;
}

uint8_t param_backend_apply_track_value_control(
    uint8_t track, param_id_t id, float value)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            || (param_backend_track_supports_midi_tone_ctx(ctx) == 0U))
        return 0U;

    if (param_backend_is_midi_cc_id(id) != 0U)
        return param_backend_send_midi_cc(track, id, value);
    return 0U;
}
