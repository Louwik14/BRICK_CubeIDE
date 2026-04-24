/******************************************************************************
 * @file    keyboard_params.c
 * @brief   Gestion des paramètres musicaux du clavier hors arpégiateur.
 *
 * Ce module stocke et applique les réglages liés au comportement clavier :
 * - tonalité racine
 * - gamme
 * - mode omnichord
 * - ordre des notes
 * - override d’accord
 *
 * Il propage ces paramètres vers les couches UI et mapping clavier, sans gérer
 * lui-même le rendu audio ni la logique de jeu temps-réel.
 ******************************************************************************/

#include "Keyboard/keyboard_params.h"

#include "Keyboard/kbd_input_mapper.h"
#include "UI/ui_core.h"

typedef struct
{
    uint8_t root_index;
    uint8_t scale_index;
    bool omnichord;
    note_order_t note_order;
    bool chord_override;
    bool mono_last;
} keyboard_params_state_t;

static keyboard_params_state_t g_keyboard_params = {
    .root_index = 0U,
    .scale_index = (uint8_t)KBD_SCALE_MAJOR,
    .omnichord = false,
    .note_order = NOTE_ORDER_NATURAL,
    .chord_override = false,
    .mono_last = false,
};

static void keyboard_params_apply(void)
{
    ui_keyboard_app_set_params((uint8_t)(60U + (g_keyboard_params.root_index % 12U)),
                               (kbd_scale_t)g_keyboard_params.scale_index,
                               g_keyboard_params.omnichord);
    ui_keyboard_app_set_note_order(g_keyboard_params.note_order);
    ui_keyboard_app_set_chord_override(g_keyboard_params.chord_override);
    kbd_input_mapper_set_omnichord_state(g_keyboard_params.omnichord);
}

void keyboard_params_init(void)
{
    keyboard_params_apply();
}

void keyboard_params_set_root(uint8_t root_index)
{
    g_keyboard_params.root_index = (uint8_t)(root_index % 12U);
    keyboard_params_apply();
}

void keyboard_params_set_scale(uint8_t scale_index)
{
    if (scale_index > (uint8_t)KBD_SCALE_CHROMATIC)
    {
        scale_index = (uint8_t)KBD_SCALE_CHROMATIC;
    }

    g_keyboard_params.scale_index = scale_index;
    keyboard_params_apply();
}

void keyboard_params_set_omnichord(bool enabled)
{
    g_keyboard_params.omnichord = enabled;
    keyboard_params_apply();
}

void keyboard_params_set_note_order(note_order_t order)
{
    g_keyboard_params.note_order = order;
    keyboard_params_apply();
}

void keyboard_params_set_chord_override(bool enabled)
{
    g_keyboard_params.chord_override = enabled;
    keyboard_params_apply();
}

void keyboard_params_set_mono_last(bool enabled)
{
    g_keyboard_params.mono_last = enabled;
}

uint8_t keyboard_params_get_root_index(void)
{
    return g_keyboard_params.root_index;
}

uint8_t keyboard_params_get_scale_index(void)
{
    return g_keyboard_params.scale_index;
}

bool keyboard_params_get_omnichord(void)
{
    return g_keyboard_params.omnichord;
}

note_order_t keyboard_params_get_note_order(void)
{
    return g_keyboard_params.note_order;
}

bool keyboard_params_get_chord_override(void)
{
    return g_keyboard_params.chord_override;
}

bool keyboard_params_get_mono_last(void)
{
    return g_keyboard_params.mono_last;
}
