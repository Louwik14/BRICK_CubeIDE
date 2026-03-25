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

#define KBD_ARP_MIDI_CHANNEL 0U

static ui_track_type_t g_keyboard_engine_sounding_type = UI_TRACK_TYPE_DX7;
static bool g_keyboard_engine_sounding_active = false;

static bool keyboard_engine_active_track_is_synth(void)
{
    return (ui_get_track_family(ui_get_active_track()) == UI_TRACK_FAMILY_SYNTH);
}

static bool keyboard_engine_active_track_has_midi_note_path(void)
{
    const ui_track_config_t config = ui_get_track_config(ui_get_active_track());
    return (config.family == UI_TRACK_FAMILY_SYNTH) || (config.type == UI_TRACK_TYPE_HYBRID);
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

void keyboard_engine_note_on(uint8_t note, uint8_t velocity)
{
    const uint8_t filter_track = keyboard_engine_get_filter_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_note_on(filter_track, note, velocity);
    }

    if (!keyboard_engine_active_track_has_midi_note_path())
    {
        return;
    }

    midi_note_on(MIDI_DEST_USB, KBD_ARP_MIDI_CHANNEL, note, velocity);

    if (!keyboard_engine_active_track_is_synth())
    {
        return;
    }

    const ui_track_type_t synth_type = keyboard_engine_get_active_synth_type();
    g_keyboard_engine_sounding_type = synth_type;
    g_keyboard_engine_sounding_active = true;

    if (synth_type == UI_TRACK_TYPE_MONOB)
    {
        monob_synth_note_on(note, velocity);
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
        midi_note_off(MIDI_DEST_USB, KBD_ARP_MIDI_CHANNEL, note, 0U);
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
        monob_synth_note_off(note);
    }
    else
    {
        microdexed_synth_note_off(note);
    }
}

void keyboard_engine_all_notes_off(void)
{
    microdexed_synth_all_notes_off();
    monob_synth_all_notes_off();

    const uint8_t filter_track = keyboard_engine_get_filter_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_all_notes_off(filter_track);
    }

    if (keyboard_engine_active_track_has_midi_note_path())
    {
        midi_all_notes_off(MIDI_DEST_USB, KBD_ARP_MIDI_CHANNEL);
    }

    g_keyboard_engine_sounding_active = false;
}
