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
#include "Audio/drum_synth.h"
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
                midi_note_off(MIDI_DEST_BOTH, channel, note, 0U);
            }

            g_seq_output_guard.note_counts[track][note] = 0U;
        }
    }

    for (uint8_t ch = 0U; ch < 16U; ++ch)
    {
        midi_all_sound_off(MIDI_DEST_BOTH, ch);
        midi_all_notes_off(MIDI_DEST_BOTH, ch);
    }

    if (send_transport_stop != 0U)
    {
        midi_stop(MIDI_DEST_BOTH);
    }

    uint8_t drum_killed[SEQ_TRACK_COUNT] = { 0U };
    track_runtime_refresh_all();
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
        {
            continue;
        }

        if (ctx->mix_track_id < MIXER_MAX_TRACKS)
        {
            mixer_track_vca_all_notes_off(ctx->mix_track_id);
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        {
            if ((ctx->instance_id < SEQ_TRACK_COUNT) && (drum_killed[ctx->instance_id] == 0U))
            {
                drum_killed[ctx->instance_id] = 1U;
                drum_synth_all_notes_off_for_instance(ctx->instance_id);
            }
        }
    }
}
