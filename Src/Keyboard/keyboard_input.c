/******************************************************************************
 * @file    keyboard_input.c
 * @brief   Gestion des entrées clavier et des sinks de notes.
 *
 * Ce module fait le lien entre les événements d’entrée et le moteur clavier :
 * - initialisation du sink ui_keyboard_app
 * - traitement des entrées hall
 * - routage des note_on / note_off
 * - bascule entre jeu direct et arpégiateur selon le mode actif
 *
 * Il ne contient pas la logique interne de synthèse ni celle de l’arpégiateur,
 * mais distribue les événements vers les bons modules.
 ******************************************************************************/

#include "Keyboard/keyboard_input.h"

#include "Board/board_product.h"
#include "Keyboard/kbd_chords_dict.h"
#include "Keyboard/kbd_input_mapper.h"
#include "Keyboard/keyboard_arp.h"
#include "Keyboard/keyboard_engine.h"
#include "Keyboard/keyboard_params.h"
#include "Keyboard/ui_keyboard_app.h"
#include "ui_core.h"

#define LOWCOST_KEY_COUNT 16U

static uint8_t g_lowcost_key_note[LOWCOST_KEY_COUNT];
static uint8_t g_lowcost_key_down[LOWCOST_KEY_COUNT];

static void keyboard_input_note_on_sink(uint8_t note, uint8_t velocity);
static void keyboard_input_note_off_sink(uint8_t note);

static uint8_t keyboard_input_has_separate_hall_keyboard(void)
{
    const board_product_capabilities_t *caps = board_product_capabilities();
    return ((caps != 0) && (caps->has_separate_hall_keyboard != 0U)) ? 1U : 0U;
}

static uint8_t keyboard_input_scale_period(uint8_t scale)
{
    switch (scale)
    {
        case (uint8_t)KBD_SCALE_PENT_MAJOR:
        case (uint8_t)KBD_SCALE_PENT_MINOR:
            return 5U;

        case (uint8_t)KBD_SCALE_CHROMATIC:
            return 12U;

        default:
            return 7U;
    }
}

static uint8_t keyboard_input_lowcost_seq_note(uint8_t key)
{
    const uint8_t scale = keyboard_params_get_scale_index();
    const uint8_t period = keyboard_input_scale_period(scale);
    const uint8_t degree = (period != 0U) ? (uint8_t)(key % period) : 0U;
    const uint8_t octave = (period != 0U) ? (uint8_t)(key / period) : 0U;
    int16_t note = (int16_t)(60U + (keyboard_params_get_root_index() % 12U));

    if (scale == (uint8_t)KBD_SCALE_CHROMATIC)
    {
        note = (int16_t)(note + key);
    }
    else
    {
        note = (int16_t)(note + kbd_scale_slot_semitone_offset(scale, degree) + ((int16_t)octave * 12));
    }

    note = (int16_t)(note + ((int16_t)ui_keyboard_app_get_octave_shift() * 12));
    if (note < 0)
    {
        note = 0;
    }
    if (note > 127)
    {
        note = 127;
    }
    return (uint8_t)note;
}

static uint8_t keyboard_input_lowcost_chromatic_note(uint8_t key)
{
    int16_t note = (int16_t)(60U + (keyboard_params_get_root_index() % 12U) + key);
    note = (int16_t)(note + ((int16_t)ui_keyboard_app_get_octave_shift() * 12));
    if (note < 0)
    {
        note = 0;
    }
    if (note > 127)
    {
        note = 127;
    }
    return (uint8_t)note;
}

static void keyboard_input_lowcost_omni(uint8_t key, bool pressed)
{
    static const uint8_t chord_for_left_group[7] = {
        0U, /* Maj */
        1U, /* Min */
        2U, /* Sus */
        3U, /* Dim */
        6U, /* 6 */
        4U, /* m7 */
        5U  /* M7 */
    };

    if (key < (uint8_t)(sizeof(chord_for_left_group) / sizeof(chord_for_left_group[0])))
    {
        ui_keyboard_app_chord_button(chord_for_left_group[key], pressed);
        return;
    }

    ui_keyboard_app_note_button((uint8_t)(key - 7U), pressed);
}

static void keyboard_input_process_lowcost_key(uint8_t key, bool pressed, uint8_t velocity)
{
    if (key >= LOWCOST_KEY_COUNT)
    {
        return;
    }

    ui_keyboard_app_set_velocity(velocity);
    if (keyboard_params_get_omnichord())
    {
        keyboard_input_lowcost_omni(key, pressed);
        return;
    }

    if (pressed)
    {
        const ui_hall_mode_t mode = ui_get_hall_mode();
        const uint8_t note = (mode == UI_HALL_MODE_SEQ)
            ? keyboard_input_lowcost_seq_note(key)
            : keyboard_input_lowcost_chromatic_note(key);
        g_lowcost_key_note[key] = note;
        g_lowcost_key_down[key] = 1U;
        keyboard_input_note_on_sink(note, velocity);
        return;
    }

    if (g_lowcost_key_down[key] != 0U)
    {
        g_lowcost_key_down[key] = 0U;
        keyboard_input_note_off_sink(g_lowcost_key_note[key]);
    }
}

static void keyboard_input_note_on_sink(uint8_t note, uint8_t velocity)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_hall_mode_t hall_mode = ui_get_hall_mode();
    const ui_hall_mode_effective_view_t effective_view =
        ui_hall_mode_resolve_effective_view(active_track, hall_mode);

    if (ui_hall_uses_arp_engine(active_track, hall_mode) != 0U)
    {
        keyboard_arp_note_on_for_track(active_track, note, velocity);
        return;
    }

    if (effective_view == UI_HALL_MODE_VIEW_ROUT)
    {
        return;
    }

    keyboard_engine_note_on(note, velocity);
}

static void keyboard_input_note_off_sink(uint8_t note)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_hall_mode_t hall_mode = ui_get_hall_mode();
    const ui_hall_mode_effective_view_t effective_view =
        ui_hall_mode_resolve_effective_view(active_track, hall_mode);

    if (ui_hall_uses_arp_engine(active_track, hall_mode) != 0U)
    {
        keyboard_arp_note_off_for_track(active_track, note);
        return;
    }

    if (effective_view == UI_HALL_MODE_VIEW_ROUT)
    {
        return;
    }

    keyboard_engine_note_off(note);
}

static void keyboard_input_all_notes_off_sink(void)
{
    if (ui_hall_mode_resolve_effective_view(ui_get_active_track(),
                                            ui_get_hall_mode()) != UI_HALL_MODE_VIEW_ROUT)
    {
        keyboard_arp_all_notes_off();
    }
}

void keyboard_input_init(void)
{
    const ui_keyboard_note_sink_t sink = {
        .note_on = keyboard_input_note_on_sink,
        .note_off = keyboard_input_note_off_sink,
        .all_notes_off = keyboard_input_all_notes_off_sink,
        .velocity = 100U,
    };

    ui_keyboard_app_init(&sink);
    kbd_input_mapper_init(keyboard_params_get_omnichord());
}

void keyboard_input_process_hall(uint8_t hall_index, bool pressed, uint8_t velocity)
{
    if (keyboard_input_has_separate_hall_keyboard() != 0U)
    {
        keyboard_input_process_lowcost_key(hall_index, pressed, velocity);
        return;
    }

    ui_keyboard_app_set_velocity(velocity);
    kbd_input_mapper_process((uint8_t)(hall_index + 1U), pressed);
}
