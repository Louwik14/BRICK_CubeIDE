/*
 * Module: seq_output_guard
 * Role: Garde-fou de sorties note pour éviter les notes bloquées.
 * Responsibilities: compter notes actives par track, filtrer état canal,
 * fournir panic/cleanup vers synthés et MIDI lors des transitions transport.
 * Integration: utilisé par scheduler/runtime/live-rec; ne décide pas du contenu musical.
 */
#include "Seq/seq_output_guard.h"

#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "midi.h"
#include "NoteFx/note_fx_pipeline.h"
#include "Storage/memory_layout.h"

typedef struct
{
    uint8_t active;
    uint8_t note;
    uint8_t midi_dest_mask;
    uint16_t reserved;
    uint32_t occurrence_id;
    uint32_t generation;
} seq_output_guard_record_t;

typedef struct
{
    seq_output_guard_record_t record[SEQ_LANE_CAPACITY][SEQ_OUTPUT_GUARD_MAX_OCCURRENCES];
    uint32_t orphan_off_count;
    uint32_t duplicate_close_count;
} seq_output_guard_state_t;

SEQ_STATE_D2 static seq_output_guard_state_t g_seq_output_guard;

void seq_output_guard_init(void)
{
    memset(&g_seq_output_guard, 0, sizeof(g_seq_output_guard));
}

void seq_output_guard_reset(void)
{
    memset(&g_seq_output_guard, 0, sizeof(g_seq_output_guard));
}

uint8_t seq_output_guard_note_on_seen(seq_track_id_t track, uint8_t note,
                                      uint32_t occurrence_id, uint32_t generation)
{
    return seq_output_guard_note_on_seen_mask(track, note, occurrence_id,
                                              generation,
                                              (uint8_t)(SEQ_OUTPUT_GUARD_MIDI_UART
                                                        | SEQ_OUTPUT_GUARD_MIDI_USB));
}

uint8_t seq_output_guard_note_on_seen_mask(seq_track_id_t track, uint8_t note,
                                           uint32_t occurrence_id, uint32_t generation,
                                           uint8_t midi_dest_mask)
{
    if ((track >= SEQ_LANE_CAPACITY) || (note >= 128U)
            || (occurrence_id == 0U) || (generation == 0U))
        return 0U;

    for (uint8_t i = 0U; i < SEQ_OUTPUT_GUARD_MAX_OCCURRENCES; ++i)
    {
        seq_output_guard_record_t *const record = &g_seq_output_guard.record[track][i];
        if ((record->active != 0U) && (record->occurrence_id == occurrence_id)
                && (record->generation == generation))
        {
            ++g_seq_output_guard.duplicate_close_count;
            return 1U;
        }
    }
    for (uint8_t i = 0U; i < SEQ_OUTPUT_GUARD_MAX_OCCURRENCES; ++i)
    {
        seq_output_guard_record_t *const record = &g_seq_output_guard.record[track][i];
        if (record->active == 0U)
        {
            *record = (seq_output_guard_record_t){
                .active = 1U,
                .note = note,
                .midi_dest_mask = midi_dest_mask,
                .occurrence_id = occurrence_id,
                .generation = generation
            };
            return 1U;
        }
    }
    return 0U;
}

uint8_t seq_output_guard_note_off_seen(seq_track_id_t track, uint8_t note,
                                       uint32_t occurrence_id, uint32_t generation)
{
    if ((track >= SEQ_LANE_CAPACITY) || (note >= 128U)
            || (occurrence_id == 0U) || (generation == 0U))
        return 0U;

    for (uint8_t i = 0U; i < SEQ_OUTPUT_GUARD_MAX_OCCURRENCES; ++i)
    {
        seq_output_guard_record_t *const record = &g_seq_output_guard.record[track][i];
        if ((record->active != 0U) && (record->note == note)
                && (record->occurrence_id == occurrence_id)
                && (record->generation == generation))
        {
            record->active = 0U;
            return 1U;
        }
    }
    ++g_seq_output_guard.orphan_off_count;
    return 0U;
}
uint8_t seq_output_guard_is_note_active_on_track(seq_track_id_t track, uint8_t note)
{
    if ((track >= SEQ_LANE_CAPACITY) || (note >= 128U))
        return 0U;
    for (uint8_t i = 0U; i < SEQ_OUTPUT_GUARD_MAX_OCCURRENCES; ++i)
    {
        const seq_output_guard_record_t *const record = &g_seq_output_guard.record[track][i];
        if ((record->active != 0U) && (record->note == note))
            return 1U;
    }
    return 0U;
}

uint8_t seq_output_guard_is_note_active_on_channel(uint8_t channel_zero_based, uint8_t note)
{
    if ((channel_zero_based >= 16U) || (note >= 128U))
        return 0U;
    for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
    {
        if (track_runtime_get_midi_channel_zero_based(track) != channel_zero_based)
            continue;
        if (seq_output_guard_is_note_active_on_track(track, note) != 0U)
            return 1U;
    }
    return 0U;
}
void seq_output_guard_panic(uint8_t send_transport_stop)
{
    if (seq_play_scheduler_transition_all(
            SEQ_PLAY_TRANSITION_PANIC_CLOSE_ALL) == 0U)
        return;
    for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
    {
        const uint8_t channel = track_runtime_get_midi_channel_zero_based(track);
        for (uint8_t i = 0U; i < SEQ_OUTPUT_GUARD_MAX_OCCURRENCES; ++i)
        {
            seq_output_guard_record_t *const record = &g_seq_output_guard.record[track][i];
            if (record->active == 0U)
                continue;
            if ((record->midi_dest_mask & SEQ_OUTPUT_GUARD_MIDI_UART) != 0U)
                midi_note_off_admit(MIDI_DEST_UART, channel, record->note, 0U);
            if ((record->midi_dest_mask & SEQ_OUTPUT_GUARD_MIDI_USB) != 0U)
                midi_note_off_admit(MIDI_DEST_USB, channel, record->note, 0U);
            record->active = 0U;
        }
    }
    if (send_transport_stop != 0U)
        midi_stop(MIDI_DEST_BOTH);
}
