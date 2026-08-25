#include "Storage/restore_audio_commit.h"

#include "Audio/audio_global_runtime.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Core/live_parameter_audio_queue.h"
#include "Core/synth_polyphony.h"
#include "Param/param_registry.h"
#include "Storage/persistent_pattern_restore_prepare.h"

uint8_t restore_audio_commit_validate(const restore_audio_plan_t *plan)
{
    if ((plan == NULL)
            || (plan->header.magic != RESTORE_PLAN_MAGIC)
            || (plan->header.abi_version != RESTORE_PLAN_ABI_VERSION)
            || (plan->header.header_bytes != sizeof(plan->header))
            || (plan->header.plan_bytes != sizeof(*plan))
            || (plan->header.binding_count != PERSIST_CONTROL_ENTITY_COUNT)
            || (plan->header.item_count > RESTORE_PLAN_MAX_ITEMS)
            || (plan->header.payload_crc32
                != persistent_pattern_restore_plan_crc32(plan)))
        return 0U;

    uint8_t previous_phase = RESTORE_PLAN_PHASE_BINDING;
    for (uint8_t entity = 0U; entity < plan->header.binding_count; ++entity)
    {
        const restore_plan_binding_t *const binding = &plan->bindings[entity];
        if ((binding->entity != entity)
                || (binding->family_key >= TRACK_RUNTIME_FAMILY_OTHER + 1U)
                || (binding->type_key >= TRACK_RUNTIME_TYPE_OTHER + 1U)
                || (binding->resource_index >= TRACK_RUNTIME_ENGINE_COUNT)
                || (binding->midi_channel < 1U) || (binding->midi_channel > 16U))
            return 0U;
        if (audio_note_engine_adapter_choose_engine(
                (track_runtime_family_t)binding->family_key,
                (track_runtime_type_t)binding->type_key)
                != (track_runtime_engine_t)binding->resource_index)
            return 0U;
    }
    for (uint16_t index = 0U; index < plan->header.item_count; ++index)
    {
        const restore_plan_item_t *const item = &plan->items[index];
        param_registry_prepared_value_t prepared;
        if ((item->param_id >= PARAM_COUNT)
                || ((item->entity >= PERSIST_CONTROL_ENTITY_COUNT)
                    && (item->entity != RESTORE_PLAN_ENTITY_NONE))
                || (item->phase <= RESTORE_PLAN_PHASE_BINDING)
                || (item->phase >= RESTORE_PLAN_PHASE_FINALIZE)
                || (item->phase < previous_phase)
                || (param_registry_prepare_value((param_id_t)item->param_id,
                                                 item->value, &prepared) == 0U)
                || (prepared.value != item->value))
            return 0U;
        previous_phase = item->phase;
    }
    return 1U;
}

uint8_t restore_audio_commit_apply(const restore_audio_plan_t *plan)
{
    if (restore_audio_commit_validate(plan) == 0U) return 0U;

    /* The boundary is destructive only after complete validation. Old live
     * commands cannot cross it, and old allocator reservations cannot affect
     * deterministic installation of the prepared bindings. */
    live_parameter_audio_queue_init();
    for (uint8_t entity = 0U; entity < PERSIST_CONTROL_ENTITY_COUNT; ++entity)
        (void)synth_polyphony_set_track_active(entity, 0U, 0U);
    audio_note_engine_adapter_init();

    for (uint8_t entity = 0U; entity < plan->header.binding_count; ++entity)
    {
        const restore_plan_binding_t *const binding = &plan->bindings[entity];
        const audio_note_engine_install_spec_t spec = {
            .entity_id = entity,
            .family = (uint8_t)binding->family_key,
            .type = (uint8_t)binding->type_key,
            .midi_channel_1_16 = binding->midi_channel,
            .midi_source = binding->midi_source,
            .flags = binding->flags,
            .voice_count = binding->voice_count,
            .voice_spread = (float)binding->voice_spread_q7 / 127.0f
        };
        audio_note_engine_adapter_install_prepared(&spec);
        const track_audio_runtime_ctx_t *const installed =
            audio_note_engine_adapter_audio_ctx(entity);
        if ((installed == NULL)
                || (installed->audio_binding.bind_state
                    == TRACK_RUNTIME_BIND_QUOTA_BLOCKED))
            return 0U;
    }
    for (uint16_t index = 0U; index < plan->header.item_count; ++index)
    {
        const restore_plan_item_t *const item = &plan->items[index];
        param_registry_prepared_value_t prepared = {
            .id = (param_id_t)item->param_id,
            .value = item->value
        };
        if (item->entity == RESTORE_PLAN_ENTITY_NONE)
        {
            float command_value = prepared.value;
            if (param_registry_prepare_global_audio_command(
                    prepared.id, prepared.value, &command_value) == 0U)
                return 0U;
            const uint8_t applied=(prepared.id == PARAM_MASTER_GAIN)
                ? audio_note_engine_adapter_set_master(command_value)
                : audio_global_runtime_apply((uint16_t)prepared.id,command_value);
            if (applied == 0U)
                return 0U;
        }
        else if (param_registry_apply_prepared_track_value_audio(
                    &prepared, item->entity) == 0U)
            return 0U;
    }
    audio_note_engine_adapter_audio_publish_snapshot();
    return 1U;
}
