#include "Core/audio_wave_table_projection.h"

#include <string.h>

#include "Audio/audio_note_engine_adapter.h"
#include "Core/project_control.h"
#include "Core/track_tone_sound_state.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

#define AUDIO_WAVE_TABLE_BINDING_COUNT \
    (BRICK6_WAVE_VOICE_INSTANCE_COUNT * BRICK6_WAVE_OSC_COUNT)

typedef struct
{
    volatile uint32_t sequence;
    audio_wave_table_binding_t entry[AUDIO_WAVE_TABLE_BINDING_COUNT];
} audio_wave_table_projection_mailbox_t;

D3_IPC audio_wave_table_projection_mailbox_t
    g_audio_wave_table_projection;

static uint16_t audio_wave_table_entry_index(uint8_t instance, uint8_t osc)
{
    return (uint16_t)instance * BRICK6_WAVE_OSC_COUNT + osc;
}

static uint8_t audio_wave_table_resolve(uint16_t logical_slot,
                                        audio_wave_table_binding_t *out)
{
    uint16_t runtime_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    uint16_t wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    if ((out == NULL)
            || (project_control_resolve_wavetable_runtime(logical_slot,
                                                          &runtime_global) == 0U)
            || (sample_global_pool_resolve_backend(runtime_global,
                                                   SAMPLE_GLOBAL_KIND_WAVETABLE,
                                                   &wavetable_slot) == 0U))
    {
        return 0U;
    }

    const wavetable_slot_t *const table = wavetable_pool_get_slot(wavetable_slot);
    if ((table == NULL)
            || (table->state != WAVETABLE_SLOT_READY)
            || (table->data == NULL)
            || (table->frame_count == 0U)
            || (table->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT))
    {
        return 0U;
    }
    *out = (audio_wave_table_binding_t){
        .wavetable_slot = wavetable_slot,
        .reserved = 0U,
        .generation = table->generation
    };
    return 1U;
}

void audio_wave_table_projection_init(void)
{
    memset((void *)&g_audio_wave_table_projection, 0,
           sizeof(g_audio_wave_table_projection));
    for (uint16_t i = 0U; i < AUDIO_WAVE_TABLE_BINDING_COUNT; ++i)
        g_audio_wave_table_projection.entry[i].wavetable_slot =
            WAVETABLE_POOL_INVALID_SLOT;
}

static void audio_wave_table_projection_publish_entries(
    const audio_wave_table_binding_t *entries)
{
    uint32_t sequence = g_audio_wave_table_projection.sequence;
    if ((sequence & 1U) != 0U)
        ++sequence;
    g_audio_wave_table_projection.sequence = sequence + 1U;
    __DMB();
    memcpy((void *)g_audio_wave_table_projection.entry,
           entries, sizeof(g_audio_wave_table_projection.entry));
    __DMB();
    g_audio_wave_table_projection.sequence = sequence + 2U;
    __DMB();
}

uint8_t audio_wave_table_projection_publish_track(
    uint8_t track, uint8_t osc, uint16_t logical_slot)
{
    if (osc >= BRICK6_WAVE_OSC_COUNT)
        return 0U;
    audio_binding_snapshot_t binding_snapshot;
    if ((audio_note_engine_adapter_snapshot_read(track, &binding_snapshot) == 0U)
            || (binding_snapshot.binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            || (binding_snapshot.binding.instance_id >= BRICK6_WAVE_VOICE_INSTANCE_COUNT))
    {
        return 0U;
    }

    audio_wave_table_binding_t entries[AUDIO_WAVE_TABLE_BINDING_COUNT];
    memcpy(entries, (const void *)g_audio_wave_table_projection.entry,
           sizeof(entries));
    const uint16_t index = audio_wave_table_entry_index(
        binding_snapshot.binding.instance_id, osc);
    entries[index] = (audio_wave_table_binding_t){
        .wavetable_slot = WAVETABLE_POOL_INVALID_SLOT,
        .reserved = 0U,
        .generation = 0U
    };
    const uint8_t logical_valid = project_control_has_wavetable(logical_slot);
    if (logical_valid != 0U)
        (void)audio_wave_table_resolve(logical_slot, &entries[index]);
    audio_wave_table_projection_publish_entries(entries);
    return logical_valid;
}

void audio_wave_table_projection_publish_all(void)
{
    audio_wave_table_binding_t entries[AUDIO_WAVE_TABLE_BINDING_COUNT];
    for (uint16_t i = 0U; i < AUDIO_WAVE_TABLE_BINDING_COUNT; ++i)
        entries[i] = (audio_wave_table_binding_t){
            .wavetable_slot = WAVETABLE_POOL_INVALID_SLOT,
            .reserved = 0U,
            .generation = 0U
        };

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        audio_binding_snapshot_t binding_snapshot;
        const track_tone_sound_state_t *const state =
            track_tone_sound_state_get_const(track);
        if ((audio_note_engine_adapter_snapshot_read(track, &binding_snapshot) == 0U)
                || (state == NULL)
                || (binding_snapshot.binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
                || (binding_snapshot.binding.instance_id >= BRICK6_WAVE_VOICE_INSTANCE_COUNT))
            continue;
        for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
        {
            const uint16_t index = audio_wave_table_entry_index(
                binding_snapshot.binding.instance_id, osc);
            (void)audio_wave_table_resolve(
                (uint16_t)(state->wave.table[osc] + 0.5f), &entries[index]);
        }
    }
    audio_wave_table_projection_publish_entries(entries);
}
