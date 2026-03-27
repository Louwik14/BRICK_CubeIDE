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
#include "MIDI/midi.h"
#include "ui_core.h"
#include "Core/track_runtime.h"

static ui_track_type_t g_keyboard_engine_sounding_type = UI_TRACK_TYPE_DX7;
static bool g_keyboard_engine_sounding_active = false;
static uint8_t g_keyboard_engine_sounding_monob_instance = 0U;

static bool keyboard_engine_active_track_is_synth(void)
{
    return (ui_get_track_family(ui_get_active_track()) == UI_TRACK_FAMILY_SYNTH);
}

static bool keyboard_engine_active_track_has_midi_note_path(void)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_config_t config = ui_get_track_config(active_track);
    return (config.family == UI_TRACK_FAMILY_SYNTH) || (config.type == UI_TRACK_TYPE_HYBRID);
}

static bool keyboard_engine_active_track_accepts_internal_source(void)
{
    const ui_track_midi_source_t source = ui_get_track_midi_source(ui_get_active_track());
    return (source == UI_TRACK_MIDI_SRC_INT) || (source == UI_TRACK_MIDI_SRC_ALL);
}

static ui_track_type_t keyboard_engine_get_active_synth_type(void)
{
    return ui_get_track_type(ui_get_active_track());
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

static uint8_t keyboard_engine_get_track_midi_channel_zero_based(uint8_t track)
{
    const uint8_t channel_1_16 = ui_get_track_midi_channel(track);
    return (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);
}

void keyboard_engine_note_on(uint8_t note, uint8_t velocity)
{
    const uint8_t filter_track = keyboard_engine_get_filter_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_note_on(filter_track, note, velocity);
    }

    if (keyboard_engine_active_track_has_midi_note_path())
    {
        midi_note_on(MIDI_DEST_USB, keyboard_engine_get_track_midi_channel_zero_based(ui_get_active_track()), note, velocity);
    }

    if (!keyboard_engine_active_track_is_synth())
    {
        return;
    }

    if (!keyboard_engine_active_track_accepts_internal_source())
    {
        return;
    }

    const ui_track_type_t synth_type = keyboard_engine_get_active_synth_type();
    g_keyboard_engine_sounding_type = synth_type;
    g_keyboard_engine_sounding_active = true;

    if (synth_type == UI_TRACK_TYPE_MONOB)
    {
        g_keyboard_engine_sounding_monob_instance = keyboard_engine_get_active_monob_instance();
        monob_synth_note_on_for_instance(g_keyboard_engine_sounding_monob_instance, note, velocity);
    }
    else
    {
        microdexed_synth_note_on(note, velocity);
    }
}

void keyboard_engine_note_off(uint8_t note)
{
    const uint8_t filter_track = keyboard_engine_get_filter_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_note_off(filter_track, note);
    }

    if (keyboard_engine_active_track_has_midi_note_path())
    {
        midi_note_off(MIDI_DEST_USB, keyboard_engine_get_track_midi_channel_zero_based(ui_get_active_track()), note, 0U);
    }

    if (!keyboard_engine_active_track_is_synth() && !g_keyboard_engine_sounding_active)
    {
        return;
    }

    const ui_track_type_t synth_type = g_keyboard_engine_sounding_active
                                     ? g_keyboard_engine_sounding_type
                                     : keyboard_engine_get_active_synth_type();

    if (synth_type == UI_TRACK_TYPE_MONOB)
    {
        monob_synth_note_off_for_instance(g_keyboard_engine_sounding_monob_instance, note);
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

    const uint8_t filter_track = keyboard_engine_get_filter_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_all_notes_off(filter_track);
    }

    if (keyboard_engine_active_track_has_midi_note_path())
    {
        midi_all_notes_off(MIDI_DEST_USB, keyboard_engine_get_track_midi_channel_zero_based(ui_get_active_track()));
    }

    g_keyboard_engine_sounding_active = false;
    g_keyboard_engine_sounding_monob_instance = 0U;
}

void keyboard_engine_midi_receive(const uint8_t *msg, size_t len)
{
    if ((msg == NULL) || (len < 3U))
    {
        return;
    }

    const uint8_t status = msg[0];
    const uint8_t type = status & 0xF0U;
    const uint8_t channel = status & 0x0FU;
    const uint8_t note = msg[1] & 0x7FU;
    const uint8_t velocity = msg[2] & 0x7FU;

    if ((type != 0x90U) && (type != 0x80U))
    {
        return;
    }

    const uint8_t is_note_on = ((type == 0x90U) && (velocity > 0U)) ? 1U : 0U;
    const uint8_t is_note_off = (is_note_on == 0U) ? 1U : 0U;

    uint8_t dx7_hit = 0U;
    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (ui_get_track_family(track) != UI_TRACK_FAMILY_SYNTH)
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
        }
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
        {
            dx7_hit = 1U;
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
}
