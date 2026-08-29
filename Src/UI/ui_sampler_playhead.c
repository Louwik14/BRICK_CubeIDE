#include "UI/ui_sampler_playhead.h"

#include "Platform/memory_layout.h"

CTRL_STATE static uint64_t
    g_ui_sampler_playhead[BRICK_ENTITY_CAPACITY];

void ui_sampler_playhead_init(void)
{
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY;
         ++entity_id)
    {
        g_ui_sampler_playhead[entity_id] = 0ULL;
    }
}

void ui_sampler_playhead_note_trigger(brick_entity_id_t entity_id,
                                      uint64_t due_sample)
{
    if (entity_id >= BRICK_ENTITY_CAPACITY)
        return;
    g_ui_sampler_playhead[entity_id] = due_sample + 1ULL;
}

ui_sampler_playhead_view_t ui_sampler_playhead_view(
    brick_entity_id_t entity_id,
    uint64_t now_sample,
    uint32_t duration_samples,
    uint8_t mode)
{
    ui_sampler_playhead_view_t view = {0U, 0.0f};
    if ((entity_id >= BRICK_ENTITY_CAPACITY) || (duration_samples == 0U))
        return view;

    const uint64_t encoded_start = g_ui_sampler_playhead[entity_id];
    if (encoded_start == 0ULL)
        return view;
    const uint64_t start_sample = encoded_start - 1ULL;
    if (now_sample < start_sample)
        return view;

    const uint64_t elapsed = now_sample - start_sample;
    if ((mode <= 1U) && (elapsed >= duration_samples))
        return view;

    const uint64_t cycle = elapsed % duration_samples;
    float phase = (float)cycle / (float)duration_samples;
    if (mode == 1U)
        phase = 1.0f - ((float)elapsed / (float)duration_samples);
    else if ((mode == 3U) && (((elapsed / duration_samples) & 1ULL) != 0ULL))
        phase = 1.0f - phase;

    view.active = 1U;
    view.normalized_position = phase;
    return view;
}
