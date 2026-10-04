#include "Storage/patch_preview.h"

#include <string.h>

#include "ControlRT/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"
#include "ControlRT/patch_preview_contract.h"
#include "Mod/mod_matrix.h"
#include "Mod/mod_lfo_types.h"
#include "Platform/memory_layout.h"
#include "Storage/persistent_control_codec.h"
#include "Storage/persistent_key_catalog.h"
#include "Track/audio_fx_control_state.h"
#include "Track/fm_control_state.h"
#include "Track/polyphony_control.h"
#include "Track/tone_param_codec.h"
#include "Track/track_runtime.h"
#include "Track/vca_control_state.h"
#include "Param/param_filter.h"
#include "Track/track_types.h"
#include "stm32h7xx.h"

typedef struct
{
    persist_control_patch_t patch;
    uint32_t publication_generation;
    uint8_t prepared;
    uint8_t note_active;
    uint8_t note;
} patch_preview_control_t;

STORAGE_STATE_SDRAM static patch_preview_control_t g_patch_preview;

static uint8_t patch_preview_validate(const persist_control_patch_t *patch)
{
    if ((patch == NULL)
            || (persist_codec_validate_patch(patch) != PERSIST_CODEC_OK))
        return 0U;

    track_family_t family;
    track_type_t type;
    polyphony_control_state_t polyphony;
    if ((persist_key_family_from_disk(patch->family, &family) == 0U)
            || (persist_key_type_from_disk(patch->type, &type) == 0U))
        return 0U;
    const uint8_t supported = (uint8_t)((type == TRACK_TYPE_FM)
        || (type == TRACK_TYPE_PRISM) || (type == TRACK_TYPE_STACK)
        || (type == TRACK_TYPE_TB303) || (type == TRACK_TYPE_ACID));
    const uint8_t expect_fm = (uint8_t)(type == TRACK_TYPE_FM);
    return (uint8_t)(
        (family == TRACK_FAMILY_SYNTH)
        && (supported != 0U)
        && (patch->asset_count == 0U)
        && (patch->fm_present == expect_fm)
        && (patch->tone_present == (uint8_t)(expect_fm == 0U))
        && (expect_fm
            ? (fm_control_state_validate(&patch->fm) != 0U)
            : (tone_program_control_validate(&patch->tone,
                track_runtime_type_from_ui(type)) != 0U))
        && (param_filter_control_validate(&patch->filter) != 0U)
        && (vca_control_state_validate(&patch->vca) != 0U)
        && (audio_fx_control_state_validate(&patch->audio_fx) != 0U)
        && (polyphony_control_prepare(&patch->polyphony, &polyphony) != 0U));
}

static patch_preview_engine_t patch_preview_engine_from_type(track_type_t type)
{
    switch (type)
    {
        case TRACK_TYPE_FM: return PATCH_PREVIEW_ENGINE_FM;
        case TRACK_TYPE_PRISM: return PATCH_PREVIEW_ENGINE_PRISM;
        case TRACK_TYPE_STACK: return PATCH_PREVIEW_ENGINE_STACK;
        case TRACK_TYPE_TB303: return PATCH_PREVIEW_ENGINE_TB303;
        case TRACK_TYPE_ACID: return PATCH_PREVIEW_ENGINE_ACID;
        default: return PATCH_PREVIEW_ENGINE_NONE;
    }
}

static uint8_t patch_preview_patch_param_get(
    const persist_control_patch_t *patch, param_id_t parameter,
    float *out_value)
{
    if ((patch == NULL) || (out_value == NULL)) return 0U;
    switch (parameter)
    {
        case PARAM_FILTER_MORPH: *out_value = patch->filter.morph; return 1U;
        case PARAM_FILTER_CUTOFF: *out_value = patch->filter.cutoff; return 1U;
        case PARAM_FILTER_RESONANCE: *out_value = patch->filter.resonance; return 1U;
        case PARAM_FILTER_EG_AMT: *out_value = patch->filter.eg_amount; return 1U;
        case PARAM_FILTER_ATTACK: *out_value = patch->filter.attack; return 1U;
        case PARAM_FILTER_DECAY: *out_value = patch->filter.decay; return 1U;
        case PARAM_FILTER_SUSTAIN: *out_value = patch->filter.sustain; return 1U;
        case PARAM_FILTER_RELEASE: *out_value = patch->filter.release; return 1U;
        case PARAM_FILTER_KEYTRK: *out_value = patch->filter.keytrack; return 1U;
        case PARAM_VCA_ATTACK: *out_value = patch->vca.attack; return 1U;
        case PARAM_VCA_DECAY: *out_value = patch->vca.decay; return 1U;
        case PARAM_VCA_SUSTAIN: *out_value = patch->vca.sustain; return 1U;
        case PARAM_VCA_RELEASE: *out_value = patch->vca.release; return 1U;
        default: break;
    }
    return (patch->fm_present != 0U)
        ? fm_control_state_get_public_param_from(&patch->fm, parameter,
                                                 out_value)
        : tone_program_control_get_from(&patch->tone, parameter, out_value);
}

static uint8_t patch_preview_mod_origin(
    const persist_control_modulation_t *modulation, uint8_t *out_origin)
{
    uint8_t fallback = BRICK_ENTITY_INVALID_ID;
    if ((modulation == NULL) || (out_origin == NULL)) return 0U;
    for (uint8_t route = 0U; route < PERSIST_CONTROL_MOD_ROUTE_COUNT; ++route)
    {
        const persist_control_mod_route_t *const item =
            &modulation->routes[route];
        if (item->destination_parameter == PERSIST_CONTROL_KEY_NONE)
        {
            *out_origin = item->destination_entity;
            return (*out_origin < BRICK_ENTITY_CAPACITY) ? 1U : 0U;
        }
        if (item->enabled == 0U) continue;
        if (fallback == BRICK_ENTITY_INVALID_ID)
            fallback = item->destination_entity;
        else if (fallback != item->destination_entity)
            return 0U;
    }
    if (fallback >= BRICK_ENTITY_CAPACITY) return 0U;
    *out_origin = fallback;
    return 1U;
}

static uint8_t patch_preview_project_modulation(
    const persist_control_patch_t *patch, patch_preview_modulation_t *out)
{
    memset(out, 0, sizeof(*out));
    if (patch->modulation_present == 0U) return 1U;
    const persist_control_modulation_t *const source = &patch->modulation;
    uint8_t origin = BRICK_ENTITY_INVALID_ID;
    if (patch_preview_mod_origin(source, &origin) == 0U) return 0U;
    for (uint8_t lfo = 0U; lfo < PERSIST_CONTROL_MOD_LFO_COUNT; ++lfo)
    {
        mod_lfo_shape_t shape;
        mod_lfo_trig_mode_t trigger;
        if ((persist_key_lfo_shape_from_disk(source->lfos[lfo].shape_key,
                                             &shape) == 0U)
                || (persist_key_lfo_trigger_from_disk(
                    source->lfos[lfo].trigger_key, &trigger) == 0U))
            return 0U;
        if (trigger >= MOD_LFO_TRIG_POLY_TRIG)
            trigger = (mod_lfo_trig_mode_t)(trigger
                - MOD_LFO_TRIG_POLY_TRIG + MOD_LFO_TRIG_TRIG);
        out->lfo[lfo] = (patch_preview_mod_lfo_t){
            .rate = source->lfos[lfo].rate,
            .phase_offset = source->lfos[lfo].phase_offset,
            .shape = (uint8_t)shape,
            .trigger = (uint8_t)trigger
        };
    }
    out->env_attack = source->envelope.attack;
    out->env_decay = source->envelope.decay;
    out->env_sustain = source->envelope.sustain;
    out->env_release = source->envelope.release;
    out->env_retrigger_hard = source->envelope.retrigger_hard;
    for (uint8_t op = 0U; op < 2U; ++op)
    {
        if ((persist_key_mod_source_from_disk(
                source->multi[op].source_a_key,
                &out->multi_source[op][0]) == 0U)
                || (persist_key_mod_source_from_disk(
                    source->multi[op].source_b_key,
                    &out->multi_source[op][1]) == 0U)
                || (persist_key_mod_source_from_disk(
                    source->slew[op].source_key,
                    &out->slew_source[op]) == 0U)) return 0U;
        out->slew_amount[op] = source->slew[op].amount;
    }
    for (uint8_t route = 0U; route < PERSIST_CONTROL_MOD_ROUTE_COUNT; ++route)
    {
        const persist_control_mod_route_t *const item = &source->routes[route];
        patch_preview_mod_route_t *const projected = &out->route[route];
        if (persist_key_mod_source_from_disk(item->source_key,
                                             &projected->source) == 0U)
            return 0U;
        projected->depth = item->depth;
        if ((item->enabled == 0U)
                || (item->destination_parameter == PERSIST_CONTROL_KEY_NONE)
                || (item->destination_entity != origin)) continue;
        uint8_t destination_entity;
        if ((persist_key_mod_destination_from_disk(item->destination_entity,
                item->destination_parameter, 0U, &destination_entity,
                &projected->destination) == 0U)
                || (destination_entity != origin)) return 0U;
        projected->base_valid = patch_preview_patch_param_get(patch,
            projected->destination, &projected->base_value);
        projected->enabled = 1U;
    }
    return 1U;
}

static uint8_t patch_preview_publish(patch_preview_command_t command,
                                     uint32_t value)
{
    return control_rt_publish_param_now((uint8_t)command,
        CONTROL_AUDIO_PARAM_PATCH_PREVIEW, value, 0U);
}

void patch_preview_init(void)
{
    memset(&g_patch_preview, 0, sizeof(g_patch_preview));
    memset(g_patch_preview_publication, 0,
           sizeof(g_patch_preview_publication));
    g_patch_preview_audio_consumed_generation = 0U;
    __DMB();
}

uint8_t patch_preview_prepare(const persist_control_patch_t *patch)
{
    if (patch_preview_validate(patch) == 0U) return 0U;
    track_type_t type;
    if (persist_key_type_from_disk(patch->type, &type) == 0U) return 0U;

    const uint32_t consumed = g_patch_preview_audio_consumed_generation;
    __DMB();
    uint32_t generation = g_patch_preview.publication_generation + 1U;
    if (generation == 0U) generation = 1U;
    if ((uint32_t)(generation - consumed)
            > PATCH_PREVIEW_PUBLICATION_SLOT_COUNT)
        return 0U;

    if ((g_patch_preview.prepared != 0U)
            && (patch_preview_publish(PATCH_PREVIEW_COMMAND_STOP, 0U) == 0U))
        return 0U;

    const uint32_t slot = generation % PATCH_PREVIEW_PUBLICATION_SLOT_COUNT;
    patch_preview_publication_t *const publication =
        &g_patch_preview_publication[slot];
    memset(publication, 0, sizeof(*publication));
    publication->engine = patch_preview_engine_from_type(type);
    publication->fm_base = patch->fm.base;
    publication->fm_macros = patch->fm.macros;
    if (type != TRACK_TYPE_FM)
    {
        const track_runtime_type_t runtime_type = track_runtime_type_from_ui(type);
        const uint8_t count = tone_param_codec_count(runtime_type);
        if ((count == 0U) || (count > PATCH_PREVIEW_ENGINE_PARAM_CAPACITY))
            return 0U;
        for (uint8_t slot_index = 0U; slot_index < count; ++slot_index)
        {
            patch_preview_engine_param_t *const item =
                &publication->engine_param[slot_index];
            if ((tone_param_codec_slot_to_param(runtime_type, slot_index,
                                                &item->parameter) == 0U)
                    || (tone_program_control_get_from(&patch->tone,
                        item->parameter, &item->value) == 0U)) return 0U;
        }
        publication->engine_param_count = count;
    }
    publication->filter = (patch_preview_filter_t){
        patch->filter.morph, patch->filter.cutoff, patch->filter.resonance,
        patch->filter.eg_amount, patch->filter.attack, patch->filter.decay,
        patch->filter.sustain, patch->filter.release, patch->filter.keytrack,
        patch->filter.env_reset, patch->filter.env_delay,
        patch->filter.retrigger
    };
    publication->vca = (patch_preview_vca_t){
        patch->vca.attack, patch->vca.decay, patch->vca.sustain,
        patch->vca.release, patch->vca.filter_mode, patch->vca.retrigger
    };
    publication->modulation_present = patch->modulation_present;
    if (patch_preview_project_modulation(patch,
            &publication->modulation) == 0U) return 0U;
    publication->generation = generation;
    __DMB();

    if (patch_preview_publish(PATCH_PREVIEW_COMMAND_PREPARE, generation) == 0U)
    {
        publication->generation = 0U;
        return 0U;
    }

    g_patch_preview.patch = *patch;
    g_patch_preview.publication_generation = generation;
    g_patch_preview.prepared = 1U;
    g_patch_preview.note_active = 0U;
    return 1U;
}

uint8_t patch_preview_note_on(uint8_t note, uint8_t velocity)
{
    if ((g_patch_preview.prepared == 0U) || (note > 127U)
            || (velocity == 0U) || (velocity > 127U))
        return 0U;
    if (g_patch_preview.note_active != 0U)
    {
        if (patch_preview_note_off() == 0U) return 0U;
    }
    const uint32_t value = (uint32_t)note | ((uint32_t)velocity << 8U);
    if (patch_preview_publish(PATCH_PREVIEW_COMMAND_NOTE_ON, value) == 0U)
        return 0U;
    g_patch_preview.note = note;
    g_patch_preview.note_active = 1U;
    return 1U;
}

uint8_t patch_preview_note_off(void)
{
    if ((g_patch_preview.prepared == 0U)
            || (g_patch_preview.note_active == 0U))
        return 1U;
    if (patch_preview_publish(PATCH_PREVIEW_COMMAND_NOTE_OFF,
                              g_patch_preview.note) == 0U)
        return 0U;
    g_patch_preview.note_active = 0U;
    return 1U;
}

uint8_t patch_preview_stop(void)
{
    if (g_patch_preview.prepared == 0U) return 1U;
    if (patch_preview_publish(PATCH_PREVIEW_COMMAND_STOP, 0U) == 0U)
        return 0U;
    memset(&g_patch_preview.patch, 0, sizeof(g_patch_preview.patch));
    g_patch_preview.prepared = 0U;
    g_patch_preview.note_active = 0U;
    g_patch_preview.note = 0U;
    return 1U;
}

uint8_t patch_preview_is_prepared(void)
{
    return g_patch_preview.prepared;
}

const persist_control_patch_t *patch_preview_get_staging(void)
{
    return (g_patch_preview.prepared != 0U) ? &g_patch_preview.patch : NULL;
}
