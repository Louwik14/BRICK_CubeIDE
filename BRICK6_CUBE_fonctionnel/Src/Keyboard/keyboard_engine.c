/******************************************************************************
 * @file    keyboard_engine.c
 * @brief   Moteur de sortie des notes du clavier.
 *
 * Ce module centralise l’émission réelle des notes :
 * - envoi MIDI
 * - déclenchement des synthés internes
 * - routage éventuel vers le filtre / mixer
 * - extinction globale des notes
 *
 * Il sert de couche de sortie unique pour les autres modules, notamment
 * l’arpégiateur et les entrées clavier directes.
 ******************************************************************************/

#include "Keyboard/keyboard_engine.h"

#include "Audio/microdexed_synth.h"
#include "Audio/mixer.h"
#include "Audio/monob_synth.h"
#include "Audio/drum_synth.h"
#include "Audio/tb3_synth.h"
#include "MIDI/midi.h"
#include "ui_core.h"
#include "Core/track_runtime.h"
#include "Seq/seq_runtime.h"
#include <string.h>

static bool g_keyboard_engine_sounding_active = false;
static uint8_t g_keyboard_engine_sounding_engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
static uint8_t g_keyboard_engine_sounding_monob_instance = 0U;
static uint8_t g_keyboard_engine_sounding_tb3_instance = 0U;
static uint8_t g_keyboard_engine_sounding_drum_instance = 0U;

#define KBD_REC_NOTE_STACK_DEPTH 8U
static uint8_t g_kbd_rec_note_stack_ch[128U][KBD_REC_NOTE_STACK_DEPTH];
static uint8_t g_kbd_rec_note_stack_count[128U];

static bool keyboard_engine_active_track_is_synth(void)
{
    return (ui_track_family_is_engine(ui_get_track_family(ui_get_active_track())) != 0);
}

static bool keyboard_engine_active_track_has_midi_note_path(void)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_config_t config = ui_get_track_config(active_track);
    return (ui_track_family_is_engine(config.family) != 0) || (config.type == UI_TRACK_TYPE_HYBRID);
}

static bool keyboard_engine_active_track_accepts_internal_source(void)
{
    const ui_track_midi_source_t source = ui_get_track_midi_source(ui_get_active_track());
    return (source == UI_TRACK_MIDI_SRC_INT) || (source == UI_TRACK_MIDI_SRC_ALL);
}

static uint8_t keyboard_engine_get_filter_target_track(void)
{
    uint8_t track_id = 0U;
    if (ui_resolve_filter_target_track(&track_id))
    {
        return track_id;
    }
    return 0xFFU;
}

static uint8_t keyboard_engine_get_active_mix_target_track(void)
{
    const uint8_t active_track = ui_get_active_track();
    uint8_t mix_track = 0U;
    track_runtime_refresh_track(active_track);
    if (track_runtime_get_mix_target_track(active_track, &mix_track) != 0U)
    {
        return mix_track;
    }
    return 0xFFU;
}

static uint8_t keyboard_engine_get_active_monob_instance(void)
{
    const uint8_t active_track = ui_get_active_track();
    track_runtime_refresh_track(active_track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(active_track);
    if ((ctx == NULL)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    return ctx->instance_id;
}

static uint8_t keyboard_engine_get_active_tb3_instance(void)
{
    const uint8_t active_track = ui_get_active_track();
    track_runtime_refresh_track(active_track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(active_track);
    if ((ctx == NULL)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_TB3)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    return ctx->instance_id;
}

static uint8_t keyboard_engine_get_track_midi_channel_zero_based(uint8_t track)
{
    const uint8_t channel_1_16 = ui_get_track_midi_channel(track);
    return (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);
}

static void keyboard_engine_dispatch_note_to_matching_tracks(uint8_t channel,
                                                             uint8_t note,
                                                             uint8_t velocity,
                                                             uint8_t source_internal,
                                                             uint8_t is_note_on)
{
    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (ui_track_family_is_engine(ui_get_track_family(track)) == 0)
        {
            continue;
        }

        const ui_track_midi_source_t source = ui_get_track_midi_source(track);
        if (source_internal != 0U)
        {
            if ((source != UI_TRACK_MIDI_SRC_INT) && (source != UI_TRACK_MIDI_SRC_ALL))
            {
                continue;
            }
        }
        else
        {
            if ((source != UI_TRACK_MIDI_SRC_EXT) && (source != UI_TRACK_MIDI_SRC_ALL))
            {
                continue;
            }
        }

        if (keyboard_engine_get_track_midi_channel_zero_based(track) != channel)
        {
            continue;
        }

        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
        {
            continue;
        }

        uint8_t filter_track = 0U;
        uint8_t mix_track = 0U;
        if (track_runtime_resolve_filter_target_track(track, &filter_track) != 0U)
        {
            if (is_note_on != 0U)
            {
                mixer_track_filter_note_on(filter_track, note, velocity);
            }
            else
            {
                mixer_track_filter_note_off(filter_track, note);
            }
        }
        if (track_runtime_get_mix_target_track(track, &mix_track) != 0U)
        {
            if (is_note_on != 0U)
            {
                mixer_track_vca_note_on(mix_track, note, velocity);
            }
            else
            {
                mixer_track_vca_note_off(mix_track, note);
            }
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
        {
            if (is_note_on != 0U)
            {
                monob_synth_note_on_for_instance(ctx->instance_id, note, velocity);
            }
            else
            {
                monob_synth_note_off_for_instance(ctx->instance_id, note);
            }
        }
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_TB3)
        {
            if (is_note_on != 0U)
            {
                tb3_synth_note_on_for_instance(ctx->instance_id, note, velocity);
            }
            else
            {
                tb3_synth_note_off_for_instance(ctx->instance_id, note);
            }
        }
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        {
            if (is_note_on != 0U)
            {
                drum_synth_note_on_for_instance(ctx->instance_id, note, velocity);
            }
            else
            {
                drum_synth_note_off_for_instance(ctx->instance_id, note);
            }
        }
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
        {
            if (is_note_on != 0U)
            {
                microdexed_synth_note_on(note, velocity);
            }
            else
            {
                microdexed_synth_note_off(note);
            }
        }
    }
}

static void keyboard_engine_live_rec_push_internal_channel(uint8_t note, uint8_t channel)
{
    if (note >= 128U)
    {
        return;
    }

    uint8_t count = g_kbd_rec_note_stack_count[note];
    if (count >= KBD_REC_NOTE_STACK_DEPTH)
    {
        for (uint8_t i = 1U; i < KBD_REC_NOTE_STACK_DEPTH; ++i)
        {
            g_kbd_rec_note_stack_ch[note][i - 1U] = g_kbd_rec_note_stack_ch[note][i];
        }
        count = (uint8_t)(KBD_REC_NOTE_STACK_DEPTH - 1U);
    }

    g_kbd_rec_note_stack_ch[note][count] = channel;
    g_kbd_rec_note_stack_count[note] = (uint8_t)(count + 1U);
}

static uint8_t keyboard_engine_live_rec_pop_internal_channel(uint8_t note, uint8_t fallback_channel)
{
    if (note >= 128U)
    {
        return fallback_channel;
    }

    const uint8_t count = g_kbd_rec_note_stack_count[note];
    if (count == 0U)
    {
        return fallback_channel;
    }

    const uint8_t index = (uint8_t)(count - 1U);
    const uint8_t channel = g_kbd_rec_note_stack_ch[note][index];
    g_kbd_rec_note_stack_count[note] = index;
    return channel;
}

void keyboard_engine_note_on(uint8_t note, uint8_t velocity)
{
    const uint8_t active_channel = keyboard_engine_get_track_midi_channel_zero_based(ui_get_active_track());
    keyboard_engine_live_rec_push_internal_channel(note, active_channel);
    seq_runtime_live_rec_note_on(SEQ_LIVE_REC_SRC_INTERNAL, active_channel, note, velocity);

    const uint8_t filter_track = keyboard_engine_get_filter_target_track();
    const uint8_t mix_track = keyboard_engine_get_active_mix_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_note_on(filter_track, note, velocity);
    }
    if (mix_track != 0xFFU)
    {
        mixer_track_vca_note_on(mix_track, note, velocity);
    }

    if (keyboard_engine_active_track_has_midi_note_path())
    {
        midi_note_on(MIDI_DEST_USB, keyboard_engine_get_track_midi_channel_zero_based(ui_get_active_track()), note, velocity);
    }

    if ((seq_runtime_rec_is_armed() != 0U) && (seq_runtime_is_running() != 0U))
    {
        /*
         * During live-rec monitoring, dispatch through track-matching routing
         * (same spirit as playback/external paths) to avoid active-track-only
         * destructive behavior differences versus sequencer playback.
         */
        keyboard_engine_dispatch_note_to_matching_tracks(active_channel, note, velocity, 1U, 1U);
        return;
    }

    if (!keyboard_engine_active_track_is_synth())
    {
        return;
    }

    if (!keyboard_engine_active_track_accepts_internal_source())
    {
        return;
    }

    const uint8_t active_track = ui_get_active_track();
    track_runtime_refresh_track(active_track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(active_track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return;
    }

    g_keyboard_engine_sounding_active = true;
    g_keyboard_engine_sounding_engine = ctx->engine;

    if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
    {
        g_keyboard_engine_sounding_monob_instance = keyboard_engine_get_active_monob_instance();
        monob_synth_note_on_for_instance(g_keyboard_engine_sounding_monob_instance, note, velocity);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_TB3)
    {
        g_keyboard_engine_sounding_tb3_instance = keyboard_engine_get_active_tb3_instance();
        tb3_synth_note_on_for_instance(g_keyboard_engine_sounding_tb3_instance, note, velocity);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        g_keyboard_engine_sounding_drum_instance = ctx->instance_id;
        drum_synth_note_on_for_instance(g_keyboard_engine_sounding_drum_instance, note, velocity);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
    {
        microdexed_synth_note_on(note, velocity);
    }
}

void keyboard_engine_note_off(uint8_t note)
{
    const uint8_t active_channel = keyboard_engine_get_track_midi_channel_zero_based(ui_get_active_track());
    const uint8_t note_on_channel = keyboard_engine_live_rec_pop_internal_channel(note, active_channel);
    seq_runtime_live_rec_note_off(SEQ_LIVE_REC_SRC_INTERNAL, note_on_channel, note);

    const uint8_t filter_track = keyboard_engine_get_filter_target_track();
    const uint8_t mix_track = keyboard_engine_get_active_mix_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_note_off(filter_track, note);
    }
    if (mix_track != 0xFFU)
    {
        mixer_track_vca_note_off(mix_track, note);
    }

    if (keyboard_engine_active_track_has_midi_note_path())
    {
        midi_note_off(MIDI_DEST_USB, keyboard_engine_get_track_midi_channel_zero_based(ui_get_active_track()), note, 0U);
    }

    if ((seq_runtime_rec_is_armed() != 0U) && (seq_runtime_is_running() != 0U))
    {
        keyboard_engine_dispatch_note_to_matching_tracks(note_on_channel, note, 0U, 1U, 0U);
        return;
    }

    if (!keyboard_engine_active_track_is_synth() && !g_keyboard_engine_sounding_active)
    {
        return;
    }

    uint8_t sounding_engine = g_keyboard_engine_sounding_active
                            ? g_keyboard_engine_sounding_engine
                            : (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    uint8_t sounding_instance = 0U;
    if (g_keyboard_engine_sounding_active)
    {
        sounding_instance = g_keyboard_engine_sounding_drum_instance;
    }
    else
    {
        const uint8_t active_track = ui_get_active_track();
        track_runtime_refresh_track(active_track);
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(active_track);
        if ((ctx != NULL) && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND))
        {
            sounding_engine = ctx->engine;
            sounding_instance = ctx->instance_id;
        }
    }

    if (sounding_engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
    {
        monob_synth_note_off_for_instance(g_keyboard_engine_sounding_monob_instance, note);
    }
    else if (sounding_engine == (uint8_t)TRACK_RUNTIME_ENGINE_TB3)
    {
        tb3_synth_note_off_for_instance(g_keyboard_engine_sounding_tb3_instance, note);
    }
    else if (sounding_engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        drum_synth_note_off_for_instance(sounding_instance, note);
    }
    else
    {
        microdexed_synth_note_off(note);
    }
}

void keyboard_engine_all_notes_off(void)
{
    microdexed_synth_all_notes_off();
    monob_synth_all_notes_off_all();
    tb3_synth_all_notes_off_all();
    drum_synth_all_notes_off_all();

    const uint8_t filter_track = keyboard_engine_get_filter_target_track();
    const uint8_t mix_track = keyboard_engine_get_active_mix_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_all_notes_off(filter_track);
    }
    if (mix_track != 0xFFU)
    {
        mixer_track_vca_all_notes_off(mix_track);
    }

    if (keyboard_engine_active_track_has_midi_note_path())
    {
        midi_all_notes_off(MIDI_DEST_USB, keyboard_engine_get_track_midi_channel_zero_based(ui_get_active_track()));
    }

    g_keyboard_engine_sounding_active = false;
    g_keyboard_engine_sounding_engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    g_keyboard_engine_sounding_monob_instance = 0U;
    g_keyboard_engine_sounding_tb3_instance = 0U;
    g_keyboard_engine_sounding_drum_instance = 0U;
    memset(g_kbd_rec_note_stack_count, 0, sizeof(g_kbd_rec_note_stack_count));
}

void keyboard_engine_midi_receive(const uint8_t *msg, size_t len)
{
    if ((msg == NULL) || (len < 2U))
    {
        return;
    }

    const uint8_t status = msg[0];
    const uint8_t type = status & 0xF0U;
    const uint8_t channel = status & 0x0FU;
    const uint8_t data1 = msg[1] & 0x7FU;
    const uint8_t data2 = (len >= 3U) ? (msg[2] & 0x7FU) : 0U;

    const uint8_t is_note_msg = ((type == 0x90U) || (type == 0x80U)) ? 1U : 0U;
    const uint8_t is_cc_msg = (type == 0xB0U) ? 1U : 0U;
    if ((is_note_msg == 0U) && (is_cc_msg == 0U))
    {
        return;
    }

    const uint8_t note = data1;
    const uint8_t velocity = data2;
    const uint8_t cc = data1;
    const uint8_t is_note_on = ((type == 0x90U) && (velocity > 0U)) ? 1U : 0U;
    const uint8_t is_note_off = ((type == 0x80U) || ((type == 0x90U) && (velocity == 0U))) ? 1U : 0U;
    const uint8_t is_all_notes_off = ((is_cc_msg != 0U) && ((cc == 123U) || (cc == 120U))) ? 1U : 0U;

    if (is_note_on != 0U)
    {
        seq_runtime_live_rec_note_on(SEQ_LIVE_REC_SRC_EXTERNAL, channel, note, velocity);
    }
    else if (is_note_off != 0U)
    {
        seq_runtime_live_rec_note_off(SEQ_LIVE_REC_SRC_EXTERNAL, channel, note);
    }

    uint8_t dx7_hit = 0U;
    uint8_t dx7_panic_hit = 0U;
    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (ui_track_family_is_engine(ui_get_track_family(track)) == 0)
        {
            continue;
        }
        const ui_track_midi_source_t source = ui_get_track_midi_source(track);
        if ((source != UI_TRACK_MIDI_SRC_EXT) && (source != UI_TRACK_MIDI_SRC_ALL))
        {
            continue;
        }
        if (keyboard_engine_get_track_midi_channel_zero_based(track) != channel)
        {
            continue;
        }

        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
        {
            continue;
        }

        uint8_t filter_track = 0U;
        uint8_t mix_track = 0U;
        if (track_runtime_resolve_filter_target_track(track, &filter_track) != 0U)
        {
            if (is_note_on != 0U)
            {
                mixer_track_filter_note_on(filter_track, note, velocity);
            }
            else if (is_note_off != 0U)
            {
                mixer_track_filter_note_off(filter_track, note);
            }
            else if (is_all_notes_off != 0U)
            {
                mixer_track_filter_all_notes_off(filter_track);
            }
        }
        if (track_runtime_get_mix_target_track(track, &mix_track) != 0U)
        {
            if (is_note_on != 0U)
            {
                mixer_track_vca_note_on(mix_track, note, velocity);
            }
            else if (is_note_off != 0U)
            {
                mixer_track_vca_note_off(mix_track, note);
            }
            else if (is_all_notes_off != 0U)
            {
                mixer_track_vca_all_notes_off(mix_track);
            }
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
        {
            if (is_note_on != 0U)
            {
                monob_synth_note_on_for_instance(ctx->instance_id, note, velocity);
            }
            else if (is_note_off != 0U)
            {
                monob_synth_note_off_for_instance(ctx->instance_id, note);
            }
            else if (is_all_notes_off != 0U)
            {
                monob_synth_all_notes_off_for_instance(ctx->instance_id);
            }
        }
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_TB3)
        {
            if (is_note_on != 0U)
            {
                tb3_synth_note_on_for_instance(ctx->instance_id, note, velocity);
            }
            else if (is_note_off != 0U)
            {
                tb3_synth_note_off_for_instance(ctx->instance_id, note);
            }
            else if (is_all_notes_off != 0U)
            {
                tb3_synth_all_notes_off_for_instance(ctx->instance_id);
            }
        }
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
        {
            if ((is_note_on != 0U) || (is_note_off != 0U))
            {
                dx7_hit = 1U;
            }
            else if (is_all_notes_off != 0U)
            {
                dx7_panic_hit = 1U;
            }
        }
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        {
            if (is_note_on != 0U)
            {
                drum_synth_note_on_for_instance(ctx->instance_id, note, velocity);
            }
            else if (is_note_off != 0U)
            {
                drum_synth_note_off_for_instance(ctx->instance_id, note);
            }
            else if (is_all_notes_off != 0U)
            {
                drum_synth_all_notes_off_for_instance(ctx->instance_id);
            }
        }
    }

    if (dx7_hit != 0U)
    {
        if (is_note_on != 0U)
        {
            microdexed_synth_note_on(note, velocity);
        }
        else if (is_note_off != 0U)
        {
            microdexed_synth_note_off(note);
        }
    }

    if (dx7_panic_hit != 0U)
    {
        microdexed_synth_all_notes_off();
    }
}
