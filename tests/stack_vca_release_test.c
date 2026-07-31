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

    if (brick6_stack_runtime_render_instance(0U, block, FRAMES, 0U) != 0U)
    {
        return 3;
    }

    brick6_stack_runtime_note_on(0U, 64U, 127U);
    brick6_stack_runtime_all_notes_off(0U);
    if (brick6_stack_runtime_render_instance(0U, block, FRAMES, 1U) != 0U)
    {
        return 4;
    }

    if (brick6_stack_runtime_submit_note_on(0U, 67U, 127U) == 0U)
    {
        return 5;
    }
    brick6_stack_runtime_cancel_note_state(0U);
    brick6_stack_runtime_process_commands_from_audio();
    if ((brick6_stack_runtime_get_voice(0U)->gate != 0U)
            || (brick6_stack_runtime_get_voice(0U)->has_active_note != 0U))
    {
        return 6;
    }
    if (brick6_stack_runtime_render_instance(0U, block, FRAMES, 1U) != 0U)
    {
        return 7;
    }

    if (brick6_stack_runtime_submit_note_on(0U, 69U, 127U) == 0U)
    {
        return 8;
    }
    brick6_stack_runtime_process_commands_from_audio();
    if ((brick6_stack_runtime_get_voice(0U)->gate == 0U)
            || (brick6_stack_runtime_get_voice(0U)->has_active_note == 0U))
    {
        return 9;
    }

    puts("stack VCA release and paste note cancellation: ok");
    return 0;
}
