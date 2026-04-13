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

#include "Keyboard/kbd_input_mapper.h"
#include "Keyboard/keyboard_arp.h"
#include "Keyboard/keyboard_engine.h"
#include "Keyboard/keyboard_params.h"
#include "Keyboard/keyboard_runtime.h"
#include "Keyboard/ui_keyboard_app.h"
#include "ui_core.h"

static void keyboard_input_note_on_sink(uint8_t note, uint8_t velocity)
{
    if (ui_get_hall_mode() == UI_HALL_MODE_ARP)
    {
        if (keyboard_runtime_is_master_buffer_route_context() != 0U)
        {
            return;
        }

        keyboard_arp_note_on(note, velocity);
        return;
    }

    keyboard_engine_note_on(note, velocity);
}

static void keyboard_input_note_off_sink(uint8_t note)
{
    if (ui_get_hall_mode() == UI_HALL_MODE_ARP)
    {
        if (keyboard_runtime_is_master_buffer_route_context() != 0U)
        {
            return;
        }

        keyboard_arp_note_off(note);
        return;
    }

    keyboard_engine_note_off(note);
}

static void keyboard_input_all_notes_off_sink(void)
{
    if (keyboard_runtime_is_master_buffer_route_context() == 0U)
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
    ui_keyboard_app_set_velocity(velocity);
    kbd_input_mapper_process((uint8_t)(hall_index + 1U), pressed);
}
