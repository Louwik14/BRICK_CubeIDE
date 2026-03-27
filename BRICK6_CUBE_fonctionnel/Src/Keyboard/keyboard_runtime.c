/******************************************************************************
 * @file    keyboard_runtime.c
 * @brief   Orchestrateur principal du sous-système clavier.
 *
 * Ce module coordonne les différents blocs du clavier :
 * - initialisation générale
 * - tick runtime
 * - wrappers publics appelés par le reste de l’application
 * - gestion des changements de mode et de track
 *
 * Il ne contient plus la logique interne détaillée de l’arpégiateur,
 * du moteur de notes, des paramètres clavier ou des entrées hall.
 * Ces responsabilités sont déportées dans des modules dédiés.
 ******************************************************************************/

#include "Keyboard/keyboard_runtime.h"

#include "Keyboard/keyboard_arp.h"
#include "Keyboard/keyboard_params.h"
#include "Keyboard/keyboard_input.h"

#include "ui_core.h"



void keyboard_runtime_init(void)
{
    keyboard_input_init();
    keyboard_params_init();
    keyboard_arp_init();
}

void keyboard_runtime_tick(void)
{
    ui_keyboard_app_tick(0U);

    if (ui_get_hall_mode() != UI_HALL_MODE_ARP)
    {
        return;
    }

    keyboard_arp_tick();
}

void keyboard_runtime_set_root(uint8_t root_index) { keyboard_params_set_root(root_index); }
void keyboard_runtime_set_scale(uint8_t scale_index) { keyboard_params_set_scale(scale_index); }
void keyboard_runtime_set_omnichord(bool enabled) { keyboard_params_set_omnichord(enabled); }
void keyboard_runtime_set_note_order(note_order_t order) { keyboard_params_set_note_order(order); }
void keyboard_runtime_set_chord_override(bool enabled) { keyboard_params_set_chord_override(enabled); }


void keyboard_runtime_set_arp_hold(bool enabled) { keyboard_arp_set_hold(enabled); }
void keyboard_runtime_set_arp_rate(uint8_t value) { keyboard_arp_set_rate(value); }
void keyboard_runtime_set_arp_oct(uint8_t value) { keyboard_arp_set_oct(value); }
void keyboard_runtime_set_arp_pattern(uint8_t value) { keyboard_arp_set_pattern(value); }
void keyboard_runtime_set_arp_gate(uint8_t value) { keyboard_arp_set_gate(value); }
void keyboard_runtime_set_arp_swing(uint8_t value) { keyboard_arp_set_swing(value); }
void keyboard_runtime_set_arp_accent(uint8_t value) { keyboard_arp_set_accent(value); }
void keyboard_runtime_set_arp_vel_acc(uint8_t value) { keyboard_arp_set_vel_acc(value); }
void keyboard_runtime_set_arp_strum(uint8_t value) { keyboard_arp_set_strum(value); }
void keyboard_runtime_set_arp_offset(int8_t value) { keyboard_arp_set_offset(value); }
void keyboard_runtime_set_arp_transpose(int8_t value) { keyboard_arp_set_transpose(value); }
void keyboard_runtime_set_arp_spread(uint8_t value) { keyboard_arp_set_spread(value); }
void keyboard_runtime_set_arp_dir(uint8_t value) { keyboard_arp_set_dir(value); }
void keyboard_runtime_set_arp_sync(uint8_t value) { keyboard_arp_set_sync(value); }

void keyboard_runtime_step_octave(int8_t delta)
{
    ui_keyboard_app_set_octave_shift((int8_t)(ui_keyboard_app_get_octave_shift() + delta));
}

void keyboard_runtime_process_hall(uint8_t hall_index, bool pressed, uint8_t velocity)
{
    keyboard_input_process_hall(hall_index, pressed, velocity);
}

void keyboard_runtime_all_notes_off(void)
{
    ui_keyboard_app_all_notes_off();
    keyboard_arp_all_notes_off();
}


void keyboard_runtime_on_active_track_changed(void)
{
    const ui_hall_mode_t hall_mode = ui_get_hall_mode();
    if ((hall_mode != UI_HALL_MODE_KEYBOARD) && (hall_mode != UI_HALL_MODE_ARP))
    {
        return;
    }

    keyboard_runtime_all_notes_off();
}

void keyboard_runtime_on_hall_mode_changed(ui_hall_mode_t previous_mode, ui_hall_mode_t new_mode)
{
    if ((previous_mode == UI_HALL_MODE_ARP) && (new_mode != UI_HALL_MODE_ARP))
    {
        keyboard_arp_on_mode_leave();
    }

    if ((new_mode == UI_HALL_MODE_ARP) && (previous_mode != UI_HALL_MODE_ARP))
    {
        keyboard_arp_on_mode_enter();
    }
}

uint8_t keyboard_runtime_get_root_index(void) { return keyboard_params_get_root_index(); }
uint8_t keyboard_runtime_get_scale_index(void) { return keyboard_params_get_scale_index(); }
bool keyboard_runtime_get_omnichord(void) { return keyboard_params_get_omnichord(); }
note_order_t keyboard_runtime_get_note_order(void) { return keyboard_params_get_note_order(); }
bool keyboard_runtime_get_chord_override(void) { return keyboard_params_get_chord_override(); }

int8_t keyboard_runtime_get_octave_shift(void)
{
    return ui_keyboard_app_get_octave_shift();
}
