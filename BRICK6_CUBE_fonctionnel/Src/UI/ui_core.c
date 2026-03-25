/**
 * @file ui_core.c
 * @brief Module applicatif ui_core.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_core.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "ui_core.h"

#include <stdio.h>
#include <string.h>

#include "stm32h7xx_hal.h"

#include "buttons.h"
#include "encoders.h"
#include "pages/ui_page_main.h"
#include "pages/ui_page_param_test.h"
#include "pages/ui_page_debug_hall.h"
#include "pages/ui_page_calibration.h"
#include "pages/ui_page_template_filter.h"
#include "pages/ui_page_template_dx7.h"
#include "pages/ui_page_template_cfg.h"
#include "pages/ui_page_template_keyboard.h"
#include "pages/ui_page_template_arp.h"
#include "pages/ui_page_template_seq.h"
#include "ui_event.h"
#include "ui_navigation.h"
#include "ui_page_manager.h"
#include "ui_param.h"
#include "ui_template_page.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_engine.h"
#include "Keyboard/keyboard_runtime.h"
#include "param_registry.h"
#include "param_store.h"
#include "audio_float.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"

#define UI_CFG_TRACK_PARAM ((param_id_t)PARAM_CFG_TRACK)
#define UI_CFG_TRACK_TYPE_PARAM ((param_id_t)PARAM_CFG_TRACK_TYPE)
#define UI_HALL_KEYBOARD_MODE_TRIGGER 8U
#define UI_HALL_ARP_MODE_TRIGGER 9U
#define UI_HALL_SEQ_MODE_TRIGGER 10U
#define UI_HALL_MODE_DOUBLE_TAP_MS 400U
#define UI_FEEDBACK_DURATION_MS 1000U

typedef struct
{
    uint8_t active_track;
    uint8_t shift_down;
    uint8_t track_select_armed;
    ui_hall_mode_t hall_mode;
    uint32_t mode_tap_ms[UI_HALL_MODE_COUNT];
    ui_track_config_t track_configs[UI_TRACK_COUNT];
    uint8_t hall_prev_pressed[HALL_KEY_COUNT];
    uint8_t hall_note_suppressed[HALL_KEY_COUNT];
    char feedback_message[16];
    uint32_t feedback_until_ms;
} ui_track_state_t;

static ui_track_state_t g_ui_track_state = {
    .active_track = 0U,
    .shift_down = 0U,
    .track_select_armed = 0U,
    .hall_mode = UI_HALL_MODE_SEQ,
    .mode_tap_ms = { 0U },
    .track_configs = {
        { UI_TRACK_FAMILY_INPUT1, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_INPUT2, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_INPUT3, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_INPUT4, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_SYNTH, UI_TRACK_TYPE_DX7 },
        { UI_TRACK_FAMILY_SYNTH, UI_TRACK_TYPE_DX7 },
        { UI_TRACK_FAMILY_SYNTH, UI_TRACK_TYPE_DX7 },
        { UI_TRACK_FAMILY_SYNTH, UI_TRACK_TYPE_DX7 },
    },
    .hall_prev_pressed = { 0U },
    .hall_note_suppressed = { 0U },
    .feedback_message = { 0 },
    .feedback_until_ms = 0U,
};

typedef struct
{
    uint8_t hall_index;
    ui_hall_mode_t target_mode;
    uint8_t target_page;
} ui_hall_mode_trigger_t;

static const ui_hall_mode_trigger_t g_ui_hall_mode_triggers[] = {
    { UI_HALL_KEYBOARD_MODE_TRIGGER, UI_HALL_MODE_KEYBOARD, UI_PAGE_TEMPLATE_KEYBOARD },
    { UI_HALL_ARP_MODE_TRIGGER, UI_HALL_MODE_ARP, UI_PAGE_TEMPLATE_ARP },
    { UI_HALL_SEQ_MODE_TRIGGER, UI_HALL_MODE_SEQ, UI_PAGE_TEMPLATE_SEQ },
};


static ui_track_config_t ui_core_get_default_track_config(void)
{
    ui_track_config_t config = {
        .family = UI_TRACK_FAMILY_INPUT1,
        .type = UI_TRACK_TYPE_AUDIO,
    };

    return config;
}

bool ui_track_family_is_input(ui_track_family_t family)
{
    return (family == UI_TRACK_FAMILY_INPUT1)
            || (family == UI_TRACK_FAMILY_INPUT2)
            || (family == UI_TRACK_FAMILY_INPUT3)
            || (family == UI_TRACK_FAMILY_INPUT4);
}

bool ui_track_type_is_valid_for_family(ui_track_family_t family, ui_track_type_t type)
{
    if (((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
            || ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        return false;
    }

    if (ui_track_family_is_input(family))
    {
        return (type == UI_TRACK_TYPE_AUDIO) || (type == UI_TRACK_TYPE_HYBRID);
    }

    if (family == UI_TRACK_FAMILY_OFF)
    {
        return false;
    }

    if (family == UI_TRACK_FAMILY_SYNTH)
    {
        return (type == UI_TRACK_TYPE_DX7) || (type == UI_TRACK_TYPE_MONOB);
    }

    return false;
}

ui_track_type_t ui_get_default_track_type_for_family(ui_track_family_t family)
{
    if (family == UI_TRACK_FAMILY_SYNTH)
    {
        return UI_TRACK_TYPE_DX7;
    }

    return UI_TRACK_TYPE_AUDIO;
}

uint8_t ui_get_track_type_count_for_family(ui_track_family_t family)
{
    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    if (family == UI_TRACK_FAMILY_OFF)
    {
        return 0U;
    }

    return (family == UI_TRACK_FAMILY_SYNTH) ? 2U : 2U;
}

uint8_t ui_get_track_type_index_for_family(ui_track_family_t family, ui_track_type_t type)
{
    if (!ui_track_type_is_valid_for_family(family, type))
    {
        return 0U;
    }

    if (family == UI_TRACK_FAMILY_SYNTH)
    {
        return (type == UI_TRACK_TYPE_MONOB) ? 1U : 0U;
    }

    return (type == UI_TRACK_TYPE_HYBRID) ? 1U : 0U;
}

ui_track_type_t ui_get_track_type_from_family_index(ui_track_family_t family, uint8_t index)
{
    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return UI_TRACK_TYPE_AUDIO;
    }

    if (family == UI_TRACK_FAMILY_OFF)
    {
        return UI_TRACK_TYPE_AUDIO;
    }

    if (family == UI_TRACK_FAMILY_SYNTH)
    {
        return (index == 0U) ? UI_TRACK_TYPE_DX7 : UI_TRACK_TYPE_MONOB;
    }

    return (index == 0U) ? UI_TRACK_TYPE_AUDIO : UI_TRACK_TYPE_HYBRID;
}

static bool ui_core_track_family_is_available(uint8_t track, ui_track_family_t family)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT))
    {
        return false;
    }

    if (family == UI_TRACK_FAMILY_OFF)
    {
        return true;
    }

    if (!ui_track_family_is_input(family))
    {
        return true;
    }

    for (uint8_t other_track = 0U; other_track < UI_TRACK_COUNT; other_track++)
    {
        if (other_track == track)
        {
            continue;
        }

        if (g_ui_track_state.track_configs[other_track].family == family)
        {
            return false;
        }
    }

    return true;
}

static uint8_t ui_core_has_track_family(ui_track_family_t family)
{
    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; track++)
    {
        if (g_ui_track_state.track_configs[track].family == family)
        {
            return 1U;
        }
    }

    return 0U;
}

static void ui_core_sync_audio_runtime_enables(void)
{
    track_enable(0U, ui_core_has_track_family(UI_TRACK_FAMILY_INPUT1));
    track_enable(1U, ui_core_has_track_family(UI_TRACK_FAMILY_INPUT2));
    track_enable(2U, ui_core_has_track_family(UI_TRACK_FAMILY_INPUT3));
    track_enable(3U, ui_core_has_track_family(UI_TRACK_FAMILY_SYNTH));
}

static void ui_core_sync_active_track_cfg_params(void)
{
    const uint8_t active_track = g_ui_track_state.active_track;
    const ui_track_config_t *active_config = &g_ui_track_state.track_configs[active_track];

    param_store_set_active(UI_CFG_TRACK_PARAM, (float)active_config->family);
    param_store_set_active(UI_CFG_TRACK_TYPE_PARAM, (float)ui_get_track_type_index_for_family(active_config->family, active_config->type));
    param_registry_sync_filter_ui_for_active_track();
}

static void ui_core_set_active_track(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return;
    }

    if (g_ui_track_state.active_track == track)
    {
        ui_core_sync_active_track_cfg_params();
        return;
    }

    keyboard_runtime_on_active_track_changed();
    g_ui_track_state.active_track = track;
    ui_core_sync_active_track_cfg_params();
}

static void ui_core_reset_track_configs(void)
{
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; track++)
    {
        if (track < UI_AUDIO_INPUT_RESOURCE_COUNT)
        {
            g_ui_track_state.track_configs[track].family = (ui_track_family_t)((uint8_t)UI_TRACK_FAMILY_INPUT1 + track);
            g_ui_track_state.track_configs[track].type = UI_TRACK_TYPE_AUDIO;
        }
        else
        {
            g_ui_track_state.track_configs[track].family = UI_TRACK_FAMILY_SYNTH;
            g_ui_track_state.track_configs[track].type = UI_TRACK_TYPE_DX7;
        }

    }

    ui_core_sync_audio_runtime_enables();
}

static void ui_core_update_shift_state(uint8_t shift_down)
{
    if ((shift_down != 0U) && (g_ui_track_state.shift_down == 0U))
    {
        g_ui_track_state.shift_down = 1U;
        g_ui_track_state.track_select_armed = 1U;
        return;
    }

    if ((shift_down == 0U) && (g_ui_track_state.shift_down != 0U))
    {
        g_ui_track_state.shift_down = 0U;
        g_ui_track_state.track_select_armed = 0U;
    }
}

static const ui_hall_mode_trigger_t *ui_core_find_hall_mode_trigger(uint8_t hall)
{
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(g_ui_hall_mode_triggers) / sizeof(g_ui_hall_mode_triggers[0])); ++i)
    {
        if (g_ui_hall_mode_triggers[i].hall_index == hall)
        {
            return &g_ui_hall_mode_triggers[i];
        }
    }

    return 0;
}

static void ui_core_activate_hall_mode_trigger(const ui_hall_mode_trigger_t *trigger, uint8_t open_target_page)
{
    if (trigger == 0)
    {
        return;
    }

    ui_set_hall_mode(trigger->target_mode);

    if (open_target_page != 0U)
    {
        ui_page_set(trigger->target_page);
    }
}

static void ui_core_handle_shift_hall_action(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return;
    }

    g_ui_track_state.hall_note_suppressed[hall] = 1U;

    const ui_hall_mode_trigger_t *trigger = ui_core_find_hall_mode_trigger(hall);
    if (trigger != 0)
    {
        const uint32_t now = HAL_GetTick();
        const uint32_t last_tap = g_ui_track_state.mode_tap_ms[trigger->target_mode];
        const uint8_t is_double_tap = ((last_tap != 0U)
                                       && ((now - last_tap) <= UI_HALL_MODE_DOUBLE_TAP_MS)) ? 1U : 0U;

        g_ui_track_state.mode_tap_ms[trigger->target_mode] = now;
        ui_core_activate_hall_mode_trigger(trigger, is_double_tap);
        return;
    }

    if (hall < UI_TRACK_COUNT)
    {
        ui_core_set_active_track(hall);
    }
}

static void ui_core_handle_track_selection_event(const ui_event_t *ev)
{
    if (ev == 0)
    {
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_SHIFT))
    {
        g_ui_track_state.shift_down = 1U;
        g_ui_track_state.track_select_armed = 1U;
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_RELEASE) && (ev->id == (uint8_t)BTN_SHIFT))
    {
        g_ui_track_state.shift_down = 0U;
        g_ui_track_state.track_select_armed = 0U;
        return;
    }
}

/**
 * @brief Point d'entrée ui_core_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_core_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */


static void ui_core_set_feedback(const char *message)
{
    if (message == 0)
    {
        g_ui_track_state.feedback_message[0] = '\0';
        g_ui_track_state.feedback_until_ms = 0U;
        return;
    }

    (void)snprintf(g_ui_track_state.feedback_message,
                   sizeof(g_ui_track_state.feedback_message),
                   "%s",
                   message);
    g_ui_track_state.feedback_until_ms = HAL_GetTick() + UI_FEEDBACK_DURATION_MS;
}

static uint8_t ui_core_collect_held_seq_steps(seq_track_id_t *out_track,
                                              seq_step_id_t *out_steps,
                                              uint8_t max_steps,
                                              uint8_t promote_pending)
{
    return seq_edit_collect_held_steps(out_track, out_steps, max_steps, promote_pending);
}

static uint8_t ui_core_handle_transport_event(const ui_event_t *ev)
{
    if (ev == 0)
    {
        return 0U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_PLAY))
    {
        seq_runtime_toggle_play_stop();
        return 1U;
    }

    return 0U;
}

static uint8_t ui_core_handle_seq_mode_event(const ui_event_t *ev)
{
    if (ev == 0)
    {
        return 0U;
    }

    if (ui_get_hall_mode() != UI_HALL_MODE_SEQ)
    {
        return 0U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS)
        && ((ev->id == (uint8_t)BTN_COPY) || (ev->id == (uint8_t)BTN_PASTE)))
    {
        seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
        seq_track_id_t held_track = 0U;
        const uint8_t held_count = ui_core_collect_held_seq_steps(&held_track,
                                                                  held_steps,
                                                                  (uint8_t)SEQ_STEPS_PER_PAGE,
                                                                  1U);
        if (held_count == 0U)
        {
            return 1U;
        }

        if (ev->id == (uint8_t)BTN_COPY)
        {
            (void)seq_edit_copy_steps(held_track, held_steps, held_count);
            return 1U;
        }

        if (g_ui_track_state.shift_down != 0U)
        {
            seq_edit_clear_steps(held_track, held_steps, held_count);
            return 1U;
        }

        seq_clipboard_paste_result_t paste_result;
        if (seq_edit_paste_steps(held_track, held_steps, held_count, &paste_result) != 0U)
        {
            if (paste_result.trunc != 0U)
            {
                ui_core_set_feedback("PASTE TRUNC");
            }
            else if (paste_result.partial != 0U)
            {
                ui_core_set_feedback("PASTE PARTIAL");
            }
        }

        return 1U;
    }

    if (g_ui_track_state.shift_down != 0U)
    {
        return 0U;
    }

    const uint8_t track = ui_get_active_track();

    if ((ev->type == UI_EVENT_HALL_PRESS) && (ev->id < SEQ_STEPS_PER_PAGE))
    {
        seq_edit_step_press(ui_get_active_track(), ev->id);
        return 1U;
    }

    if ((ev->type == UI_EVENT_HALL_RELEASE) && (ev->id < SEQ_STEPS_PER_PAGE))
    {
        seq_edit_step_release(ui_get_active_track(), ev->id);
        return 1U;
    }

    if (ev->type == UI_EVENT_BUTTON_PRESS)
    {
        if (ev->id == (uint8_t)BTN_TRANSPOSE_UP)
        {
            seq_edit_change_page(track, 1);
            return 1U;
        }

        if (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        {
            seq_edit_change_page(track, -1);
            return 1U;
        }
    }

    return 0U;
}

void ui_core_init(void)
{
    g_ui_track_state.active_track = 0U;
    seq_runtime_init();
    ui_core_reset_track_configs();
    g_ui_track_state.shift_down = 0U;
    g_ui_track_state.track_select_armed = 0U;
    g_ui_track_state.hall_mode = UI_HALL_MODE_SEQ;
    ui_core_set_feedback(0);
    for (uint8_t mode = 0U; mode < (uint8_t)UI_HALL_MODE_COUNT; ++mode)
    {
        g_ui_track_state.mode_tap_ms[mode] = 0U;
    }

    for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
    {
        g_ui_track_state.hall_prev_pressed[hall] = 0U;
        g_ui_track_state.hall_note_suppressed[hall] = 0U;
    }

    ui_core_sync_active_track_cfg_params();

    ui_template_family_registry_init();
    ui_page_template_colors_register_families();
    ui_page_template_cfg_register_families();
    ui_page_template_dx7_register_families();
    ui_page_template_keyboard_register_families();
    ui_page_template_arp_register_families();
    ui_page_template_seq_register_families();

    ui_page_manager_init();

    /*
     * Register pages once at boot. Registration order defines stable page IDs
     * used by the navigation rule table.
     */
    ui_page_manager_register(&g_ui_page_main);
    ui_page_manager_register(&g_ui_page_param_test);
    ui_page_manager_register(&g_ui_page_debug_hall);
    ui_page_manager_register(&g_ui_page_calibration);
    ui_page_manager_register(&g_ui_page_user_calibration);
    ui_page_manager_register(&g_ui_page_template_colors);
    ui_page_manager_register(&g_ui_page_template_cfg);
    ui_page_manager_register(&g_ui_page_template_dx7);
    ui_page_manager_register(&g_ui_page_template_keyboard);
    ui_page_manager_register(&g_ui_page_template_arp);
    ui_page_manager_register(&g_ui_page_template_seq);

    if (hall_calibration_load() != 0U)
    {
        ui_page_set(UI_PAGE_MAIN);
    }
    else
    {
        ui_page_set(UI_PAGE_CALIBRATION);
    }
}

void ui_core_service_track_selection_inputs(void)
{
    ui_core_update_shift_state(button_down(BTN_SHIFT));

    for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
    {
        const uint8_t pressed = hall_engine_is_pressed(hall);
        const uint8_t was_pressed = g_ui_track_state.hall_prev_pressed[hall];

        if ((was_pressed == 0U) && (pressed != 0U)
                && (g_ui_track_state.shift_down != 0U)
                && (g_ui_track_state.track_select_armed != 0U))
        {
            ui_core_handle_shift_hall_action(hall);
        }

        g_ui_track_state.hall_prev_pressed[hall] = pressed;
    }

    if (((ui_get_hall_mode() == UI_HALL_MODE_KEYBOARD) || (ui_get_hall_mode() == UI_HALL_MODE_ARP)) && (g_ui_track_state.shift_down == 0U))
    {
        if (button_pressed(BTN_TRANSPOSE_UP) != 0U)
        {
            keyboard_runtime_step_octave(1);
        }

        if (button_pressed(BTN_TRANSPOSE_DOWN) != 0U)
        {
            keyboard_runtime_step_octave(-1);
        }
    }
}

/**
 * @brief Point d'entrée ui_core_tick.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_core_tick.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_core_tick(void)
{
    ui_event_t ev;

    for (uint8_t encoder = 0U; encoder < (uint8_t)ENC_COUNT; encoder++)
    {
        const int16_t delta = encoder_consume_delta(encoder);
        ui_param_handle_encoder(encoder, delta);
    }

    ui_event_from_inputs();
    seq_edit_step_hold_update();

    while (ui_event_pop(&ev))
    {
        ui_core_handle_track_selection_event(&ev);

        if (ui_core_handle_transport_event(&ev) != 0U)
        {
            continue;
        }

        if (ui_core_handle_seq_mode_event(&ev) != 0U)
        {
            continue;
        }

        ui_navigation_handle_event(&ev);

        const ui_page_t *active_page = ui_page_get();
        if ((active_page != 0) && (active_page->handle_event != 0))
        {
            active_page->handle_event(&ev);
        }
    }

    const ui_page_t *active_page = ui_page_get();
    if ((active_page != 0) && (active_page->tick != 0))
    {
        active_page->tick();
    }
}

uint8_t ui_get_active_track(void)
{
    return g_ui_track_state.active_track;
}

bool ui_resolve_filter_target_track(uint8_t *out_track_id)
{
    if (out_track_id == 0)
    {
        return false;
    }

    const ui_track_family_t family = ui_get_track_family(ui_get_active_track());
    switch (family)
    {
        case UI_TRACK_FAMILY_INPUT1:
            *out_track_id = 0U;
            return true;

        case UI_TRACK_FAMILY_INPUT2:
            *out_track_id = 1U;
            return true;

        case UI_TRACK_FAMILY_INPUT3:
            *out_track_id = 2U;
            return true;

        case UI_TRACK_FAMILY_SYNTH:
            *out_track_id = 3U;
            return true;

        default:
            return false;
    }
}

ui_track_config_t ui_get_track_config(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return ui_core_get_default_track_config();
    }

    return g_ui_track_state.track_configs[track];
}

ui_track_family_t ui_get_track_family(uint8_t track)
{
    return ui_get_track_config(track).family;
}

ui_track_type_t ui_get_track_type(uint8_t track)
{
    return ui_get_track_config(track).type;
}

bool ui_set_track_family(uint8_t track, ui_track_family_t family)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT))
    {
        return false;
    }

    if (!ui_core_track_family_is_available(track, family))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_cfg_params();
        }
        return false;
    }

    ui_track_config_t *config = &g_ui_track_state.track_configs[track];

    if (config->family == family)
    {
        if (!ui_track_type_is_valid_for_family(config->family, config->type))
        {
            config->type = ui_get_default_track_type_for_family(config->family);
        }

        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_cfg_params();
        }
        return true;
    }

    config->family = family;
    if (!ui_track_type_is_valid_for_family(config->family, config->type))
    {
        config->type = ui_get_default_track_type_for_family(config->family);
    }

    ui_core_sync_audio_runtime_enables();

    if (track == g_ui_track_state.active_track)
    {
        keyboard_runtime_on_active_track_changed();
        ui_core_sync_active_track_cfg_params();
    }

    return true;
}

bool ui_set_track_type(uint8_t track, ui_track_type_t type)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        return false;
    }

    ui_track_config_t *config = &g_ui_track_state.track_configs[track];
    if (!ui_track_type_is_valid_for_family(config->family, type))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_cfg_params();
        }
        return false;
    }

    config->type = type;

    if (track == g_ui_track_state.active_track)
    {
        keyboard_runtime_on_active_track_changed();
        ui_core_sync_active_track_cfg_params();
    }

    return true;
}

uint8_t ui_count_tracks_with_family(ui_track_family_t family)
{
    uint8_t count = 0U;

    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; track++)
    {
        if (g_ui_track_state.track_configs[track].family == family)
        {
            count++;
        }
    }

    return count;
}

const char *ui_get_track_family_display_name(ui_track_family_t family)
{
    switch (family)
    {
        case UI_TRACK_FAMILY_OFF:
            return "Off";

        case UI_TRACK_FAMILY_INPUT1:
            return "Input1";

        case UI_TRACK_FAMILY_INPUT2:
            return "Input2";

        case UI_TRACK_FAMILY_INPUT3:
            return "Input3";

        case UI_TRACK_FAMILY_INPUT4:
            return "Input4";

        case UI_TRACK_FAMILY_SYNTH:
            return "Synth";

        default:
            return "Track";
    }
}

const char *ui_get_track_family_short_name(ui_track_family_t family)
{
    switch (family)
    {
        case UI_TRACK_FAMILY_OFF:
            return "Off";

        case UI_TRACK_FAMILY_INPUT1:
            return "In1";

        case UI_TRACK_FAMILY_INPUT2:
            return "In2";

        case UI_TRACK_FAMILY_INPUT3:
            return "In3";

        case UI_TRACK_FAMILY_INPUT4:
            return "In4";

        case UI_TRACK_FAMILY_SYNTH:
            return "Syn";

        default:
            return "---";
    }
}

const char *ui_get_track_type_display_name(ui_track_family_t family, ui_track_type_t type)
{
    if (!ui_track_type_is_valid_for_family(family, type))
    {
        return "-";
    }

    switch (type)
    {
        case UI_TRACK_TYPE_AUDIO:
            return "Audio";

        case UI_TRACK_TYPE_HYBRID:
            return "Hybrid";

        case UI_TRACK_TYPE_DX7:
            return "DX7";

        case UI_TRACK_TYPE_MONOB:
            return "MonoB";

        default:
            return "-";
    }
}

const char *ui_get_track_type_short_name(ui_track_family_t family, ui_track_type_t type)
{
    if (!ui_track_type_is_valid_for_family(family, type))
    {
        return "---";
    }

    switch (type)
    {
        case UI_TRACK_TYPE_AUDIO:
            return "Aud";

        case UI_TRACK_TYPE_HYBRID:
            return "Hyb";

        case UI_TRACK_TYPE_DX7:
            return "DX7";

        case UI_TRACK_TYPE_MONOB:
            return "MB";

        default:
            return "---";
    }
}

void ui_get_track_runtime_header_label(uint8_t track, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if ((track == g_ui_track_state.active_track)
        && (g_ui_track_state.feedback_message[0] != '\0')
        && ((int32_t)(g_ui_track_state.feedback_until_ms - HAL_GetTick()) > 0))
    {
        (void)snprintf(out, out_len, "%s", g_ui_track_state.feedback_message);
        return;
    }

    const ui_track_config_t config = ui_get_track_config(track);

    if (config.family == UI_TRACK_FAMILY_OFF)
    {
        (void)snprintf(out, out_len, "Off");
        return;
    }

    if (config.family == UI_TRACK_FAMILY_SYNTH)
    {
        (void)snprintf(out, out_len, "%s", ui_get_track_type_display_name(config.family, config.type));
        return;
    }

    if (config.type == UI_TRACK_TYPE_HYBRID)
    {
        (void)snprintf(out, out_len, "%s %s",
                       ui_get_track_family_short_name(config.family),
                       ui_get_track_type_short_name(config.family, config.type));
        return;
    }

    (void)snprintf(out, out_len, "%s", ui_get_track_family_short_name(config.family));
}

ui_hall_mode_t ui_get_hall_mode(void)
{
    return g_ui_track_state.hall_mode;
}

static const char *ui_format_transposed_hall_mode_short_label(const char *base_label)
{
    static char label[8];
    const int8_t octave_shift = keyboard_runtime_get_octave_shift();

    if ((base_label == 0) || (octave_shift == 0))
    {
        return base_label;
    }

    (void)snprintf(label, sizeof(label), "%s%+d", base_label, (int)octave_shift);
    return label;
}

void ui_set_hall_mode(ui_hall_mode_t mode)
{
    if ((uint8_t)mode >= (uint8_t)UI_HALL_MODE_COUNT)
    {
        return;
    }

    if (g_ui_track_state.hall_mode == mode)
    {
        return;
    }

    if ((g_ui_track_state.hall_mode == UI_HALL_MODE_KEYBOARD) && (mode == UI_HALL_MODE_SEQ))
    {
        keyboard_runtime_all_notes_off();
    }

    keyboard_runtime_on_hall_mode_changed(g_ui_track_state.hall_mode, mode);
    g_ui_track_state.hall_mode = mode;
}

const char *ui_get_hall_mode_short_label(void)
{
    if (g_ui_track_state.hall_mode == UI_HALL_MODE_KEYBOARD)
    {
        return ui_format_transposed_hall_mode_short_label("KBD");
    }

    if (g_ui_track_state.hall_mode == UI_HALL_MODE_ARP)
    {
        return ui_format_transposed_hall_mode_short_label("ARP");
    }

    return "SEQ";
}

uint8_t ui_core_hall_note_is_suppressed(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_ui_track_state.hall_note_suppressed[hall];
}

void ui_core_clear_hall_note_suppression(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return;
    }

    g_ui_track_state.hall_note_suppressed[hall] = 0U;
}
