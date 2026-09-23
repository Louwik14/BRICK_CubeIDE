#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "Keyboard/keyboard_params.h"
#include "NoteFx/note_fx_engine.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_musical_time.h"

static note_event_t emitted;
static uint8_t emitted_count;

uint8_t keyboard_params_get_root_index(void) { return 0U; }
uint8_t keyboard_params_get_scale_index(void) { return 0U; }
uint64_t seq_division_period_samples(uint8_t index,
                                     uint32_t samples_per_step_q16)
{
    (void)index;
    (void)samples_per_step_q16;
    return 100U;
}
uint8_t seq_runtime_get_musical_time(seq_track_id_t track,
                                     uint64_t sample_time,
                                     seq_musical_time_t *out_time)
{
    (void)track;
    memset(out_time, 0, sizeof(*out_time));
    out_time->sample_time = sample_time;
    out_time->samples_per_step_q16 = UINT32_C(100) << 16U;
    return 1U;
}

static note_event_result_t capture(const note_event_t *event, void *context)
{
    (void)context;
    emitted = *event;
    ++emitted_count;
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_event_t source_on(uint8_t stage)
{
    const note_event_t event = {
        .sample_abs = 0U,
        .duration_samples = NOTE_EVENT_DURATION_OPEN,
        .source_id = 1U,
        .intent_id = 2U,
        .source_generation = 1U,
        .group_id = 3U,
        .track = 0U,
        .destination_id = 0U,
        .note = 60U,
        .velocity = 100U,
        .kind = NOTE_EVENT_KIND_ON,
        .provenance = NOTE_EVENT_SOURCE_STEP,
        .stage = stage
    };
    return event;
}

static uint8_t held_count(uint8_t slot)
{
    note_event_t held[NOTE_FX_HELD_PITCH_CAPACITY];
    uint8_t count = 0U;
    assert(note_fx_engine_collect_held(0U, slot, 0U, held,
        NOTE_FX_HELD_PITCH_CAPACITY, &count) == NOTE_EVENT_RESULT_ACCEPTED);
    return count;
}

int main(void)
{
    note_event_t input = source_on(0U);
    note_event_t output[NOTE_FX_BATCH_CAPACITY];
    uint8_t count = 0U;

    note_fx_engine_init();
    assert(note_fx_engine_configure(0U, 0U, NOTE_FX_MODEL_GATE,
        100U, 0U, NOTE_FX_GATE_MODE_CLIP, 1U) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(note_fx_engine_transform(0U, &input, 1U, output,
        NOTE_FX_BATCH_CAPACITY, &count) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(held_count(0U) == 0U);

    note_fx_engine_init();
    assert(note_fx_engine_configure(0U, 0U, NOTE_FX_MODEL_SCALER,
        7U, 0U, 0U, 1U) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(note_fx_engine_transform(0U, &input, 1U, output,
        NOTE_FX_BATCH_CAPACITY, &count) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(held_count(0U) == 1U);
    note_event_t terminal_off = output[0];
    terminal_off.kind = NOTE_EVENT_KIND_OFF;
    terminal_off.dependency_mask = 1U;
    terminal_off.dependency_versions = 1U;
    note_fx_engine_release_terminal(&terminal_off);
    assert(held_count(0U) == 0U);

    note_fx_engine_init();
    assert(note_fx_engine_configure(0U, 0U, NOTE_FX_MODEL_ARP,
        0U, NOTE_FX_ARP_ORDER, 1U, 1U) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(note_fx_engine_transform(0U, &input, 1U, output,
        NOTE_FX_BATCH_CAPACITY, &count) == NOTE_EVENT_RESULT_ACCEPTED);
    emitted_count = 0U;
    assert(note_fx_engine_process(0U, 1U, UINT32_C(100) << 16U,
        capture, 0) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(emitted_count == 1U);
    assert(emitted.dependency_mask == 1U);
    terminal_off = emitted;
    terminal_off.kind = NOTE_EVENT_KIND_OFF;
    note_fx_engine_release_terminal(&terminal_off);
    assert(held_count(0U) == 1U);
    note_fx_engine_forget_dependency(0U, 0U);
    assert(held_count(0U) == 1U);

    note_fx_engine_init();
    assert(note_fx_engine_configure(0U, 1U, NOTE_FX_MODEL_ARP,
        0U, NOTE_FX_ARP_ORDER, 1U, 0x21U) == NOTE_EVENT_RESULT_ACCEPTED);
    input.stage = 1U;
    input.duration_samples = 1U;
    input.dependency_mask = 1U;
    input.dependency_versions = 1U;
    assert(note_fx_engine_transform(1U, &input, 1U, output,
        NOTE_FX_BATCH_CAPACITY, &count) == NOTE_EVENT_RESULT_ACCEPTED);
    emitted_count = 0U;
    assert(note_fx_engine_process(0U, 1U, UINT32_C(100) << 16U,
        capture, 0) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(emitted_count == 1U);
    assert(emitted.dependency_mask == 3U);
    assert(emitted.dependency_versions == 0x21U);
    terminal_off = emitted;
    terminal_off.kind = NOTE_EVENT_KIND_OFF;
    note_fx_engine_release_terminal(&terminal_off);
    assert(held_count(1U) == 1U);
    emitted_count = 0U;
    assert(note_fx_engine_process(1U, 1U, UINT32_C(100) << 16U,
        capture, 0) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(emitted_count == 0U);
    assert(held_count(1U) == 0U);

    note_fx_engine_init();
    input = source_on(0U);
    assert(note_fx_engine_configure(0U, 0U, NOTE_FX_MODEL_SCALER,
        7U, 0U, 0U, 1U) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(note_fx_engine_transform(0U, &input, 1U, output,
        NOTE_FX_BATCH_CAPACITY, &count) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(held_count(0U) == 1U);
    output[0].stage = 2U;
    output[0].dependency_mask = 3U;
    output[0].dependency_versions = 0x21U;
    assert(note_fx_engine_configure(0U, 2U, NOTE_FX_MODEL_ARP,
        0U, NOTE_FX_ARP_ORDER, 1U, 0x300U) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(note_fx_engine_transform(2U, output, 1U, &output[1],
        NOTE_FX_BATCH_CAPACITY - 1U, &count) == NOTE_EVENT_RESULT_ACCEPTED);
    assert(held_count(2U) == 1U);
    note_fx_engine_forget_dependency(0U, 1U);
    assert(held_count(0U) == 1U);
    assert(held_count(2U) == 0U);

    return 0;
}
