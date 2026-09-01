#include "Sampler/audio_wave_table_projection_control.h"
#include "IPC/audio_wavetable_registry_contract.h"

#include <string.h>

#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "IPC/live_clock_control.h"
#include "Param/param_registry.h"
#include "Platform/intercore_cache.h"
#include "Platform/memory_layout.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Storage/project_control.h"
#include "Track/track_runtime.h"
#include "stm32h7xx.h"

#define AUDIO_WAVE_TABLE_SELECTION_COUNT \
    (AUDIO_WAVETABLE_VOICE_INSTANCE_COUNT * AUDIO_WAVETABLE_OSC_COUNT)

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

uint8_t audio_wave_table_projection_install_descriptor(
    const audio_wavetable_descriptor_t *descriptor)
{
    if ((descriptor == NULL)
        || (descriptor->wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || (descriptor->generation == 0U)
        || (descriptor->band_count > WAVETABLE_MIPMAP_MAX_BANDS)) return 0U;
    audio_wavetable_registry_slot_t *const dst =
        &g_audio_wavetable_registry[descriptor->wavetable_slot];
    dst->sequence++;
    dst->ready = 0U;
    intercore_cache_publish(dst, sizeof(*dst));
    dst->descriptor = *descriptor;
    dst->ready = 1U;
    __DMB();
    dst->sequence++;
    intercore_cache_publish(dst, sizeof(*dst));
    return 1U;
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
    uint64_t sample = 0U;
    if ((selection == NULL) || !live_clock_read_audio_sample(&sample)) return 0U;
    const control_audio_command_t commands[2] = {
        { .effective_sample_time = sample, .value = selection->generation,
          .id = CONTROL_AUDIO_PARAM_WAVETABLE_GEN, .entity = index,
          .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PARAM, 0U) },
        { .effective_sample_time = sample, .value = selection->wavetable_slot,
          .id = CONTROL_AUDIO_PARAM_WAVETABLE_SET, .entity = index,
          .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PARAM, 0U) }
    };
    return control_audio_publish_batch(commands, 2U);
}

uint8_t audio_wave_table_projection_publish_track(
    uint8_t track, uint8_t osc, uint16_t logical)
{
    track_runtime_descriptor_t descriptor;
    if ((osc >= AUDIO_WAVETABLE_OSC_COUNT)
        || !track_runtime_get_descriptor(track, &descriptor)
        || (descriptor.engine != TRACK_RUNTIME_ENGINE_WAVE)
        || (descriptor.instance_id >= AUDIO_WAVETABLE_VOICE_INSTANCE_COUNT))
        return 0U;
    const uint8_t index = (uint8_t)(descriptor.instance_id
        * AUDIO_WAVETABLE_OSC_COUNT + osc);
    audio_wave_table_selection_t selection = {
        .wavetable_slot = WAVETABLE_POOL_INVALID_SLOT };
    const uint8_t valid = resolve_selection(logical, &selection);
    if (!publish_selection(index, &selection)) return 0U;
    g_control_selection[index] = selection;
    return valid;
}

void audio_wave_table_projection_withdraw_slot(uint16_t slot,
                                               uint32_t generation)
{
    if (slot < WAVETABLE_POOL_MAX_SLOTS)
    {
        audio_wavetable_registry_slot_t *const registry =
            &g_audio_wavetable_registry[slot];
        if ((generation == 0U)
            || (registry->descriptor.generation == generation))
        {
            registry->sequence++;
            registry->ready = 0U;
            intercore_cache_publish(registry, sizeof(*registry));
            registry->sequence++;
            intercore_cache_publish(registry, sizeof(*registry));
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
