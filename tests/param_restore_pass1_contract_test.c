#include <assert.h>
#include <stdint.h>

#include "Audio/audio_note_engine_adapter.h"
#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"

const param_desc_t param_registry[PARAM_COUNT] = {
    [PARAM_MIX_LEVEL] = {
        .id = PARAM_MIX_LEVEL,
        .min = -1.0f,
        .max = 1.0f
    }
};

static uint32_t encode_float(float input)
{
    union { float f; uint32_t u; } value = { .f = input };
    return value.u;
}

int main(void)
{
    param_registry_prepared_value_t value = {0};
    assert(param_registry_prepare_value(PARAM_MIX_LEVEL, 2.0f, &value) == 1U);
    assert(value.id == PARAM_MIX_LEVEL);
    assert(value.value == 1.0f);
    assert(param_registry_prepare_value(PARAM_MIX_LEVEL, -2.0f, &value) == 1U);
    assert(value.value == -1.0f);
    assert(param_registry_prepare_value(PARAM_RESERVED_000, 0.0f, &value) == 0U);
    assert(param_registry_prepare_value(PARAM_MIX_LEVEL, 0.0f, NULL) == 0U);

    param_registry_control_shadow_init();
    param_registry_control_shadow_set_pending(2U, PARAM_MIX_LEVEL, 0.25f);
    param_registry_runtime_ui_value_t pending_before = {0};
    assert(param_registry_control_shadow_ui_value_get(
        2U, PARAM_MIX_LEVEL, &pending_before) == 1U);
    assert(pending_before.base_value == 0.25f);
    assert(pending_before.flags ==
        (PARAM_REGISTRY_RUNTIME_UI_VALUE_VALID
         | PARAM_REGISTRY_RUNTIME_AUDIO_PENDING));
    param_registry_control_shadow_mark_published(2U, PARAM_MIX_LEVEL);
    param_registry_control_shadow_set(2U, PARAM_MIX_LEVEL, 0.5f);
    param_registry_control_shadow_mark_pending(2U, PARAM_MIX_LEVEL);
    param_registry_runtime_ui_value_t pending_after = {0};
    assert(param_registry_control_shadow_ui_value_get(
        2U, PARAM_MIX_LEVEL, &pending_after) == 1U);
    assert(pending_after.base_value == 0.5f);
    assert(pending_after.flags == pending_before.flags);

    const control_audio_event_t event = {
        .source_generation = encode_float(0.375f),
        .param_id = 6U,
        .param_value = 2U,
        .entity_id = 3U,
        .note = (uint8_t)TRACK_RUNTIME_TYPE_WAVE,
        .velocity = 9U,
        .provenance = (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH,
        .flags = AUDIO_RUNTIME_FLAG_CAN_FILTER
    };
    audio_note_engine_install_spec_t spec = {0};
    assert(audio_note_engine_adapter_prepare_install_spec(&event, &spec) == 1U);
    assert(spec.entity_id == 3U);
    assert(spec.family == TRACK_RUNTIME_FAMILY_SYNTH);
    assert(spec.type == TRACK_RUNTIME_TYPE_WAVE);
    assert(spec.midi_channel_1_16 == 9U);
    assert(spec.midi_source == 2U);
    assert(spec.flags == AUDIO_RUNTIME_FLAG_CAN_FILTER);
    assert(spec.voice_count == 6U);
    assert(spec.voice_spread == 0.375f);
    assert(audio_note_engine_adapter_prepare_install_spec(NULL, &spec) == 0U);
    assert(audio_note_engine_adapter_prepare_install_spec(&event, NULL) == 0U);
    return 0;
}
