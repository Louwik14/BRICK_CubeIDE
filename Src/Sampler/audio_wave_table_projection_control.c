#include "Sampler/audio_wave_table_projection_control.h"
#include "IPC/audio_wavetable_registry_contract.h"

#include <string.h>

#include "IPC/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"
#include "Param/param_registry.h"
#include "Platform/intercore_cache.h"
#include "Platform/cache_maintenance.h"
#include "IPC/shared_memory_ref_control.h"
#include "Platform/memory_layout.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Storage/project_control.h"
#include "Track/track_runtime.h"
#include "stm32h7xx.h"

#define AUDIO_WAVE_TABLE_SELECTION_COUNT \
    (BRICK_ENTITY_CAPACITY * AUDIO_WAVETABLE_OSC_COUNT)

static audio_wave_table_selection_t
    g_control_selection[AUDIO_WAVE_TABLE_SELECTION_COUNT];
void audio_wave_table_projection_init(void)
{
    memset(g_control_selection, 0, sizeof(g_control_selection));
    for (uint8_t i = 0U; i < AUDIO_WAVE_TABLE_SELECTION_COUNT; ++i)
        g_control_selection[i].wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    memset(g_audio_wavetable_registry, 0, sizeof(g_audio_wavetable_registry));
    intercore_cache_publish(g_audio_wavetable_registry,
                            sizeof(g_audio_wavetable_registry));
}

uint8_t audio_wave_table_projection_build_descriptor(
    uint16_t wavetable_slot, const wavetable_slot_t *slot,
    audio_wavetable_descriptor_t *out)
{
    if ((slot == NULL) || (out == NULL)
        || (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || (slot->state != WAVETABLE_SLOT_READY)) return 0U;
    memset(out, 0, sizeof(*out));
    out->generation = slot->generation;
    out->frame_count = slot->frame_count;
    out->frame_sample_count = slot->frame_sample_count;
    out->wavetable_slot = wavetable_slot;
    out->global_slot = slot->global_slot;
    out->band_count = slot->mipmap.band_count;
    if (shared_memory_ref_make_page_pool(slot->first_page_slot, 0U,
                                         slot->data_bytes,
                                         &out->base_data) == 0U)
        return 0U;
    dcache_clean_by_addr_aligned(slot->data, slot->data_bytes);
    uint32_t mip_offset = 0U;
    for (uint16_t i = 0U; i < out->band_count; ++i)
    {
        const wavetable_mipmap_band_t *const src = &slot->mipmap.bands[i];
        audio_wavetable_band_t *const dst = &out->bands[i];
        const uint32_t bytes = src->sample_count * sizeof(float);
        dst->max_phase_increment = src->max_phase_increment;
        dst->cycle_sample_count = src->cycle_sample_count;
        dst->sample_count = src->sample_count;
        dst->cycle_magnitude = src->cycle_magnitude;
        dst->flags = src->flags;
        if (shared_memory_ref_make_page_pool(slot->mipmap.first_page_slot,
                                             mip_offset, bytes,
                                             &dst->data) == 0U)
            return 0U;
        mip_offset += bytes;
    }
    dcache_clean_by_addr_aligned(slot->mipmap.data, slot->mipmap.data_bytes);
    return 1U;
}

uint8_t audio_wave_table_projection_install_descriptor(
    const audio_wavetable_descriptor_t *descriptor)
{
    if ((descriptor == NULL)
        || (descriptor->wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || (descriptor->generation == 0U)
        || (descriptor->band_count > WAVETABLE_MIPMAP_MAX_BANDS)) return 0U;
    audio_wavetable_registry_slot_t *const dst =
        &g_audio_wavetable_registry[descriptor->wavetable_slot];
    const uint32_t inactive = dst->active_snapshot ^ 1U;
    audio_wavetable_registry_snapshot_t *const next =
        &dst->snapshots[inactive];
    next->ready = 0U;
    next->descriptor = *descriptor;
    next->ready = 1U;
    intercore_cache_publish(next, sizeof(*next));
    dst->active_snapshot = inactive;
    intercore_cache_publish((const void *)&dst->active_snapshot,
                            sizeof(dst->active_snapshot));
    return 1U;
}

void audio_wave_table_projection_install_prepared(
    const audio_wavetable_descriptor_t *descriptor)
{
    audio_wavetable_registry_slot_t *const dst =
        &g_audio_wavetable_registry[descriptor->wavetable_slot];
    const uint32_t inactive = dst->active_snapshot ^ 1U;
    audio_wavetable_registry_snapshot_t *const next =
        &dst->snapshots[inactive];
    next->ready = 0U;
    next->descriptor = *descriptor;
    next->ready = 1U;
    intercore_cache_publish(next, sizeof(*next));
    dst->active_snapshot = inactive;
    intercore_cache_publish((const void *)&dst->active_snapshot,
                            sizeof(dst->active_snapshot));
}

static uint8_t resolve_selection(uint16_t logical,
                                 audio_wave_table_selection_t *out)
{
    uint16_t global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    uint16_t slot = WAVETABLE_POOL_INVALID_SLOT;
    if ((out == NULL)
        || !project_control_resolve_wavetable_runtime(logical, &global)
        || !sample_global_pool_resolve_backend(global,
            SAMPLE_GLOBAL_KIND_WAVETABLE, &slot)) return 0U;
    const wavetable_slot_t *const table = wavetable_pool_get_slot(slot);
    if ((table == NULL) || (table->state != WAVETABLE_SLOT_READY)
        || (table->data == NULL) || (table->frame_count == 0U)
        || (table->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT)) return 0U;
    *out = (audio_wave_table_selection_t){
        .wavetable_slot = slot, .generation = table->generation };
    return 1U;
}

static uint8_t publish_selection(
    uint8_t index, const audio_wave_table_selection_t *selection)
{
    if (selection == NULL) return 0U;
    control_audio_command_t commands[2];
    if ((control_rt_build_param_command(index,
            CONTROL_AUDIO_PARAM_WAVETABLE_GEN, selection->generation,
            CONTROL_AUDIO_PARAM_KIND_BASE_GLOBAL, 0U, &commands[0]) == 0U)
            || (control_rt_build_param_command(index,
                CONTROL_AUDIO_PARAM_WAVETABLE_SET, selection->wavetable_slot,
                CONTROL_AUDIO_PARAM_KIND_BASE_GLOBAL, 0U,
                &commands[1]) == 0U)) return 0U;
    return control_rt_publish_batch_now(commands, 2U);
}

uint8_t audio_wave_table_projection_publish_track(
    uint8_t track, uint8_t osc, uint16_t logical)
{
    track_runtime_descriptor_t descriptor;
    if ((osc >= AUDIO_WAVETABLE_OSC_COUNT)
        || !track_runtime_get_descriptor(track, &descriptor)
        || (descriptor.engine != TRACK_RUNTIME_ENGINE_WAVE))
        return 0U;
    const uint8_t index = (uint8_t)(track
        * AUDIO_WAVETABLE_OSC_COUNT + osc);
    audio_wave_table_selection_t selection = {
        .wavetable_slot = WAVETABLE_POOL_INVALID_SLOT };
    const uint8_t valid = resolve_selection(logical, &selection);
    if (!publish_selection(index, &selection)) return 0U;
    g_control_selection[index] = selection;
    return valid;
}

uint8_t audio_wave_table_projection_clear_track(uint8_t track, uint8_t osc)
{
    track_runtime_descriptor_t descriptor;
    if ((osc >= AUDIO_WAVETABLE_OSC_COUNT)
        || !track_runtime_get_descriptor(track, &descriptor)
        || (descriptor.engine != TRACK_RUNTIME_ENGINE_WAVE))
        return 0U;
    const uint8_t index = (uint8_t)(track
        * AUDIO_WAVETABLE_OSC_COUNT + osc);
    const audio_wave_table_selection_t invalid = {
        .wavetable_slot = WAVETABLE_POOL_INVALID_SLOT };
    if (!publish_selection(index, &invalid)) return 0U;
    g_control_selection[index] = invalid;
    return 1U;
}

void audio_wave_table_projection_withdraw_slot(uint16_t slot,
                                               uint32_t generation)
{
    if (slot < WAVETABLE_POOL_MAX_SLOTS)
    {
        audio_wavetable_registry_slot_t *const registry =
            &g_audio_wavetable_registry[slot];
        const uint32_t active = registry->active_snapshot;
        const audio_wavetable_registry_snapshot_t *const current =
            &registry->snapshots[active];
        if ((generation == 0U)
            || ((current->ready != 0U)
                && (current->descriptor.generation == generation)))
        {
            const uint32_t inactive = active ^ 1U;
            audio_wavetable_registry_snapshot_t *const next =
                &registry->snapshots[inactive];
            memset(next, 0, sizeof(*next));
            intercore_cache_publish(next, sizeof(*next));
            registry->active_snapshot = inactive;
            intercore_cache_publish((const void *)&registry->active_snapshot,
                                    sizeof(registry->active_snapshot));
        }
    }
    for (uint8_t i = 0U; i < AUDIO_WAVE_TABLE_SELECTION_COUNT; ++i)
        if ((g_control_selection[i].wavetable_slot == slot)
            && (g_control_selection[i].generation == generation))
        {
            const audio_wave_table_selection_t invalid = {
                .wavetable_slot = WAVETABLE_POOL_INVALID_SLOT };
            if (publish_selection(i, &invalid)) g_control_selection[i] = invalid;
        }
}
