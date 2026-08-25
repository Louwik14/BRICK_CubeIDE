#include "Storage/persistent_pattern_restore_prepare.h"

#include <string.h>

#include "Audio/audio_note_engine_adapter.h"
#include "Audio/fx_modfx_global.h"
#include "Audio/mixer.h"
#include "Core/brick_build_config.h"
#include "Core/live_parameter_migration.h"
#include "Core/synth_polyphony.h"
#include "Core/track_runtime.h"
#include "Param/param_registry.h"
#include "Storage/persistent_key_catalog.h"
#include "Storage/persistent_pattern_control.h"
#include "Storage/restore_transaction.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "stm32h7xx.h"

static uint32_t crc32_bytes(const uint8_t *data, size_t bytes)
{
    uint32_t crc = 0xFFFFFFFFUL;
    while (bytes-- != 0U)
    {
        crc ^= *data++;
        for (uint8_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ ((0U - (crc & 1U)) & 0xEDB88320UL);
    }
    return ~crc;
}

uint32_t persistent_pattern_restore_plan_crc32(const restore_audio_plan_t *plan)
{
    if (plan == NULL) return 0U;
    const size_t bytes = sizeof(plan->bindings)
        + ((size_t)plan->header.item_count * sizeof(plan->items[0]));
    return crc32_bytes((const uint8_t *)plan->bindings, bytes);
}

static float entity_value(const persist_control_entity_t *entity,
                          param_id_t wanted, float fallback)
{
    for (uint16_t i = 0U; i < entity->parameter_count; ++i)
    {
        param_id_t id;
        if ((persist_key_param_from_disk(entity->parameters[i].key, &id) != 0U)
                && (id == wanted))
            return entity->parameters[i].value.f32;
    }
    return fallback;
}

persist_codec_result_t persistent_pattern_restore_prepare(
    const persist_control_pattern_t *pattern, restore_audio_plan_t *out_plan)
{
    if ((pattern == NULL) || (out_plan == NULL)) return PERSIST_CODEC_INVALID_ARGUMENT;
    persist_codec_result_t result = persistent_pattern_control_validate(pattern);
    if (result != PERSIST_CODEC_OK) return result;

    memset(out_plan, 0, sizeof(*out_plan));
    restore_audio_plan_t *const candidate = out_plan;
    uint8_t looper_count = 0U;
    uint8_t voice_total = 0U;
    uint32_t mix_mask = 0U;
    const uint8_t group_active = (pattern->entities[PERSIST_CONTROL_GROUP_MASTER_ID].type
                                   == PERSIST_TYPE_GROUP) ? 1U : 0U;

    for (uint8_t e = 0U; e < PERSIST_CONTROL_ENTITY_COUNT; ++e)
    {
        const persist_control_entity_t *const entity = &pattern->entities[e];
        ui_track_family_t ui_family;
        ui_track_type_t ui_type;
        ui_track_midi_source_t midi_source;
        if ((persist_key_family_from_disk(entity->family, &ui_family) == 0U)
                || (persist_key_type_from_disk(entity->type, &ui_type) == 0U)
                || (persist_key_midi_source_from_disk(entity->midi_source_key, &midi_source) == 0U))
            return PERSIST_CODEC_UNKNOWN_KEY;
        const track_runtime_family_t family = track_runtime_family_from_ui(ui_family);
        const track_runtime_type_t type = track_runtime_type_from_ui(ui_type);
        const track_runtime_engine_t engine = audio_note_engine_adapter_choose_engine(family, type);
        restore_plan_binding_t *const binding = &candidate->bindings[e];
        binding->entity = e;
        binding->family_key = (uint32_t)family;
        binding->type_key = (uint32_t)type;
        binding->input_key = entity->input_key;
        binding->midi_channel = entity->midi_channel;
        binding->midi_source = (uint8_t)midi_source;
        binding->flags = track_runtime_compute_flags(family, type);
        binding->resource_index = (uint16_t)engine;
        binding->voice_count = 1U;
        if (group_active != 0U)
        {
            if (e == PERSIST_CONTROL_GROUP_MASTER_ID) binding->flags |= AUDIO_RUNTIME_FLAG_GROUP_MASTER;
            else if (e >= PERSIST_CONTROL_FIRST_GROUP_CHILD_ID) binding->flags |= AUDIO_RUNTIME_FLAG_GROUP_CHILD;
        }
        if (engine == TRACK_RUNTIME_ENGINE_LOOPER)
        {
            if (++looper_count > BRICK6_LOOPER_GLOBAL_CAP) return PERSIST_CODEC_INVALID_ENTITY;
        }
        if ((family == TRACK_RUNTIME_FAMILY_SYNTH) || (family == TRACK_RUNTIME_FAMILY_DRUM))
        {
            if (e >= SYNTH_POLYPHONY_TRACK_CAPACITY) return PERSIST_CODEC_INVALID_ENTITY;
            float voices = entity_value(entity, PARAM_CFG_POLY_VOICES,
                                        param_registry[PARAM_CFG_POLY_VOICES].default_value);
            if (family == TRACK_RUNTIME_FAMILY_DRUM) voices = 1.0f;
            param_registry_prepared_value_t prepared;
            if (param_registry_prepare_value(PARAM_CFG_POLY_VOICES, voices, &prepared) == 0U)
                return PERSIST_CODEC_INVALID_ENTITY;
            binding->voice_count = (uint8_t)(prepared.value + 0.5f);
            if ((uint16_t)voice_total + binding->voice_count > SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                return PERSIST_CODEC_INVALID_ENTITY;
            voice_total = (uint8_t)(voice_total + binding->voice_count);
            float spread = entity_value(entity, PARAM_CFG_POLY_SPREAD,
                                        param_registry[PARAM_CFG_POLY_SPREAD].default_value);
            if (param_registry_prepare_value(PARAM_CFG_POLY_SPREAD, spread, &prepared) == 0U)
                return PERSIST_CODEC_INVALID_ENTITY;
            binding->voice_spread_q7 = (uint8_t)(prepared.value * 127.0f + 0.5f);
        }
        uint8_t mix = 0xFFU;
        if ((binding->flags & AUDIO_RUNTIME_FLAG_GROUP_MASTER) != 0U) mix = MIXER_GROUP_BUS_TRACK;
        else if ((family != TRACK_RUNTIME_FAMILY_OFF) && (family != TRACK_RUNTIME_FAMILY_MIDI)
                 && ((binding->flags & AUDIO_RUNTIME_FLAG_GROUP_CHILD) == 0U)) mix = e;
        if (mix != 0xFFU)
        {
            if ((mix >= MIXER_MAX_TRACKS) || ((mix_mask & (1UL << mix)) != 0U))
                return PERSIST_CODEC_INVALID_ENTITY;
            mix_mask |= 1UL << mix;
        }
    }
    uint8_t has_modfx_precise=0U,has_modfx_legacy=0U;
    uint8_t modfx_model=0U;float modfx_packed[4]={0.0f,0.0f,0.0f,0.0f};
    for (uint16_t i=0U;i<pattern->globals.parameter_count;++i)
    {
        param_id_t id;
        if (persist_key_param_from_disk(pattern->globals.parameters[i].key,&id)==0U)
            return PERSIST_CODEC_UNKNOWN_KEY;
        const float value=pattern->globals.parameters[i].value.f32;
        if (id==PARAM_MODFX_MODEL) modfx_model=(uint8_t)(value+0.5f);
        if (id>=PARAM_MODFX_RATE&&id<=PARAM_MODFX_WIDTH) has_modfx_precise=1U;
        if (id>=PARAM_MODFX_BANK_DAISY_STEREO_AB&&id<=PARAM_MODFX_BANK_JUNOLOGUE_CD)
        {
            has_modfx_legacy=1U;
            if (id>=PARAM_MODFX_BANK_DAISY_STEREO_AB
                    && id<=PARAM_MODFX_BANK_DAISY_STEREO_G)
                modfx_packed[id-PARAM_MODFX_BANK_DAISY_STEREO_AB]=value;
            else if (id>=PARAM_MODFX_BANK_JUNOLOGUE_AB
                    && id<=PARAM_MODFX_BANK_JUNOLOGUE_CD)
                modfx_packed[id-PARAM_MODFX_BANK_JUNOLOGUE_AB]=value;
        }
    }
    for (uint16_t i = 0U; i < pattern->globals.parameter_count; ++i)
    {
        param_id_t id;
        if ((persist_key_param_from_disk(pattern->globals.parameters[i].key, &id) == 0U)
                || ((live_parameter_is_audio_owned(id) == 0U)
                    && (id != PARAM_MASTER_GAIN))) continue;
        if (candidate->header.item_count >= RESTORE_PLAN_MAX_ITEMS)
            return PERSIST_CODEC_CAPACITY_EXCEEDED;
        param_registry_prepared_value_t prepared;
        if (param_registry_prepare_value(id,
                pattern->globals.parameters[i].value.f32, &prepared) == 0U)
            return PERSIST_CODEC_UNKNOWN_KEY;
        restore_plan_item_t *const item =
            &candidate->items[candidate->header.item_count++];
        item->param_id = (uint16_t)id;
        item->entity = RESTORE_PLAN_ENTITY_NONE;
        item->phase = RESTORE_PLAN_PHASE_GLOBAL;
        item->value = prepared.value;
    }
    if ((has_modfx_legacy != 0U) && (has_modfx_precise == 0U))
    {
        param_registry_prepared_value_t expanded[8];uint8_t count=0U;
        if (param_registry_prepare_legacy_modfx_bank_values(
                modfx_model,modfx_packed,expanded,&count) == 0U)
            return PERSIST_CODEC_UNKNOWN_KEY;
        for (uint8_t i=0U;i<count;++i)
        {
            if (candidate->header.item_count >= RESTORE_PLAN_MAX_ITEMS)
                return PERSIST_CODEC_CAPACITY_EXCEEDED;
            restore_plan_item_t *const item=
                &candidate->items[candidate->header.item_count++];
            item->param_id=(uint16_t)expanded[i].id;
            item->entity=RESTORE_PLAN_ENTITY_NONE;
            item->phase=RESTORE_PLAN_PHASE_GLOBAL;
            item->value=expanded[i].value;
        }
    }

    for (uint8_t e = 0U; e < PERSIST_CONTROL_ENTITY_COUNT; ++e)
    {
        const persist_control_entity_t *const entity = &pattern->entities[e];
        for (uint16_t i = 0U; i < entity->parameter_count; ++i)
        {
            param_id_t id;
            if ((persist_key_param_from_disk(entity->parameters[i].key, &id) == 0U)
                    || (param_registry_track_value_is_audio_command(id,e) == 0U)
                    || (id == PARAM_CFG_POLY_VOICES)
                    || (id == PARAM_CFG_POLY_SPREAD)) continue;
            if (candidate->header.item_count >= RESTORE_PLAN_MAX_ITEMS)
                return PERSIST_CODEC_CAPACITY_EXCEEDED;
            param_registry_prepared_value_t prepared;
            if (param_registry_prepare_value(id, entity->parameters[i].value.f32, &prepared) == 0U)
                return PERSIST_CODEC_UNKNOWN_KEY;
            restore_plan_item_t *const item = &candidate->items[candidate->header.item_count++];
            item->param_id = (uint16_t)id;
            item->entity = e;
            item->phase = (uint8_t)(param_registry_is_audio_fx_param(id)
                ? RESTORE_PLAN_PHASE_DEPENDENT : RESTORE_PLAN_PHASE_TRACK);
            if ((id == PARAM_AUDIO_FX_MODEL) || (id == PARAM_AUDIO_FX_B_MODEL))
                item->phase = RESTORE_PLAN_PHASE_MODEL;
            item->value = prepared.value;
        }
    }
    /* Stable phase ordering makes dependency order explicit while preserving
     * deterministic entity/DTO order within a phase. */
    for (uint16_t i = 1U; i < candidate->header.item_count; ++i)
    {
        const restore_plan_item_t item = candidate->items[i];
        uint16_t position = i;
        while ((position > 0U)
                && (candidate->items[position - 1U].phase > item.phase))
        {
            candidate->items[position] = candidate->items[position - 1U];
            --position;
        }
        candidate->items[position] = item;
    }
    candidate->header.magic = RESTORE_PLAN_MAGIC;
    candidate->header.abi_version = RESTORE_PLAN_ABI_VERSION;
    candidate->header.header_bytes = sizeof(candidate->header);
    candidate->header.plan_bytes = sizeof(*candidate);
    candidate->header.binding_count = PERSIST_CONTROL_ENTITY_COUNT;
    candidate->header.payload_crc32 = persistent_pattern_restore_plan_crc32(candidate);
    return PERSIST_CODEC_OK;
}

persist_codec_result_t persistent_pattern_restore_execute(
    const persist_control_pattern_t *pattern,uint8_t resume_transport)
{
    persist_codec_result_t result=
        persistent_pattern_restore_prepare(pattern,&g_restore_audio_plan);
    if (result != PERSIST_CODEC_OK) return result;
    const uint8_t was_running=seq_runtime_is_running();
    seq_runtime_stop();
    restore_result_t audio_result=RESTORE_RESULT_NONE;
    uint32_t request_seq=0U;
    if (restore_transaction_control_publish(&request_seq) == 0U)
        return PERSIST_CODEC_INVALID_ENTITY;
    while (restore_transaction_control_completed(request_seq, &audio_result) == 0U)
    {
        /* AUDIO IRQ/background is the sole COMMIT owner on both H743/H747. */
        __DMB();
    }
    if (audio_result != RESTORE_RESULT_COMPLETE)
        return PERSIST_CODEC_INVALID_ENTITY;
    result=persistent_pattern_control_install_restored(pattern,0U);
    if ((result == PERSIST_CODEC_OK) && (resume_transport != 0U)
            && (was_running != 0U))
        seq_runtime_start();
    return result;
}
