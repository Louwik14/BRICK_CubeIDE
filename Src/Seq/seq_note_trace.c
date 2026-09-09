#include "Seq/seq_note_trace.h"

#include "stm32h7xx.h"
#include "Platform/memory_layout.h"

#define SEQ_NOTE_TRACE_WATCHED_OUTPUT_CAPACITY 64U

typedef struct
{
    uint32_t output_id;
    uint8_t track;
    uint8_t step;
} seq_note_trace_watched_output_t;

SEQ_STATE_D2 volatile seq_note_trace_entry_t
    g_seq_note_trace_ring[SEQ_NOTE_TRACE_CAPACITY];
volatile uint32_t g_seq_note_trace_head;
volatile uint32_t g_seq_note_trace_overwrite_count;
volatile uint8_t g_seq_note_trace_enabled = 1U;
volatile uint8_t g_seq_note_trace_track = 0U;
volatile uint8_t g_seq_note_trace_step = 0U;
volatile seq_step_debug_t g_seq_step_debug;

SEQ_STATE_D2 static seq_note_trace_watched_output_t
    g_seq_note_trace_watched_outputs[SEQ_NOTE_TRACE_WATCHED_OUTPUT_CAPACITY];
static uint8_t g_seq_note_trace_watched_output_cursor;
static uint8_t g_seq_note_trace_horizon_dirty;

uint8_t seq_note_trace_target(uint8_t track, uint8_t step)
{
    return (g_seq_note_trace_enabled != 0U)
        && (track == g_seq_note_trace_track)
        && (step == g_seq_note_trace_step);
}

void seq_note_trace_record(uint16_t kind, uint8_t track, uint8_t step,
                           uint64_t sample, uint64_t aux_sample,
                           uint32_t output_id, uint32_t related_id)
{
    if (g_seq_note_trace_enabled == 0U)
        return;
    if (seq_note_trace_target(track, step) != 0U)
    {
        g_seq_step_debug.track = track;
        g_seq_step_debug.processed_step = step;
        g_seq_step_debug.sample_time = sample;
        g_seq_step_debug.last_output_id = output_id;
        if ((kind == SEQ_NOTE_TRACE_ACTIVE_CREATED)
                || (kind == SEQ_NOTE_TRACE_ACTIVE_REPLACED))
            ++g_seq_step_debug.note_on_generated_count;
        else if ((kind == SEQ_NOTE_TRACE_TERMINAL_ON)
                || (kind == SEQ_NOTE_TRACE_TERMINAL_OFF))
            ++g_seq_step_debug.actions_staged_count;
        else if ((kind == SEQ_NOTE_TRACE_FIFO_NOTE_ON)
                || (kind == SEQ_NOTE_TRACE_FIFO_NOTE_OFF))
            ++g_seq_step_debug.actions_published_count;
        if (kind == SEQ_NOTE_TRACE_SKIP_COMMIT_FLOOR)
            g_seq_step_debug.last_skip_reason = SEQ_STEP_DEBUG_SKIP_COMMIT_FLOOR;
        else if (kind == SEQ_NOTE_TRACE_SKIP_INVALIDATED)
            g_seq_step_debug.last_skip_reason = SEQ_STEP_DEBUG_SKIP_INVALIDATED;
        else if (kind == SEQ_NOTE_TRACE_REJECT_STALE_SCHEDULER)
            g_seq_step_debug.last_skip_reason = SEQ_STEP_DEBUG_SKIP_STALE;
        else if (kind == SEQ_NOTE_TRACE_SUPPRESS_MUTED)
            g_seq_step_debug.last_skip_reason = SEQ_STEP_DEBUG_SKIP_MUTED;
        else if (kind == SEQ_NOTE_TRACE_REJECT_NOTE_FX)
            g_seq_step_debug.last_skip_reason = SEQ_STEP_DEBUG_SKIP_NOTE_FX;
    }
    const uint32_t sequence = g_seq_note_trace_head;
    const uint32_t index = sequence & (SEQ_NOTE_TRACE_CAPACITY - 1U);
    volatile seq_note_trace_entry_t *const entry =
        &g_seq_note_trace_ring[index];
    entry->sample = sample;
    entry->aux_sample = aux_sample;
    entry->output_id = output_id;
    entry->related_id = related_id;
    entry->kind = kind;
    entry->track = track;
    entry->step = step;
    __DMB();
    entry->sequence = sequence;
    __DMB();
    g_seq_note_trace_head = sequence + 1U;
    if (sequence >= SEQ_NOTE_TRACE_CAPACITY)
        ++g_seq_note_trace_overwrite_count;
    if ((kind == SEQ_NOTE_TRACE_ACTIVE_CREATED)
            || (kind == SEQ_NOTE_TRACE_ACTIVE_REPLACED)
            || (kind == SEQ_NOTE_TRACE_VICTIM_OFF_GENERATED)
            || (kind == SEQ_NOTE_TRACE_VICTIM_OFF_MISSING)
            || (kind == SEQ_NOTE_TRACE_TERMINAL_ON)
            || (kind == SEQ_NOTE_TRACE_TERMINAL_OFF)
            || (kind == SEQ_NOTE_TRACE_REJECT_NOTE_FX)
            || (kind == SEQ_NOTE_TRACE_REJECT_TERMINAL))
        g_seq_note_trace_horizon_dirty = 1U;
}

void seq_note_trace_watch_output(uint8_t track, uint8_t step,
                                 uint32_t output_id)
{
    if ((output_id == 0U) || (seq_note_trace_target(track, step) == 0U))
        return;
    seq_note_trace_watched_output_t *const watched =
        &g_seq_note_trace_watched_outputs[
            g_seq_note_trace_watched_output_cursor];
    watched->output_id = output_id;
    watched->track = track;
    watched->step = step;
    g_seq_note_trace_watched_output_cursor = (uint8_t)(
        (g_seq_note_trace_watched_output_cursor + 1U)
        % SEQ_NOTE_TRACE_WATCHED_OUTPUT_CAPACITY);
}

uint8_t seq_note_trace_output_is_watched(uint32_t output_id,
                                         uint8_t *out_track,
                                         uint8_t *out_step)
{
    if ((g_seq_note_trace_enabled == 0U) || (output_id == 0U))
        return 0U;
    for (uint8_t i = 0U; i < SEQ_NOTE_TRACE_WATCHED_OUTPUT_CAPACITY; ++i)
    {
        if (g_seq_note_trace_watched_outputs[i].output_id != output_id)
            continue;
        if (out_track != 0)
            *out_track = g_seq_note_trace_watched_outputs[i].track;
        if (out_step != 0)
            *out_step = g_seq_note_trace_watched_outputs[i].step;
        return 1U;
    }
    return 0U;
}

void seq_note_trace_horizon_begin(uint64_t first_sample)
{
    g_seq_step_debug.horizon_first = first_sample;
    g_seq_note_trace_horizon_dirty = 0U;
}

void seq_note_trace_horizon_commit(uint64_t first_sample,
                                   uint64_t end_sample)
{
    g_seq_step_debug.horizon_first = first_sample;
    g_seq_step_debug.horizon_end = end_sample;
    if (g_seq_note_trace_horizon_dirty != 0U)
        seq_note_trace_record(SEQ_NOTE_TRACE_HORIZON_COMMIT,
            g_seq_note_trace_track, g_seq_note_trace_step,
            first_sample, end_sample, 0U, 0U);
    g_seq_note_trace_horizon_dirty = 0U;
}

void seq_note_trace_horizon_abort(uint64_t first_sample,
                                  uint64_t end_sample)
{
    g_seq_step_debug.horizon_first = first_sample;
    g_seq_step_debug.horizon_end = end_sample;
    ++g_seq_step_debug.musical_window_aborted_count;
    g_seq_step_debug.last_skip_reason = SEQ_STEP_DEBUG_SKIP_WINDOW_ABORTED;
    if (g_seq_note_trace_horizon_dirty != 0U)
        seq_note_trace_record(SEQ_NOTE_TRACE_HORIZON_ABORT,
            g_seq_note_trace_track, g_seq_note_trace_step,
            first_sample, end_sample, 0U, 0U);
    g_seq_note_trace_horizon_dirty = 0U;
}

void seq_step_debug_expect(uint8_t track, uint8_t step, uint64_t sample_time)
{
    if (seq_note_trace_target(track, step) == 0U) return;
    g_seq_step_debug.track = track;
    g_seq_step_debug.expected_step = step;
    g_seq_step_debug.sample_time = sample_time;
    ++g_seq_step_debug.expected_count;
}

void seq_step_debug_scheduler_processed(uint8_t track, uint8_t step,
                                        uint64_t sample_time,
                                        uint8_t generated_any)
{
    if (seq_note_trace_target(track, step) == 0U) return;
    g_seq_step_debug.processed_step = step;
    g_seq_step_debug.sample_time = sample_time;
    ++g_seq_step_debug.scheduler_processed_count;
    if (generated_any == 0U)
        ++g_seq_step_debug.step_advance_without_event_count;
}

void seq_step_debug_source_refused(uint8_t track, uint8_t step,
                                   uint64_t sample_time)
{
    if (seq_note_trace_target(track, step) == 0U) return;
    ++g_seq_step_debug.scheduler_source_refused_count;
    g_seq_step_debug.sample_time = sample_time;
    g_seq_step_debug.last_skip_reason = SEQ_STEP_DEBUG_SKIP_SOURCE_REJECTED;
}

void seq_step_debug_imminent_refused(uint8_t track, uint8_t step,
                                     uint64_t sample_time)
{
    if (seq_note_trace_target(track, step) == 0U) return;
    ++g_seq_step_debug.imminent_occurrence_refused_count;
    g_seq_step_debug.sample_time = sample_time;
    g_seq_step_debug.last_skip_reason = SEQ_STEP_DEBUG_SKIP_IMMINENT_REJECTED;
}

void seq_step_debug_note_off_generated(uint8_t track, uint8_t step,
                                       uint64_t sample_time,
                                       uint32_t output_id)
{
    if (seq_note_trace_target(track, step) == 0U) return;
    ++g_seq_step_debug.note_off_generated_count;
    g_seq_step_debug.sample_time = sample_time;
    g_seq_step_debug.last_output_id = output_id;
}

void seq_step_debug_audio(uint32_t output_id, uint64_t sample_time,
                          uint8_t applied)
{
    uint8_t track = 0U, step = 0U;
    if (seq_note_trace_output_is_watched(output_id, &track, &step) == 0U)
        return;
    ++g_seq_step_debug.audio_consumed_count;
    if (applied != 0U) ++g_seq_step_debug.audio_applied_count;
    g_seq_step_debug.track = track;
    g_seq_step_debug.processed_step = step;
    g_seq_step_debug.sample_time = sample_time;
    g_seq_step_debug.last_output_id = output_id;
}
