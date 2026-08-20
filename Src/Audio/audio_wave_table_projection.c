#include "Core/audio_wave_table_projection.h"

#include <string.h>

#include "Core/brick6_wave_runtime.h"
#include "Storage/memory_layout.h"
#include "Sampler/wavetable_pool.h"
#include "stm32h7xx.h"

#define AUDIO_WAVE_TABLE_BINDING_COUNT \
    (BRICK6_WAVE_VOICE_INSTANCE_COUNT * BRICK6_WAVE_OSC_COUNT)
#define AUDIO_WAVE_TABLE_CONSUME_ATTEMPTS 2U

static AUDIO_HOT audio_wave_table_binding_t
    g_audio_wave_table_local[AUDIO_WAVE_TABLE_BINDING_COUNT];
static AUDIO_HOT audio_wave_table_binding_t
    g_audio_wave_table_candidate[AUDIO_WAVE_TABLE_BINDING_COUNT];
static AUDIO_HOT audio_wave_table_binding_t
    g_audio_wave_table_applied[AUDIO_WAVE_TABLE_BINDING_COUNT];
static uint32_t g_audio_wave_table_projection_sequence;

typedef struct
{
    volatile uint32_t sequence;
    audio_wave_table_binding_t entry[AUDIO_WAVE_TABLE_BINDING_COUNT];
} audio_wave_table_projection_mailbox_t;

extern audio_wave_table_projection_mailbox_t g_audio_wave_table_projection;

static uint8_t audio_wave_table_binding_equal(
    const audio_wave_table_binding_t *a,
    const audio_wave_table_binding_t *b)
{
    return (uint8_t)((a->wavetable_slot == b->wavetable_slot)
            && (a->generation == b->generation));
}

void audio_wave_table_projection_audio_init(void)
{
    memset(g_audio_wave_table_local, 0,
           sizeof(g_audio_wave_table_local));
    memset(g_audio_wave_table_candidate, 0,
           sizeof(g_audio_wave_table_candidate));
    for (uint16_t i = 0U; i < AUDIO_WAVE_TABLE_BINDING_COUNT; ++i)
    {
        g_audio_wave_table_local[i].wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
        g_audio_wave_table_applied[i].wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    }
    g_audio_wave_table_projection_sequence = UINT32_MAX;
}

void audio_wave_table_projection_audio_consume(void)
{
    for (uint32_t attempt = 0U;
         attempt < AUDIO_WAVE_TABLE_CONSUME_ATTEMPTS;
         ++attempt)
    {
        const uint32_t before = g_audio_wave_table_projection.sequence;
        if (before == g_audio_wave_table_projection_sequence)
            return;
        __DMB();
        if ((before & 1U) != 0U)
            continue;
        memcpy(g_audio_wave_table_candidate,
               (const void *)g_audio_wave_table_projection.entry,
               sizeof(g_audio_wave_table_candidate));
        __DMB();
        if (before == g_audio_wave_table_projection.sequence)
        {
            memcpy(g_audio_wave_table_local, g_audio_wave_table_candidate,
                   sizeof(g_audio_wave_table_local));
            for (uint8_t instance = 0U;
                 instance < BRICK6_WAVE_VOICE_INSTANCE_COUNT;
                 ++instance)
            {
                for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
                {
                    const uint16_t index =
                        (uint16_t)instance * BRICK6_WAVE_OSC_COUNT + osc;
                    const audio_wave_table_binding_t *const binding =
                        &g_audio_wave_table_local[index];
                    if (audio_wave_table_binding_equal(
                            binding, &g_audio_wave_table_applied[index]) != 0U)
                        continue;
                    const wavetable_slot_t *const table =
                        (binding->wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
                        ? wavetable_pool_get_slot(binding->wavetable_slot) : NULL;
                    const uint32_t generation = (table != NULL)
                        ? table->generation : 0U;
                    if ((table == NULL)
                            || (table->state != WAVETABLE_SLOT_READY)
                            || (table->data == NULL)
                            || (table->frame_count == 0U)
                            || (table->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT)
                            || (generation != binding->generation))
                    {
                        brick6_wave_runtime_set_osc_table_wavetable_generation(
                            instance, osc, WAVETABLE_POOL_INVALID_SLOT, 0U);
                    }
                    else
                    {
                        brick6_wave_runtime_set_osc_table_wavetable_generation(
                            instance, osc, binding->wavetable_slot,
                            binding->generation);
                    }
                    g_audio_wave_table_applied[index] = *binding;
                }
            }
            g_audio_wave_table_projection_sequence = before;
            return;
        }
    }
}
