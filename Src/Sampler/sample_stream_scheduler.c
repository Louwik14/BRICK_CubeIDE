#include "Sampler/sample_stream_scheduler.h"

#include <string.h>

static sample_stream_scheduler_config_t g_sample_stream_scheduler_config = {
    .max_wait_frames = SAMPLE_STREAM_SCHEDULER_DEFAULT_MAX_WAIT_FRAMES,
};

static sample_stream_audio_frame_t sample_stream_scheduler_saturating_add(
    sample_stream_audio_frame_t value,
    sample_stream_audio_frame_t increment)
{
    return (value > (SAMPLE_STREAM_AUDIO_FRAME_NEVER - increment))
               ? SAMPLE_STREAM_AUDIO_FRAME_NEVER
               : value + increment;
}

static sample_stream_audio_frame_t sample_stream_scheduler_dispatch_deadline(
    const sample_stream_request_entry_t *entry)
{
    const sample_stream_audio_frame_t starvation_deadline =
        sample_stream_scheduler_saturating_add(entry->created_audio_frame,
                                               g_sample_stream_scheduler_config.max_wait_frames);
    return (entry->consume_deadline_audio_frame < starvation_deadline)
               ? entry->consume_deadline_audio_frame
               : starvation_deadline;
}

void sample_stream_scheduler_init(const sample_stream_scheduler_config_t *config)
{
    g_sample_stream_scheduler_config.max_wait_frames =
        ((config != 0) && (config->max_wait_frames != 0U))
            ? config->max_wait_frames
            : SAMPLE_STREAM_SCHEDULER_DEFAULT_MAX_WAIT_FRAMES;
}

uint8_t sample_stream_scheduler_pick(
    const sample_stream_request_entry_t *entries,
    uint32_t entry_count,
    sample_stream_audio_frame_t now_audio_frame,
    sample_stream_scheduler_decision_t *out_decision)
{
    if ((entries == 0) || (entry_count == 0U) || (out_decision == 0))
    {
        return 0U;
    }

    uint8_t found = 0U;
    uint32_t best_index = 0U;
    sample_stream_audio_frame_t best_dispatch_deadline = SAMPLE_STREAM_AUDIO_FRAME_NEVER;
    sample_stream_audio_frame_t best_consume_deadline = SAMPLE_STREAM_AUDIO_FRAME_NEVER;
    sample_stream_audio_frame_t best_created = SAMPLE_STREAM_AUDIO_FRAME_NEVER;
    uint32_t best_sequence = UINT32_MAX;

    for (uint32_t i = 0U; i < entry_count; ++i)
    {
        const sample_stream_request_entry_t *const entry = &entries[i];
        if (entry->active == 0U)
        {
            continue;
        }

        const sample_stream_audio_frame_t dispatch_deadline =
            sample_stream_scheduler_dispatch_deadline(entry);
        const uint8_t better = (uint8_t)((found == 0U)
            || (dispatch_deadline < best_dispatch_deadline)
            || ((dispatch_deadline == best_dispatch_deadline)
                && (entry->consume_deadline_audio_frame < best_consume_deadline))
            || ((dispatch_deadline == best_dispatch_deadline)
                && (entry->consume_deadline_audio_frame == best_consume_deadline)
                && (entry->created_audio_frame < best_created))
            || ((dispatch_deadline == best_dispatch_deadline)
                && (entry->consume_deadline_audio_frame == best_consume_deadline)
                && (entry->created_audio_frame == best_created)
                && (entry->requested_at < best_sequence)));
        if (better == 0U)
        {
            continue;
        }

        found = 1U;
        best_index = i;
        best_dispatch_deadline = dispatch_deadline;
        best_consume_deadline = entry->consume_deadline_audio_frame;
        best_created = entry->created_audio_frame;
        best_sequence = entry->requested_at;
    }

    if (found == 0U)
    {
        return 0U;
    }

    memset(out_decision, 0, sizeof(*out_decision));
    out_decision->entry_index = best_index;
    out_decision->consume_deadline_audio_frame = best_consume_deadline;
    out_decision->dispatch_deadline_audio_frame = best_dispatch_deadline;
    out_decision->waited_frames = (now_audio_frame > best_created)
                                      ? (now_audio_frame - best_created)
                                      : 0U;
    out_decision->starvation_guard_applied =
        (best_dispatch_deadline < best_consume_deadline) ? 1U : 0U;
    return 1U;
}
