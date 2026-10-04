#include "Storage/patch_preview.h"

#include <string.h>

#include "ControlRT/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"
#include "ControlRT/patch_preview_contract.h"
#include "Platform/memory_layout.h"
#include "Storage/persistent_control_codec.h"
#include "Storage/persistent_key_catalog.h"
#include "Track/audio_fx_control_state.h"
#include "Track/fm_control_state.h"
#include "Track/polyphony_control.h"
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

static uint8_t patch_preview_validate_fm(const persist_control_patch_t *patch)
{
    if ((patch == NULL)
            || (persist_codec_validate_patch(patch) != PERSIST_CODEC_OK))
        return 0U;

    track_family_t family;
    track_type_t type;
    polyphony_control_state_t polyphony;
    return (uint8_t)(
        (persist_key_family_from_disk(patch->family, &family) != 0U)
        && (persist_key_type_from_disk(patch->type, &type) != 0U)
        && (family == TRACK_FAMILY_SYNTH)
        && (type == TRACK_TYPE_FM)
        && (patch->asset_count == 0U)
        && (patch->fm_present == 1U)
        && (patch->tone_present == 0U)
        && (fm_control_state_validate(&patch->fm) != 0U)
        && (param_filter_control_validate(&patch->filter) != 0U)
        && (vca_control_state_validate(&patch->vca) != 0U)
        && (audio_fx_control_state_validate(&patch->audio_fx) != 0U)
        && (polyphony_control_prepare(&patch->polyphony, &polyphony) != 0U));
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
    memset(g_patch_preview_fm_publication, 0,
           sizeof(g_patch_preview_fm_publication));
    g_patch_preview_audio_consumed_generation = 0U;
    __DMB();
}

uint8_t patch_preview_prepare(const persist_control_patch_t *patch)
{
    if (patch_preview_validate_fm(patch) == 0U) return 0U;

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
    patch_preview_fm_publication_t *const publication =
        &g_patch_preview_fm_publication[slot];
    publication->base = patch->fm.base;
    publication->macros = patch->fm.macros;
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
