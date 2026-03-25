/******************************************************************************
 * @file    kbd_input_mapper.c
 * @brief   Traduction des entrées physiques du clavier vers l’interface clavier.
 *
 * Ce module convertit les index issus du scan matériel en événements compris
 * par ui_keyboard_app :
 * - mapping direct en mode clavier normal
 * - redistribution note / accord en mode omnichord
 * - filtrage des index hors plage
 *
 * Il ne gère ni l’état musical du clavier, ni le moteur audio, ni la logique
 * d’arpégiateur. Son rôle se limite au routage des entrées physiques.
 ******************************************************************************/

#include "Keyboard/kbd_input_mapper.h"

#include "Keyboard/ui_keyboard_app.h"

static bool g_kbd_mapper_omnichord = false;

void kbd_input_mapper_init(bool omnichord_state)
{
    g_kbd_mapper_omnichord = omnichord_state;
}

void kbd_input_mapper_set_omnichord_state(bool enabled)
{
    g_kbd_mapper_omnichord = enabled;
}

void kbd_input_mapper_process(uint8_t seq_index, bool pressed)
{
    if ((seq_index < 1U) || (seq_index > 16U))
    {
        return;
    }

    const uint8_t idx = (uint8_t)(seq_index - 1U);

    if (!g_kbd_mapper_omnichord)
    {
        ui_keyboard_app_note_button(idx, pressed);
        return;
    }

    if (idx <= 3U)
    {
        ui_keyboard_app_chord_button(idx, pressed);
        return;
    }

    if ((idx >= 8U) && (idx <= 11U))
    {
        ui_keyboard_app_chord_button((uint8_t)(idx - 4U), pressed);
        return;
    }

    if ((idx >= 4U) && (idx <= 7U))
    {
        ui_keyboard_app_note_button((uint8_t)(idx - 4U), pressed);
        return;
    }

    if ((idx >= 12U) && (idx <= 14U))
    {
        ui_keyboard_app_note_button((uint8_t)(idx - 8U), pressed);
        return;
    }

    if (idx == 15U)
    {
        ui_keyboard_app_note_button(7U, pressed);
    }
}
