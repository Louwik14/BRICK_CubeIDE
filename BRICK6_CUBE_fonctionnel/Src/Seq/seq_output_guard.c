#include "Seq/seq_output_guard.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "midi.h"
#include "ui_core.h"

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

uint8_t seq_output_guard_is_note_active_on_channel(uint8_t channel_zero_based, uint8_t note)
{
    if ((channel_zero_based >= 16U) || (note >= 128U))
    {
        return 0U;
    }

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const uint8_t track_ch_1_16 = ui_get_track_midi_channel(track);
        const uint8_t track_ch = (uint8_t)((track_ch_1_16 > 0U) ? (track_ch_1_16 - 1U) : 0U);
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
        const uint8_t channel_1_16 = ui_get_track_midi_channel(track);
        const uint8_t channel = (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);

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

    uint8_t monob_killed[8U] = { 0U };
    uint8_t dx7_killed = 0U;
    track_runtime_refresh_all();
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
        {
            continue;
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
        {
            if ((ctx->instance_id < 8U) && (monob_killed[ctx->instance_id] == 0U))
            {
                monob_killed[ctx->instance_id] = 1U;
                monob_synth_all_notes_off_for_instance(ctx->instance_id);
            }
        }
        else if ((ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7) && (dx7_killed == 0U))
        {
            dx7_killed = 1U;
            microdexed_synth_all_notes_off();
        }
    }
}
