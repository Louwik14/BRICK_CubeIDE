#include "Core/brick6_stack_runtime.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

uint8_t audio_track_diag_is_selected_logical_track(uint8_t track)
{
    (void)track;
    return 0U;
}

void audio_track_diag_report_stack_soft_clips(uint8_t track, uint32_t count)
{
    (void)track;
    (void)count;
}

int16_t brick6_stack_braids_wavetable_sample(uint8_t wave_index, uint32_t phase)
{
    (void)wave_index;
    return (int16_t)(phase >> 16);
}

static float block_energy(const float *samples, uint32_t frames)
{
    float energy = 0.0f;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        energy += fabsf(samples[i]);
    }
    return energy;
}

int main(void)
{
    enum { FRAMES = BRICK6_STACK_RENDER_BLOCK_SIZE };
    float block[FRAMES];

    brick6_stack_runtime_init();
    brick6_stack_runtime_note_on(0U, 60U, 127U);
    brick6_stack_runtime_render_instance(0U, block, FRAMES, 1U);
    if (block_energy(block, FRAMES) <= 0.01f)
    {
        return 1;
    }

    brick6_stack_runtime_note_off(0U, 60U);
    brick6_stack_runtime_render_instance(0U, block, FRAMES, 1U);
    if (block_energy(block, FRAMES) <= 0.01f)
    {
        return 2;
    }

    brick6_stack_runtime_render_instance(0U, block, FRAMES, 0U);
    if (block_energy(block, FRAMES) != 0.0f)
    {
        return 3;
    }

    brick6_stack_runtime_note_on(0U, 64U, 127U);
    brick6_stack_runtime_all_notes_off(0U);
    brick6_stack_runtime_render_instance(0U, block, FRAMES, 1U);
    if (block_energy(block, FRAMES) != 0.0f)
    {
        return 4;
    }

    puts("stack VCA release source lifetime: ok");
    return 0;
}
