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

#include "buttons.h"
#include "encoders.h"
#include "pages/ui_page_main.h"
#include "pages/ui_page_param_test.h"
#include "pages/ui_page_debug_hall.h"
#include "pages/ui_page_calibration.h"
#include "pages/ui_page_template_filter.h"
#include "pages/ui_page_template_dx7.h"
#include "pages/ui_page_template_cfg.h"
#include "ui_event.h"
#include "ui_navigation.h"
#include "ui_page_manager.h"
#include "ui_param.h"
#include "ui_template_page.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_engine.h"
#include "param_store.h"

#define UI_CFG_TRACK_PARAM ((param_id_t)PARAM_CFG_TRACK)
#define UI_CFG_TRACK_TYPE_PARAM ((param_id_t)PARAM_CFG_TRACK_TYPE)

typedef struct
{
    uint8_t active_track;
    uint8_t shift_down;
    uint8_t track_select_armed;
    ui_track_type_t track_types[UI_TRACK_COUNT];
    uint8_t hall_prev_pressed[UI_TRACK_COUNT];
    uint8_t hall_note_suppressed[UI_TRACK_COUNT];
} ui_track_state_t;

static ui_track_state_t g_ui_track_state = {
    .active_track = 0U,
    .shift_down = 0U,
    .track_select_armed = 0U,
    .track_types = {
        UI_TRACK_TYPE_AUDIO,
        UI_TRACK_TYPE_AUDIO,
        UI_TRACK_TYPE_AUDIO,
        UI_TRACK_TYPE_AUDIO,
        UI_TRACK_TYPE_SYNTH,
        UI_TRACK_TYPE_SYNTH,
        UI_TRACK_TYPE_SYNTH,
        UI_TRACK_TYPE_SYNTH,
    },
    .hall_prev_pressed = { 0U },
    .hall_note_suppressed = { 0U },
};

static void ui_core_sync_active_track_cfg_params(void)
{
    const uint8_t active_track = g_ui_track_state.active_track;
    const ui_track_type_t active_type = g_ui_track_state.track_types[active_track];

    param_store_set_active(UI_CFG_TRACK_PARAM, (float)active_track);
    param_store_set_active(UI_CFG_TRACK_TYPE_PARAM, (float)active_type);
}

static void ui_core_set_active_track(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return;
    }

    g_ui_track_state.active_track = track;
    ui_core_sync_active_track_cfg_params();
}

static void ui_core_reset_track_types(void)
{
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; track++)
    {
        g_ui_track_state.track_types[track] = (track < UI_AUDIO_TRACK_LIMIT) ? UI_TRACK_TYPE_AUDIO : UI_TRACK_TYPE_SYNTH;
    }
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

    if ((ev->type == UI_EVENT_HALL_PRESS)
            && (g_ui_track_state.shift_down != 0U)
            && (g_ui_track_state.track_select_armed != 0U)
            && (ev->id < UI_TRACK_COUNT))
    {
        ui_core_set_active_track(ev->id);
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
    ui_core_reset_track_types();
    g_ui_track_state.shift_down = 0U;
    g_ui_track_state.track_select_armed = 0U;

    for (uint8_t hall = 0U; hall < UI_TRACK_COUNT; hall++)
    {
        g_ui_track_state.hall_prev_pressed[hall] = 0U;
        g_ui_track_state.hall_note_suppressed[hall] = 0U;
    }

    ui_core_sync_active_track_cfg_params();

    ui_template_family_registry_init();
    ui_page_template_filter_register_families();
    ui_page_template_cfg_register_families();

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

    for (uint8_t hall = 0U; hall < UI_TRACK_COUNT; hall++)
    {
        const uint8_t pressed = hall_engine_is_pressed(hall);
        const uint8_t was_pressed = g_ui_track_state.hall_prev_pressed[hall];

        if ((was_pressed == 0U) && (pressed != 0U)
                && (g_ui_track_state.shift_down != 0U)
                && (g_ui_track_state.track_select_armed != 0U))
        {
            ui_core_set_active_track(hall);
            g_ui_track_state.hall_note_suppressed[hall] = 1U;
        }

        g_ui_track_state.hall_prev_pressed[hall] = pressed;
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

ui_track_type_t ui_get_track_type(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return UI_TRACK_TYPE_AUDIO;
    }

    return g_ui_track_state.track_types[track];
}

bool ui_set_track_type(uint8_t track, ui_track_type_t type)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        return false;
    }

    const ui_track_type_t current_type = g_ui_track_state.track_types[track];
    if (current_type == type)
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_cfg_params();
        }
        return true;
    }

    if ((type == UI_TRACK_TYPE_AUDIO) && (ui_count_tracks_of_type(UI_TRACK_TYPE_AUDIO) >= UI_AUDIO_TRACK_LIMIT))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_cfg_params();
        }
        return false;
    }

    g_ui_track_state.track_types[track] = type;

    if (track == g_ui_track_state.active_track)
    {
        ui_core_sync_active_track_cfg_params();
    }

    return true;
}

uint8_t ui_count_tracks_of_type(ui_track_type_t type)
{
    uint8_t count = 0U;

    if ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; track++)
    {
        if (g_ui_track_state.track_types[track] == type)
        {
            count++;
        }
    }

    return count;
}

const char *ui_get_track_type_display_name(ui_track_type_t type)
{
    switch (type)
    {
        case UI_TRACK_TYPE_AUDIO:
            return "Audio";

        case UI_TRACK_TYPE_SYNTH:
            return "Synth";

        case UI_TRACK_TYPE_MIDI:
            return "MIDI";

        case UI_TRACK_TYPE_CARD:
            return "Card";

        default:
            return "Track";
    }
}

const char *ui_get_track_type_short_name(ui_track_type_t type)
{
    switch (type)
    {
        case UI_TRACK_TYPE_AUDIO:
            return "AUD";

        case UI_TRACK_TYPE_SYNTH:
            return "SYN";

        case UI_TRACK_TYPE_MIDI:
            return "MID";

        case UI_TRACK_TYPE_CARD:
            return "CRD";

        default:
            return "---";
    }
}

uint8_t ui_core_hall_note_is_suppressed(uint8_t hall)
{
    if (hall >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    return g_ui_track_state.hall_note_suppressed[hall];
}

void ui_core_clear_hall_note_suppression(uint8_t hall)
{
    if (hall >= UI_TRACK_COUNT)
    {
        return;
    }

    g_ui_track_state.hall_note_suppressed[hall] = 0U;
}
