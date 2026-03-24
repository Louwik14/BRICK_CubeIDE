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
#include "ui_event.h"
#include "ui_navigation.h"
#include "ui_page_manager.h"
#include "ui_param.h"
#include "ui_template_page.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_engine.h"
#include "Keyboard/keyboard_runtime.h"
#include "param_store.h"
#include "audio_float.h"

#define UI_CFG_TRACK_PARAM ((param_id_t)PARAM_CFG_TRACK)
#define UI_CFG_TRACK_TYPE_PARAM ((param_id_t)PARAM_CFG_TRACK_TYPE)
#define UI_HALL_KEYBOARD_MODE_TRIGGER 8U
#define UI_HALL_ARP_MODE_TRIGGER 9U
#define UI_HALL_MODE_DOUBLE_TAP_MS 400U

typedef struct
{
    uint8_t active_track;
    uint8_t shift_down;
    uint8_t track_select_armed;
    ui_hall_mode_t hall_mode;
    uint32_t last_keyboard_mode_tap_ms;
    uint32_t last_arp_mode_tap_ms;
    ui_track_config_t track_configs[UI_TRACK_COUNT];
    uint8_t hall_prev_pressed[HALL_KEY_COUNT];
    uint8_t hall_note_suppressed[HALL_KEY_COUNT];
} ui_track_state_t;

static ui_track_state_t g_ui_track_state = {
    .active_track = 0U,
    .shift_down = 0U,
    .track_select_armed = 0U,
    .hall_mode = UI_HALL_MODE_SEQ,
    .last_keyboard_mode_tap_ms = 0U,
    .last_arp_mode_tap_ms = 0U,
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

static void ui_core_activate_keyboard_hall_mode(uint8_t open_keyboard_page)
{
    ui_set_hall_mode(UI_HALL_MODE_KEYBOARD);

    if (open_keyboard_page != 0U)
    {
        ui_page_set(UI_PAGE_TEMPLATE_KEYBOARD);
    }
}

static void ui_core_activate_arp_hall_mode(uint8_t open_arp_page)
{
    ui_set_hall_mode(UI_HALL_MODE_ARP);

    if (open_arp_page != 0U)
    {
        ui_page_set(UI_PAGE_TEMPLATE_ARP);
    }
}

static void ui_core_handle_shift_hall_action(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return;
    }

    g_ui_track_state.hall_note_suppressed[hall] = 1U;

    if (hall == UI_HALL_ARP_MODE_TRIGGER)
    {
        const uint32_t now = HAL_GetTick();
        const uint8_t is_double_tap = ((g_ui_track_state.last_keyboard_mode_tap_ms != 0U)
                                       && ((now - g_ui_track_state.last_keyboard_mode_tap_ms) <= UI_HALL_MODE_DOUBLE_TAP_MS)) ? 1U : 0U;
        g_ui_track_state.last_keyboard_mode_tap_ms = now;
        ui_core_activate_keyboard_hall_mode(is_double_tap);
        return;
    }

    if (hall == UI_HALL_ARP_MODE_TRIGGER)
    {
        const uint32_t now = HAL_GetTick();
        const uint8_t is_double_tap = ((g_ui_track_state.last_arp_mode_tap_ms != 0U)
                                       && ((now - g_ui_track_state.last_arp_mode_tap_ms) <= UI_HALL_MODE_DOUBLE_TAP_MS)) ? 1U : 0U;
        g_ui_track_state.last_arp_mode_tap_ms = now;
        ui_core_activate_arp_hall_mode(is_double_tap);
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
void ui_core_init(void)
{
    g_ui_track_state.active_track = 0U;
    ui_core_reset_track_configs();
    g_ui_track_state.shift_down = 0U;
    g_ui_track_state.track_select_armed = 0U;
    g_ui_track_state.hall_mode = UI_HALL_MODE_SEQ;
    g_ui_track_state.last_keyboard_mode_tap_ms = 0U;
    g_ui_track_state.last_arp_mode_tap_ms = 0U;

    for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
    {
        g_ui_track_state.hall_prev_pressed[hall] = 0U;
        g_ui_track_state.hall_note_suppressed[hall] = 0U;
    }

    ui_core_sync_active_track_cfg_params();

    ui_template_family_registry_init();
    ui_page_template_filter_register_families();
    ui_page_template_cfg_register_families();
    ui_page_template_dx7_register_families();
    ui_page_template_keyboard_register_families();
    ui_page_template_arp_register_families();

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
    ui_page_manager_register(&g_ui_page_template_filter);
    ui_page_manager_register(&g_ui_page_template_cfg);
    ui_page_manager_register(&g_ui_page_template_dx7);
    ui_page_manager_register(&g_ui_page_template_keyboard);
    ui_page_manager_register(&g_ui_page_template_arp);

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

    while (ui_event_pop(&ev))
    {
        ui_core_handle_track_selection_event(&ev);
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

static const char *ui_format_keyboard_hall_mode_short_label(void)
{
    static char label[8];
    const int8_t octave_shift = keyboard_runtime_get_octave_shift();

    if (octave_shift == 0)
    {
        return "KBD";
    }

    (void)snprintf(label, sizeof(label), "KBD%+d", (int)octave_shift);
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
        return ui_format_keyboard_hall_mode_short_label();
    }

    if (g_ui_track_state.hall_mode == UI_HALL_MODE_ARP)
    {
        return "ARP";
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
