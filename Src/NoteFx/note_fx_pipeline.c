#include "NoteFx/note_fx_pipeline.h"
#include <string.h>

#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "NoteFx/note_fx_engine.h"
#include "NoteFx/note_fx_state.h"
#include "Core/track_runtime.h"
#include "Seq/seq_runtime_exec.h"

static uint8_t g_note_fx_override_valid[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT];
static uint8_t g_note_fx_override_value[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT];
static uint8_t g_note_fx_runtime_arp_slot[NOTE_FX_TRACK_COUNT];

static void note_fx_pipeline_terminal(const note_fx_event_t *event, void *context)
{
    (void)context;
    if (event == 0)
    {
        return;
    }
    seq_play_scheduler_dispatch_terminal_note_to_channel(
        event->track, event->destination, event->note, event->velocity,
        event->type == NOTE_FX_EVENT_ON);
}

static void note_fx_pipeline_sync_track(uint8_t track)
{
    note_fx_track_state_t state;
    if (note_fx_state_capture_track(track, &state) == 0U)
    {
        return;
    }
    uint8_t arp_slot = g_note_fx_runtime_arp_slot[track];
    if ((arp_slot >= NOTE_FX_SLOT_COUNT)
            || ((g_note_fx_override_valid[track][arp_slot][3] != 0U)
                && (g_note_fx_override_value[track][arp_slot][3] != NOTE_FX_MODEL_ARP)))
    {
        arp_slot = NOTE_FX_SLOT_NONE;
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        {
            const uint8_t model = g_note_fx_override_valid[track][slot][3]
                ? g_note_fx_override_value[track][slot][3]
                : state.value[slot][3];
            if (model == NOTE_FX_MODEL_ARP)
            {
                arp_slot = slot;
                break;
            }
        }
        g_note_fx_runtime_arp_slot[track] = arp_slot;
    }

    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        uint8_t value[NOTE_FX_PARAM_COUNT];
        for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT; ++param)
            value[param] = g_note_fx_override_valid[track][slot][param]
                ? g_note_fx_override_value[track][slot][param]
                : state.value[slot][param];
        if ((value[3] == NOTE_FX_MODEL_ARP) && (slot != arp_slot))
        {
            value[3] = NOTE_FX_MODEL_OFF;
        }
        note_fx_engine_configure(track, slot, value[3], value[0], value[1], value[2]);
    }
}

void note_fx_pipeline_init(void)
{
    memset(g_note_fx_override_valid, 0, sizeof(g_note_fx_override_valid));
    memset(g_note_fx_runtime_arp_slot, NOTE_FX_SLOT_NONE, sizeof(g_note_fx_runtime_arp_slot));
    note_fx_engine_init();
}

uint8_t note_fx_pipeline_apply_runtime_param(uint8_t track, uint8_t slot,
                                             uint8_t param, uint8_t value)
{
    if (track >= NOTE_FX_TRACK_COUNT || slot >= NOTE_FX_SLOT_COUNT ||
        param >= NOTE_FX_PARAM_COUNT) return 0U;
    note_fx_track_state_t state;
    if (note_fx_state_capture_track(track, &state) == 0U) return 0U;
    const uint8_t previous = g_note_fx_override_valid[track][slot][param]
        ? g_note_fx_override_value[track][slot][param] : state.value[slot][param];
    if (param == 3U && previous != value) note_fx_pipeline_cleanup_track(track);
    if (param == 3U && value == NOTE_FX_MODEL_ARP)
    {
        g_note_fx_runtime_arp_slot[track] = slot;
    }
    g_note_fx_override_valid[track][slot][param] = 1U;
    g_note_fx_override_value[track][slot][param] = value;
    note_fx_pipeline_sync_track(track);
    return 1U;
}

uint8_t note_fx_pipeline_release_runtime_param(uint8_t track, uint8_t slot,
                                               uint8_t param)
{
    if (track >= NOTE_FX_TRACK_COUNT || slot >= NOTE_FX_SLOT_COUNT ||
        param >= NOTE_FX_PARAM_COUNT) return 0U;
    note_fx_track_state_t state;
    if (note_fx_state_capture_track(track, &state) == 0U) return 0U;
    if (param == 3U && g_note_fx_override_valid[track][slot][param] != 0U &&
        g_note_fx_override_value[track][slot][param] != state.value[slot][param])
        note_fx_pipeline_cleanup_track(track);
    g_note_fx_override_valid[track][slot][param] = 0U;
    if ((param == 3U) && (g_note_fx_runtime_arp_slot[track] == slot))
    {
        g_note_fx_runtime_arp_slot[track] = NOTE_FX_SLOT_NONE;
    }
    note_fx_pipeline_sync_track(track);
    return 1U;
}

uint8_t note_fx_pipeline_submit(uint8_t track, uint8_t note, uint8_t velocity,
                                uint8_t is_note_on, uint64_t sample_time)
{
    note_fx_pipeline_sync_track(track);
    const note_fx_event_t event = {
        .sample = sample_time,
        .track = track,
        .note = note,
        .velocity = velocity,
        .destination = track_runtime_get_midi_channel_zero_based(track),
        .type = is_note_on ? NOTE_FX_EVENT_ON : NOTE_FX_EVENT_OFF
    };
    return note_fx_engine_source(&event, note_fx_pipeline_terminal, 0);
}

void note_fx_pipeline_cleanup_track(uint8_t track)
{
    note_fx_engine_cleanup(track, seq_runtime_exec_get_audio_timeline_sample(),
                           note_fx_pipeline_terminal, 0);
}

void note_fx_pipeline_cleanup_all(void)
{
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        note_fx_pipeline_cleanup_track(track);
}

void note_fx_pipeline_suspend_track(uint8_t track, uint8_t suspended)
{
    note_fx_engine_suspend(track, suspended,
                           seq_runtime_exec_get_audio_timeline_sample(),
                           note_fx_pipeline_terminal, 0);
}

void note_fx_pipeline_before_model_change(uint8_t track)
{
    note_fx_pipeline_cleanup_track(track);
}

void note_fx_pipeline_on_base_param_change(uint8_t track)
{
    if (track < NOTE_FX_TRACK_COUNT)
    {
        note_fx_pipeline_sync_track(track);
    }
}

void note_fx_pipeline_reset_runtime_overrides(uint8_t track)
{
    if (track >= NOTE_FX_TRACK_COUNT) return;
    note_fx_pipeline_cleanup_track(track);
    memset(g_note_fx_override_valid[track], 0, sizeof(g_note_fx_override_valid[track]));
    g_note_fx_runtime_arp_slot[track] = NOTE_FX_SLOT_NONE;
    note_fx_pipeline_sync_track(track);
}

void note_fx_pipeline_reset_all_runtime_overrides(void)
{
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        note_fx_pipeline_reset_runtime_overrides(track);
}

void note_fx_pipeline_process(uint64_t block_start, uint16_t frames,
                              uint32_t samples_per_step_q16)
{
    note_fx_engine_process(block_start, frames, samples_per_step_q16,
                           note_fx_pipeline_terminal, 0);
}

uint16_t note_fx_pipeline_frames_until_deadline(uint64_t block_start,
                                                uint16_t max_frames)
{
    const uint64_t deadline = note_fx_engine_next_deadline();
    if ((deadline <= block_start) || (deadline >= block_start + max_frames))
    {
        return max_frames;
    }
    return (uint16_t)(deadline - block_start);
}
