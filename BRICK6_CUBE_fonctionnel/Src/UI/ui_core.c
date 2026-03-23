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
#include "ui_event.h"
#include "ui_navigation.h"
#include "ui_page_manager.h"
#include "ui_param.h"
#include "ui_template_page.h"
#include "App/Hall/hall_calibration.h"

typedef struct
{
    uint8_t active_track;
    uint8_t shift_down;
    uint8_t track_select_armed;
} ui_track_state_t;

static ui_track_state_t g_ui_track_state = {
    .active_track = 0U,
    .shift_down = 0U,
    .track_select_armed = 0U,
};

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
        g_ui_track_state.active_track = ev->id;
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
    g_ui_track_state.shift_down = 0U;
    g_ui_track_state.track_select_armed = 0U;

    ui_template_family_registry_init();
    ui_page_template_filter_register_families();

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

    return UI_TRACK_TYPE_AUDIO;
}
