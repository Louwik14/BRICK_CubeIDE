/*
 * Module: seq_output_guard
 * Role: Garde-fou de sorties note pour éviter les notes bloquées.
 * Responsibilities: compter notes actives par track, filtrer état canal,
 * fournir panic/cleanup vers synthés et MIDI lors des transitions transport.
 * Integration: utilisé par scheduler/runtime/live-rec; ne décide pas du contenu musical.
 */
#include "Seq/seq_output_guard.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "Audio/audio_control_command.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Audio/drum_synth.h"
#include "Audio/audio_midi_out.h"
#include "Audio/mixer.h"
#include "midi.h"

typedef struct
{
    uint8_t note_counts[SEQ_TRACK_COUNT][128U];
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

void seq_output_guard_note_on_seen(seq_track_id_t track, uint8_t note)
{
    if ((track >= SEQ_TRACK_COUNT) || (note >= 128U))
    {
        return;
    }

    if (g_seq_output_guard.note_counts[track][note] < 0xFFU)
    {
        g_seq_output_guard.note_counts[track][note]++;
    }
}

void seq_output_guard_note_off_seen(seq_track_id_t track, uint8_t note)
{
    if ((track >= SEQ_TRACK_COUNT) || (note >= 128U))
    {
        return;
    }

    if (g_seq_output_guard.note_counts[track][note] > 0U)
    {
        g_seq_output_guard.note_counts[track][note]--;
    }
}

uint8_t seq_output_guard_is_note_active_on_track(seq_track_id_t track, uint8_t note)
{
    if ((track >= SEQ_TRACK_COUNT) || (note >= 128U))
    {
        return 0U;
    }

    return (g_seq_output_guard.note_counts[track][note] > 0U) ? 1U : 0U;
}

uint8_t seq_output_guard_is_note_active_on_channel(uint8_t channel_zero_based, uint8_t note)
{
    if ((channel_zero_based >= 16U) || (note >= 128U))
    {
        return 0U;
    }

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const uint8_t track_ch = track_runtime_get_midi_channel_zero_based(track);
        if (track_ch != channel_zero_based)
        {
            continue;
        }

        if (g_seq_output_guard.note_counts[track][note] > 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

void seq_output_guard_panic(uint8_t send_transport_stop)
{
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const uint8_t channel = track_runtime_get_midi_channel_zero_based(track);

        for (uint8_t note = 0U; note < 128U; ++note)
        {
            const uint8_t count = g_seq_output_guard.note_counts[track][note];
            if (count == 0U)
            {
                continue;
            }

            for (uint8_t i = 0U; i < count; ++i)
            {
                (void)audio_midi_out_note_off(channel, note, 0U, 0U);
            }

            g_seq_output_guard.note_counts[track][note] = 0U;
        }
    }

    for (uint8_t ch = 0U; ch < 16U; ++ch)
    {
        (void)audio_midi_out_submit_raw((uint8_t)(0xB0U | (ch & 0x0FU)),
                                        120U,
                                        0U,
                                        3U,
                                        0U,
                                        AUDIO_MIDI_OUT_PRIORITY_CRITICAL);
        (void)audio_midi_out_all_notes_off(ch, 0U);
    }

    if (send_transport_stop != 0U)
    {
        (void)audio_midi_out_stop(0U);
    }

    uint8_t drum_killed[SEQ_TRACK_COUNT] = { 0U };
    track_runtime_refresh_all();
    audio_control_command_submit_sampler_param(0U,
                                               AUDIO_CONTROL_SAMPLER_STOP_TRANSPORT_CLIPS,
                                               0.0f,
                                               0U);
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

        if (resolved.mix_track_id < MIXER_MAX_TRACKS)
        {
            audio_control_command_submit_mixer_vca(resolved.mix_track_id,
                                                   AUDIO_CONTROL_VCA_ALL_NOTES_OFF,
                                                   0.0f);
        }

        if (resolved.descriptor.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        {
            if ((resolved.descriptor.instance_id < SEQ_TRACK_COUNT)
                    && (drum_killed[resolved.descriptor.instance_id] == 0U))
            {
                drum_killed[resolved.descriptor.instance_id] = 1U;
                audio_control_command_submit_drum_note(resolved.descriptor.instance_id,
                                                       AUDIO_CONTROL_NOTE_ALL_OFF,
                                                       0U,
                                                       0U);
            }
        }
        else if (resolved.descriptor.engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
        {
            audio_control_command_submit_wave_note(resolved.descriptor.instance_id,
                                                   AUDIO_CONTROL_NOTE_ALL_OFF,
                                                   0U,
                                                   0U);
        }
    }
}
