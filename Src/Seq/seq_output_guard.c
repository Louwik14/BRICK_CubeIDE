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
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/synth_polyphony.h"
#include "Mod/mod_lfo_v1.h"
#include "Audio/drum_synth.h"
#include "Audio/mixer.h"
#include "midi.h"
#include "NoteFx/note_fx_pipeline.h"

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
    seq_output_guard_record_t record[TRACK_TOPOLOGY_TRACK_COUNT][SEQ_OUTPUT_GUARD_MAX_OCCURRENCES];
    uint32_t orphan_off_count;
    uint32_t duplicate_close_count;
} seq_output_guard_state_t;

static seq_output_guard_state_t g_seq_output_guard;

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
    if ((track >= TRACK_TOPOLOGY_TRACK_COUNT) || (note >= 128U)
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
    if ((track >= TRACK_TOPOLOGY_TRACK_COUNT) || (note >= 128U)
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
    if ((track >= TRACK_TOPOLOGY_TRACK_COUNT) || (note >= 128U))
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
    for (seq_track_id_t track = 0U; track < TRACK_TOPOLOGY_TRACK_COUNT; ++track)
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
    synth_polyphony_panic();
    for (seq_track_id_t track = 0U; track < TRACK_TOPOLOGY_TRACK_COUNT; ++track)
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
    {
        midi_stop(MIDI_DEST_BOTH);
    }

    uint8_t drum_killed[SEQ_TRACK_COUNT] = { 0U };
    track_runtime_refresh_all();
    brick6_sampler_runtime_stop_transport_clips();
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        /* Non-UI projection consumer: panic only needs resolved routing/engine state. */
        track_runtime_resolved_track_t resolved;
        if (track_runtime_resolve_track(track, &resolved) == 0U)
        {
            continue;
        }

        if (resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
        {
            continue;
        }

        const uint8_t poly_count = synth_polyphony_get_voice_count(track);
        const uint8_t is_poly_synth = (uint8_t)((poly_count > 1U)
            && ((resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_PRISM)
                || (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_STACK)
                || (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_WAVE)));
        if (is_poly_synth != 0U)
        {
            synth_poly_release_t released[SYNTH_POLYPHONY_MAX_VOICES];
            const uint8_t released_count =
                synth_polyphony_release_source(track,
                                               SYNTH_POLY_SOURCE_SEQUENCER,
                                               released,
                                               SYNTH_POLYPHONY_MAX_VOICES);
            for (uint8_t i = 0U; i < released_count; ++i)
            {
                const uint8_t voice = released[i].voice;
                const uint8_t note = released[i].note;
                const uint8_t instance = SYNTH_POLYPHONY_INSTANCE(track, voice);
                if (resolved.has_mix_target != 0U)
                {
                    mixer_track_poly_note_off(track, voice, note);
                }
                if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_PRISM)
                {
                    brick6_braids_runtime_note_off(instance, note);
                }
                else if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_STACK)
                {
                    brick6_stack_runtime_note_off(instance, note);
                }
                else if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_WAVE)
                {
                    brick6_wave_runtime_note_off(instance, note);
                }
            }
            continue;
        }

        if (resolved.mix_track_id < MIXER_MAX_TRACKS)
        {
            mixer_track_vca_all_notes_off(resolved.mix_track_id);
        }
        mod_lfo_v1_all_notes_off(track);

        if (resolved.descriptor.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        {
            if ((resolved.descriptor.instance_id < SEQ_TRACK_COUNT)
                    && (drum_killed[resolved.descriptor.instance_id] == 0U))
            {
                drum_killed[resolved.descriptor.instance_id] = 1U;
                drum_synth_all_notes_off_for_instance(resolved.descriptor.instance_id);
            }
        }
        else if (resolved.descriptor.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
        {
            brick6_braids_runtime_all_notes_off(resolved.descriptor.instance_id);
        }
        else if (resolved.descriptor.engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
        {
            (void)brick6_stack_runtime_submit_all_notes_off(resolved.descriptor.instance_id);
        }
        else if (resolved.descriptor.engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
        {
            brick6_wave_runtime_all_notes_off(resolved.descriptor.instance_id);
        }
    }
    seq_play_scheduler_terminal_reset();
}
