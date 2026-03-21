#include "Audio/microdexed_synth.h"

#include <algorithm>

#ifndef MICRODEXED_MINIMAL
#define MICRODEXED_MINIMAL
#endif
#include "../../Micro_Dexed/microdexed_marki_minimal.h"

namespace
{
MicroDexedMarkIMinimal g_microdexed;
uint8_t g_microdexed_initialized = 0U;
uint8_t g_microdexed_enabled = 0U;
int16_t g_render_buffer[DEXED_RENDER_MAX_FRAMES];

constexpr float kInt16ToFloat = 1.0f / 32768.0f;
}

extern "C" {

void microdexed_synth_init(float sample_rate, uint32_t block_size)
{
    (void)block_size;

    const int rate = (sample_rate > 0.0f) ? static_cast<int>(sample_rate + 0.5f) : SAMPLE_RATE;

    g_microdexed_initialized = g_microdexed.init(rate) ? 1U : 0U;
    g_microdexed_enabled = 0U;

    if (g_microdexed_initialized != 0U)
    {
        g_microdexed.loadDefaultPatch();
        g_microdexed.allNotesOff();
    }
}

void microdexed_synth_set_enabled(uint8_t enabled)
{
    g_microdexed_enabled = (enabled != 0U) ? 1U : 0U;

    if ((g_microdexed_enabled == 0U) && (g_microdexed_initialized != 0U))
    {
        g_microdexed.allNotesOff();
    }
}

uint8_t microdexed_synth_is_enabled(void)
{
    return (uint8_t)((g_microdexed_initialized != 0U) && (g_microdexed_enabled != 0U));
}

void microdexed_synth_note_on(uint8_t midi_note, uint8_t velocity)
{
    if (microdexed_synth_is_enabled() == 0U)
    {
        return;
    }

    if (velocity == 0U)
    {
        velocity = 1U;
    }

    g_microdexed.noteOn(midi_note, velocity);
}

void microdexed_synth_note_off(uint8_t midi_note)
{
    if (g_microdexed_initialized == 0U)
    {
        return;
    }

    g_microdexed.noteOff(midi_note);
}

void microdexed_synth_all_notes_off(void)
{
    if (g_microdexed_initialized == 0U)
    {
        return;
    }

    g_microdexed.allNotesOff();
}

void microdexed_synth_process_block(float *mono_out, uint32_t frames)
{
    if (mono_out == nullptr || frames == 0U)
    {
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        mono_out[i] = 0.0f;
    }

    if (microdexed_synth_is_enabled() == 0U)
    {
        return;
    }

    uint32_t offset = 0U;
    while (offset < frames)
    {
        const uint32_t chunk = std::min<uint32_t>(frames - offset, DEXED_RENDER_MAX_FRAMES);
        g_microdexed.render(g_render_buffer, static_cast<int>(chunk));

        for (uint32_t i = 0U; i < chunk; ++i)
        {
            mono_out[offset + i] = (float)g_render_buffer[i] * kInt16ToFloat;
        }

        offset += chunk;
    }
}

} /* extern \"C\" */
