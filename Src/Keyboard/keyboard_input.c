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

#include "App/Hall/hall_keymap.h"
#include "Board/board_product.h"
#include "Keyboard/kbd_chords_dict.h"
#include "Keyboard/kbd_input_mapper.h"
#include "Keyboard/keyboard_engine.h"
#include "Keyboard/keyboard_params.h"
#include "Keyboard/ui_keyboard_app.h"
#include "Storage/undo_v2.h"
#include "buttons.h"
#include "ui_core.h"
#include "ui_core_mute.h"
#include "ui_event.h"
#include "ui_navigation.h"
#include "ui_page_manager.h"
#include "pages/ui_page_settings.h"
#include "pages/ui_page_template_cfg.h"
#include "Core/live_event.h"

#include <string.h>

#define LOWCOST_KEY_COUNT HALL_KEY_COUNT
#define KEYBOARD_INPUT_OWNER_STACK_DEPTH 8U

typedef struct
{
    uint8_t track;
} keyboard_input_note_owner_t;

static uint8_t g_lowcost_key_note[LOWCOST_KEY_COUNT];
static uint8_t g_lowcost_key_down[LOWCOST_KEY_COUNT];
static uint8_t g_lowcost_key_consumed[LOWCOST_KEY_COUNT];
static keyboard_input_note_owner_t
    g_keyboard_input_note_owner[128U][KEYBOARD_INPUT_OWNER_STACK_DEPTH];
static uint8_t g_keyboard_input_note_owner_count[128U];
static uint8_t g_keyboard_input_timed_context_active;
static uint32_t g_keyboard_input_capture_tick;
static uint32_t g_keyboard_input_ingress_serial;

static void keyboard_input_note_on_sink(uint8_t note, uint8_t velocity);
static void keyboard_input_note_off_sink(uint8_t note);

static void keyboard_input_note_owner_push(uint8_t note, uint8_t track)
{
    if (note >= 128U)
    {
        return;
    }

    uint8_t count = g_keyboard_input_note_owner_count[note];
    if (count >= KEYBOARD_INPUT_OWNER_STACK_DEPTH)
    {
        for (uint8_t i = 1U; i < KEYBOARD_INPUT_OWNER_STACK_DEPTH; ++i)
        {
            g_keyboard_input_note_owner[note][i - 1U] =
                g_keyboard_input_note_owner[note][i];
        }
        count = (uint8_t)(KEYBOARD_INPUT_OWNER_STACK_DEPTH - 1U);
    }

    g_keyboard_input_note_owner[note][count].track = track;
    g_keyboard_input_note_owner_count[note] = (uint8_t)(count + 1U);
}

static uint8_t keyboard_input_note_owner_pop(uint8_t note,
                                             keyboard_input_note_owner_t *out_owner)
{
    if ((note >= 128U) || (out_owner == NULL)
            || (g_keyboard_input_note_owner_count[note] == 0U))
    {
        return 0U;
    }

    const uint8_t index = (uint8_t)(g_keyboard_input_note_owner_count[note] - 1U);
    *out_owner = g_keyboard_input_note_owner[note][index];
    g_keyboard_input_note_owner_count[note] = index;
    return 1U;
}

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
    hall_key_metadata_t meta;
    if ((hall_keymap_metadata(key, &meta) == 0U) || (meta.kind != HALL_KEY_KIND_WHITE))
    {
        return 0U;
    }

    const uint8_t scale = keyboard_params_get_scale_index();
    const uint8_t period = keyboard_input_scale_period(scale);
    const uint8_t white_zero = (uint8_t)(meta.white_index - 1U);
    const uint8_t degree = (period != 0U) ? (uint8_t)(white_zero % period) : 0U;
    const uint8_t octave = (period != 0U) ? (uint8_t)(white_zero / period) : 0U;
    int16_t note = (int16_t)(60U + (keyboard_params_get_root_index() % 12U));

    if (scale == (uint8_t)KBD_SCALE_CHROMATIC)
    {
        note = (int16_t)(note + white_zero);
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

static void keyboard_input_lowcost_nav_button(button_id_t button)
{
    const ui_event_t ev = {
        .type = UI_EVENT_BUTTON_PRESS,
        .id = (uint8_t)button,
        .value = 1
    };
    ui_navigation_handle_event(&ev);
}

static void keyboard_input_lowcost_trigger_black_shortcut(uint8_t black_index)
{
    switch (black_index)
    {
        case 1U:
            keyboard_input_lowcost_nav_button(BTN_PARAM_2); /* Tone */
            break;

        case 2U:
            keyboard_input_lowcost_nav_button(BTN_PARAM_1); /* Env ensemble */
            break;

        case 3U:
            keyboard_input_lowcost_nav_button(BTN_PARAM_5); /* Play */
            break;

        case 4U:
            keyboard_input_lowcost_nav_button(BTN_PARAM_3); /* Mod */
            break;

        case 5U:
            keyboard_input_lowcost_nav_button(BTN_PARAM_4); /* Mix */
            break;

        case 6U:
            (void)ui_core_request_undo();
            break;

        case 7U:
            (void)undo_v2_redo();
            break;

        case 8U:
            break;

        case 9U:
            ui_page_template_rec_cfg_open_main();
            ui_page_set(UI_PAGE_TEMPLATE_REC_CFG);
            break;

        case 10U:
            if (ui_page_settings_is_open() == 0U)
            {
                ui_page_settings_open(ui_page_get_id());
            }
            break;

        default:
            break;
    }
}

static uint8_t keyboard_input_lowcost_shortcut_press(uint8_t key, ui_hall_mode_t mode)
{
    hall_key_metadata_t meta;
    if ((hall_keymap_metadata(key, &meta) == 0U) || (meta.kind != HALL_KEY_KIND_BLACK))
    {
        return 0U;
    }

    const uint8_t shift_down = (button_down(BTN_SHIFT) != 0U) ? 1U : 0U;
    const uint8_t shortcut_active =
        (uint8_t)(((mode == UI_HALL_MODE_SEQ)
                   || ((mode == UI_HALL_MODE_KEYBOARD)
                       && (shift_down != 0U)))
                  ? 1U : 0U);
    if (shortcut_active == 0U)
    {
        return 0U;
    }

    keyboard_input_lowcost_trigger_black_shortcut(meta.black_index);
    return 1U;
}

static ui_hall_mode_t keyboard_input_effective_input_mode(void)
{
    const ui_hall_mode_t mode = ui_get_hall_mode();
    return (mode == UI_HALL_MODE_MUTE) ? ui_core_mute_get_passthrough_hall_mode() : mode;
}

static uint8_t keyboard_input_lowcost_chromatic_note(uint8_t key)
{
    hall_key_metadata_t meta;
    if (hall_keymap_metadata(key, &meta) == 0U)
    {
        return 0U;
    }

    int16_t note = (int16_t)(60U + (keyboard_params_get_root_index() % 12U) + meta.chromatic_position);
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

    hall_key_metadata_t meta;
    if (hall_keymap_metadata(key, &meta) == 0U)
    {
        return;
    }

    uint8_t group = 0xFFU;
    if ((meta.kind == HALL_KEY_KIND_WHITE) && (meta.white_index >= 1U) && (meta.white_index <= 4U))
    {
        group = (uint8_t)(meta.white_index - 1U);
    }
    else if ((meta.kind == HALL_KEY_KIND_BLACK) && (meta.black_index >= 1U) && (meta.black_index <= 3U))
    {
        group = (uint8_t)(4U + meta.black_index - 1U);
    }

    if (group < (uint8_t)(sizeof(chord_for_left_group) / sizeof(chord_for_left_group[0])))
    {
        ui_keyboard_app_chord_button(chord_for_left_group[group], pressed);
        return;
    }

    ui_keyboard_app_note_button(meta.chromatic_position, pressed);
}

static void keyboard_input_process_lowcost_key(uint8_t key, bool pressed, uint8_t velocity)
{
    if (key >= LOWCOST_KEY_COUNT)
    {
        return;
    }

    ui_keyboard_app_set_velocity(velocity);

    if (pressed)
    {
        const ui_hall_mode_t mode = keyboard_input_effective_input_mode();
        if (keyboard_input_lowcost_shortcut_press(key, mode) != 0U)
        {
            g_lowcost_key_consumed[key] = 1U;
            return;
        }
    }
    else if (g_lowcost_key_consumed[key] != 0U)
    {
        g_lowcost_key_consumed[key] = 0U;
        return;
    }

    if (keyboard_params_get_omnichord())
    {
        keyboard_input_lowcost_omni(key, pressed);
        return;
    }

    if (pressed)
    {
        const ui_hall_mode_t mode = keyboard_input_effective_input_mode();
        if (mode == UI_HALL_MODE_SEQ)
        {
            hall_key_metadata_t meta;
            if ((hall_keymap_metadata(key, &meta) == 0U) || (meta.kind != HALL_KEY_KIND_WHITE))
            {
                return;
            }
        }
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
    const uint8_t active_track = ui_get_active_lane();
    const ui_hall_mode_t hall_mode = keyboard_input_effective_input_mode();
    const ui_hall_mode_effective_view_t effective_view =
        ui_hall_mode_resolve_effective_view(active_track, hall_mode);

    if (effective_view == UI_HALL_MODE_VIEW_ROUT)
    {
        return;
    }

    keyboard_input_note_owner_push(note, active_track);
    if (g_keyboard_input_timed_context_active != 0U)
    {
        keyboard_engine_note_on_for_track_timed(active_track, note, velocity,
                                                g_keyboard_input_capture_tick,
                                                g_keyboard_input_ingress_serial);
    }
    else
    {
        keyboard_engine_note_on_for_track(active_track, note, velocity);
    }
}

static void keyboard_input_note_off_sink(uint8_t note)
{
    keyboard_input_note_owner_t owner;
    if (keyboard_input_note_owner_pop(note, &owner) == 0U)
    {
        return;
    }

    if (g_keyboard_input_timed_context_active != 0U)
    {
        keyboard_engine_note_off_for_track_timed(owner.track, note,
                                                 g_keyboard_input_capture_tick,
                                                 g_keyboard_input_ingress_serial);
    }
    else
    {
        keyboard_engine_note_off_for_track(owner.track, note);
    }
}

static void keyboard_input_all_notes_off_sink(void)
{
    for (uint8_t note = 0U; note < 128U; ++note)
    {
        keyboard_input_note_owner_t owner;
        while (keyboard_input_note_owner_pop(note, &owner) != 0U)
        {
            keyboard_engine_note_off_for_track(owner.track, note);
        }
    }
}

void keyboard_input_init(void)
{
    memset(g_keyboard_input_note_owner_count, 0, sizeof(g_keyboard_input_note_owner_count));
    g_keyboard_input_timed_context_active = 0U;
    g_keyboard_input_capture_tick = 0U;
    g_keyboard_input_ingress_serial = 0U;
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

void keyboard_input_process_hall_timed(uint8_t hall_index, bool pressed,
                                       uint8_t velocity, uint32_t capture_tick,
                                       uint32_t ingress_serial)
{
    g_keyboard_input_timed_context_active = 1U;
    g_keyboard_input_capture_tick = capture_tick;
    g_keyboard_input_ingress_serial = ingress_serial;
    keyboard_input_process_hall(hall_index, pressed, velocity);
    g_keyboard_input_timed_context_active = 0U;
}
