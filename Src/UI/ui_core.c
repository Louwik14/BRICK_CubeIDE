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
#include "pages/ui_page_settings.h"
#include "pages/ui_page_audio_rec.h"
#include "pages/ui_page_patch_assign.h"
#include "pages/ui_page_kit_assign.h"
#include "pages/ui_page_name_edit.h"
#include "Storage/sample_capture.h"
#include "ui_bootstrap.h"
#include "ui_event.h"
#include "ui_core_navigation_bridge.h"
#include "ui_hall_input_service.h"
#include "ui_hall_mode_state.h"
#include "ui_hall_mode_flow.h"
#include "ui_macro_interaction.h"
#include "ui_track_catalog.h"
#include "ui_core_clipboard.h"
#include "ui_core_feedback.h"
#include "ui_core_mute.h"
#include "ui_core_pattern.h"
#include "ui_core_runtime_bridge.h"
#include "ui_page_manager.h"
#include "ui_param.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "App/Hall/hall_engine.h"

#define UI_TRACK_MOD_BUTTON BTN_PARAM_8

typedef struct
{
    uint8_t active_track;
    uint8_t shift_down;
    uint8_t track_select_armed;
    uint32_t mode_tap_ms[UI_HALL_MODE_COUNT];
    uint32_t cfg_tap_ms[UI_TRACK_COUNT];
    uint8_t hall_prev_pressed[HALL_KEY_COUNT];
    uint8_t hall_note_suppressed[HALL_KEY_COUNT];
    uint8_t macro_overlay_active;
    uint8_t macro_overlay_latched;
    ui_macro_overlay_submode_t macro_overlay_submode;
    ui_macro_overlay_submode_t macro_overlay_last_submode;
    uint8_t macro_overlay_latch_repress_armed;
    uint8_t macro_overlay_shift_prev_down;
    uint8_t macro_overlay_track_prev_down;
} ui_track_state_t;

static ui_track_state_t g_ui_track_state = {
    .active_track = 0U,
    .shift_down = 0U,
    .track_select_armed = 0U,
    .mode_tap_ms = { 0U },
    .cfg_tap_ms = { 0U },
    .hall_prev_pressed = { 0U },
    .hall_note_suppressed = { 0U },
    .macro_overlay_active = 0U,
    .macro_overlay_latched = 0U,
    .macro_overlay_submode = UI_MACRO_OVERLAY_SUBMODE_CTRL,
    .macro_overlay_last_submode = UI_MACRO_OVERLAY_SUBMODE_CTRL,
    .macro_overlay_latch_repress_armed = 0U,
    .macro_overlay_shift_prev_down = 0U,
    .macro_overlay_track_prev_down = 0U,
};

static void ui_core_set_active_track(uint8_t track);


static void ui_core_mute_suppress_hall_note(uint8_t hall)
{
    if (hall < HALL_KEY_COUNT)
    {
        g_ui_track_state.hall_note_suppressed[hall] = 1U;
    }
}

static uint8_t ui_core_handle_mute_event(const ui_event_t *ev)
{
    return ui_core_mute_handle_event(ev,
                                     &g_ui_track_state.shift_down,
                                     g_ui_track_state.track_select_armed,
                                     ui_get_hall_mode,
                                     ui_set_hall_mode,
                                     ui_core_mute_suppress_hall_note);
}


static ui_track_config_t ui_core_get_default_track_config(void)
{
    ui_track_config_t config = {
        .family = UI_TRACK_FAMILY_OFF,
        .type = UI_TRACK_TYPE_AUDIO,
    };

    return config;
}

static const ui_track_config_t *ui_core_get_track_configs(void)
{
    return track_state_get_configs();
}

bool ui_track_family_is_input(ui_track_family_t family)
{
    return ui_track_catalog_family_is_input(family);
}

bool ui_track_family_is_engine(ui_track_family_t family)
{
    return ui_track_catalog_family_is_engine(family);
}

bool ui_track_type_is_valid_for_family(ui_track_family_t family, ui_track_type_t type)
{
    return ui_track_catalog_type_is_valid_for_family(family, type);
}

bool ui_track_type_is_available(uint8_t track, ui_track_family_t family, ui_track_type_t type)
{
    return ui_track_catalog_type_is_available(track, family, type, ui_core_get_track_configs());
}

ui_track_type_t ui_get_default_track_type_for_family(ui_track_family_t family)
{
    return ui_track_catalog_default_type_for_family(family);
}

uint8_t ui_get_track_type_count_for_family(ui_track_family_t family)
{
    return ui_track_catalog_type_count_for_family(family,
                                                  g_ui_track_state.active_track,
                                                  ui_core_get_track_configs());
}

uint8_t ui_get_track_type_index_for_family(ui_track_family_t family, ui_track_type_t type)
{
    return ui_track_catalog_type_index_for_family(family,
                                                  type,
                                                  g_ui_track_state.active_track,
                                                  ui_core_get_track_configs());
}

ui_track_type_t ui_get_track_type_from_family_index(ui_track_family_t family, uint8_t index)
{
    return ui_track_catalog_type_from_family_index(family,
                                                   index,
                                                   g_ui_track_state.active_track,
                                                   ui_core_get_track_configs());
}

static bool ui_core_track_family_is_available(uint8_t track, ui_track_family_t family)
{
    return ui_track_catalog_family_is_available(track, family, ui_core_get_track_configs());
}

static uint8_t ui_core_select_active_track(uint8_t track)
{
    if ((track >= UI_TRACK_COUNT) || (g_ui_track_state.active_track == track))
    {
        return 0U;
    }

    g_ui_track_state.active_track = track;
    return 1U;
}

static void ui_core_set_active_track(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return;
    }

    if (g_ui_track_state.active_track == track)
    {
        ui_core_runtime_bridge_sync_active_track_context(0U);
        return;
    }

    (void)ui_core_select_active_track(track);
    ui_core_runtime_bridge_sync_active_track_context(1U);
}

uint8_t ui_get_track_midi_channel(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 1U;
    }

    return track_state_get_midi_channel(track);
}

bool ui_set_track_midi_channel(uint8_t track, uint8_t channel_1_16)
{
    if ((track >= UI_TRACK_COUNT) || (channel_1_16 < 1U) || (channel_1_16 > 16U))
    {
        return false;
    }

    if (track_state_set_track_midi_channel(track, channel_1_16) == false)
    {
        return false;
    }
    track_runtime_invalidate_track(track);
    if (track == g_ui_track_state.active_track)
    {
        ui_core_runtime_bridge_sync_active_track_midi_channel();
    }
    return true;
}

ui_track_midi_source_t ui_get_track_midi_source(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return UI_TRACK_MIDI_SRC_ALL;
    }

    return track_state_get_midi_source(track);
}

bool ui_set_track_midi_source(uint8_t track, ui_track_midi_source_t source)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)source >= (uint8_t)UI_TRACK_MIDI_SRC_COUNT))
    {
        return false;
    }

    if (track_state_set_track_midi_source(track, source) == false)
    {
        return false;
    }
    track_runtime_invalidate_track(track);
    if (track == g_ui_track_state.active_track)
    {
        ui_core_runtime_bridge_sync_active_track_midi_source();
    }
    return true;
}

static bool ui_apply_track_config_bulk_mutation_internal(const uint8_t family[UI_TRACK_COUNT],
                                                         const uint8_t type[UI_TRACK_COUNT],
                                                         const uint8_t midi_channel[UI_TRACK_COUNT],
                                                         const uint8_t midi_source[UI_TRACK_COUNT])
{
    if ((family == 0) || (type == 0) || (midi_channel == 0) || (midi_source == 0))
    {
        return false;
    }

    return track_state_apply_bulk(family, type, midi_channel, midi_source);
}

bool ui_apply_track_config_bulk_mutation(const uint8_t family[UI_TRACK_COUNT],
                                         const uint8_t type[UI_TRACK_COUNT],
                                         const uint8_t midi_channel[UI_TRACK_COUNT],
                                         const uint8_t midi_source[UI_TRACK_COUNT])
{
    return ui_apply_track_config_bulk_mutation_internal(family, type, midi_channel, midi_source);
}

bool ui_restore_track_config_bulk(const uint8_t family[UI_TRACK_COUNT],
                                  const uint8_t type[UI_TRACK_COUNT],
                                  const uint8_t midi_channel[UI_TRACK_COUNT],
                                  const uint8_t midi_source[UI_TRACK_COUNT])
{
    return ui_core_runtime_bridge_restore_track_config_bulk(family,
                                                            type,
                                                            midi_channel,
                                                            midi_source,
                                                            ui_core_runtime_bridge_post_track_structure_change);
}

uint8_t ui_track_midi_channel_used_by_other(uint8_t track, uint8_t channel_1_16)
{
    if ((track >= UI_TRACK_COUNT) || (channel_1_16 < 1U) || (channel_1_16 > 16U))
    {
        return 0U;
    }

    for (uint8_t other = 0U; other < UI_TRACK_COUNT; ++other)
    {
        if (other == track)
        {
            continue;
        }

        if (ui_get_track_midi_channel(other) == channel_1_16)
        {
            return 1U;
        }
    }

    return 0U;
}

static void ui_core_update_shift_state(uint8_t shift_down)
{
    if ((shift_down != 0U) && (g_ui_track_state.shift_down == 0U))
    {
        g_ui_track_state.shift_down = 1U;
        return;
    }

    if ((shift_down == 0U) && (g_ui_track_state.shift_down != 0U))
    {
        g_ui_track_state.shift_down = 0U;
    }
}

static void ui_core_update_track_modifier_state(uint8_t track_modifier_down)
{
    g_ui_track_state.track_select_armed = (track_modifier_down != 0U) ? 1U : 0U;
}

static void ui_core_macro_overlay_reset(void)
{
    if (g_ui_track_state.macro_overlay_active != 0U)
    {
        ui_macro_interaction_reset();
    }

    g_ui_track_state.macro_overlay_active = 0U;
    g_ui_track_state.macro_overlay_latched = 0U;
    g_ui_track_state.macro_overlay_latch_repress_armed = 0U;
}

static void ui_core_macro_overlay_cycle_submode(void)
{
    g_ui_track_state.macro_overlay_submode =
        (g_ui_track_state.macro_overlay_submode == UI_MACRO_OVERLAY_SUBMODE_CTRL)
            ? UI_MACRO_OVERLAY_SUBMODE_ASSIGN
            : UI_MACRO_OVERLAY_SUBMODE_CTRL;
    g_ui_track_state.macro_overlay_last_submode = g_ui_track_state.macro_overlay_submode;
    ui_macro_interaction_reset();
}

static void ui_core_macro_overlay_activate_last_submode(void)
{
    g_ui_track_state.macro_overlay_submode = g_ui_track_state.macro_overlay_last_submode;
    g_ui_track_state.macro_overlay_active = 1U;
}

static void ui_core_service_macro_overlay_inputs(uint8_t shift_down, uint8_t track_modifier_down)
{
    const uint8_t shift_pressed =
        ((shift_down != 0U) && (g_ui_track_state.macro_overlay_shift_prev_down == 0U)) ? 1U : 0U;
    const uint8_t shift_released =
        ((shift_down == 0U) && (g_ui_track_state.macro_overlay_shift_prev_down != 0U)) ? 1U : 0U;
    const uint8_t track_pressed =
        ((track_modifier_down != 0U) && (g_ui_track_state.macro_overlay_track_prev_down == 0U)) ? 1U : 0U;

    if ((shift_down != 0U) && (track_pressed != 0U))
    {
        if (g_ui_track_state.macro_overlay_active != 0U)
        {
            ui_core_macro_overlay_cycle_submode();
        }
        else
        {
            ui_core_macro_overlay_activate_last_submode();
        }
    }

    if ((track_modifier_down != 0U)
            && (shift_released != 0U)
            && (g_ui_track_state.macro_overlay_active != 0U))
    {
        g_ui_track_state.macro_overlay_latch_repress_armed = 1U;
    }

    if ((track_modifier_down != 0U)
            && (shift_pressed != 0U)
            && (g_ui_track_state.macro_overlay_latch_repress_armed != 0U))
    {
        g_ui_track_state.macro_overlay_active = 1U;
        g_ui_track_state.macro_overlay_latched = 1U;
        g_ui_track_state.macro_overlay_latch_repress_armed = 0U;
    }

    if ((track_modifier_down == 0U) && (shift_down == 0U))
    {
        g_ui_track_state.macro_overlay_latch_repress_armed = 0U;
        if (g_ui_track_state.macro_overlay_latched == 0U)
        {
            ui_core_macro_overlay_reset();
        }
    }

    g_ui_track_state.macro_overlay_shift_prev_down = shift_down;
    g_ui_track_state.macro_overlay_track_prev_down = track_modifier_down;
}

static void ui_core_handle_track_selection_event(const ui_event_t *ev)
{
    /*
     * Modifier-mirror contract:
     * - This queued path mirrors SHIFT/TRACK_MOD button events.
     * - It intentionally coexists with out-of-queue mirror updates in
     *   ui_core_service_track_selection_inputs() so both direct hall actions
     *   and queued handlers observe fresh modifier state.
     */
    if (ev == 0)
    {
        return;
    }

    if (ui_core_mute_is_active() != 0U)
    {
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_SHIFT))
    {
        g_ui_track_state.shift_down = 1U;
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_RELEASE) && (ev->id == (uint8_t)BTN_SHIFT))
    {
        g_ui_track_state.shift_down = 0U;
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)UI_TRACK_MOD_BUTTON))
    {
        g_ui_track_state.track_select_armed = 1U;
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_RELEASE) && (ev->id == (uint8_t)UI_TRACK_MOD_BUTTON))
    {
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
    ui_core_feedback_set(message, HAL_GetTick());
}

static void ui_core_transport_enter_pattern(ui_pattern_mode_t mode)
{
    ui_core_pattern_enter(mode, ui_get_hall_mode(), ui_set_hall_mode);
}

static uint8_t ui_core_is_track_hall_event_consumed(const ui_event_t *ev)
{
    if ((ev == 0)
        || (ui_core_mute_is_active() != 0U)
        || (g_ui_track_state.track_select_armed == 0U)
        || (g_ui_track_state.shift_down != 0U)
        || (ui_get_hall_mode() == UI_HALL_MODE_PATTERN))
    {
        return 0U;
    }

    if ((ev->type != UI_EVENT_HALL_PRESS) && (ev->type != UI_EVENT_HALL_RELEASE))
    {
        return 0U;
    }

    return (ev->id < HALL_KEY_COUNT) ? 1U : 0U;
}

static uint8_t ui_core_handle_routing_event(const ui_event_t *ev)
{
    return ui_core_runtime_bridge_handle_routing_event(ev,
                                                       ui_get_active_track(),
                                                       ui_get_hall_mode(),
                                                       g_ui_track_state.track_select_armed,
                                                       ui_core_mute_suppress_hall_note);
}

static uint8_t ui_core_handle_transport_event(const ui_event_t *ev)
{
    return ui_core_runtime_bridge_handle_transport_event(ev,
                                                          ui_core_mute_is_active(),
                                                          g_ui_track_state.shift_down,
                                                          g_ui_track_state.track_select_armed,
                                                          ui_core_transport_enter_pattern,
                                                          ui_core_set_feedback);
}

uint8_t ui_core_request_undo(void)
{
    return ui_core_runtime_bridge_request_undo(ui_core_set_feedback);
}

static uint8_t ui_core_handle_pattern_mode_event(const ui_event_t *ev)
{
    return ui_core_runtime_bridge_handle_pattern_mode_event(ev,
                                                            ui_get_hall_mode(),
                                                            g_ui_track_state.shift_down,
                                                            g_ui_track_state.track_select_armed,
                                                            ui_set_hall_mode,
                                                            ui_core_set_feedback);
}

static uint8_t ui_core_handle_global_shortcuts(const ui_event_t *ev)
{
    return ui_core_runtime_bridge_handle_global_shortcuts(ev,
                                                          g_ui_track_state.shift_down,
                                                          g_ui_track_state.track_select_armed,
                                                          ui_core_mute_is_active(),
                                                          ui_core_request_undo,
                                                          ui_core_set_feedback);
}

static uint8_t ui_core_handle_seq_mode_event(const ui_event_t *ev)
{
    return ui_core_runtime_bridge_handle_seq_mode_event(ev,
                                                        ui_get_hall_mode(),
                                                        g_ui_track_state.shift_down,
                                                        ui_core_set_feedback);
}

static uint8_t ui_core_handle_macro_mode_event(const ui_event_t *ev)
{
    const uint8_t macro_context =
        (uint8_t)((g_ui_track_state.macro_overlay_active != 0U)
                  && (ui_core_mute_is_active() == 0U));
    if ((ev == 0) || (macro_context == 0U))
    {
        return 0U;
    }

    if ((ev->type != UI_EVENT_HALL_PRESS) && (ev->type != UI_EVENT_HALL_RELEASE))
    {
        return 0U;
    }

    if (ev->id >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return 1U;
}

void ui_core_init(void)
{
    ui_core_clipboard_init();
    ui_core_feedback_init();
    ui_core_pattern_init();
    ui_core_runtime_bridge_init();
    sample_capture_model_init();
    ui_macro_interaction_init();
    track_state_init();
    g_ui_track_state.active_track = 0U;
    g_ui_track_state.shift_down = 0U;
    g_ui_track_state.track_select_armed = 0U;
    g_ui_track_state.macro_overlay_active = 0U;
    g_ui_track_state.macro_overlay_latched = 0U;
    g_ui_track_state.macro_overlay_submode = UI_MACRO_OVERLAY_SUBMODE_CTRL;
    g_ui_track_state.macro_overlay_last_submode = UI_MACRO_OVERLAY_SUBMODE_CTRL;
    g_ui_track_state.macro_overlay_latch_repress_armed = 0U;
    g_ui_track_state.macro_overlay_shift_prev_down = 0U;
    g_ui_track_state.macro_overlay_track_prev_down = 0U;
    ui_core_mute_init();
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

    ui_core_runtime_bridge_sync_active_track_mirror();

    ui_bootstrap_init();
}

void ui_core_service_track_selection_inputs(void)
{
    /*
     * Out-of-queue contract:
     * - Runs in superloop before hall_keyboard_bridge_process() and before ui event
     *   queue dispatch in ui_core_tick().
     * - Keeps modifier mirrors (shift_down / track_select_armed) coherent with raw
     *   button state, so downstream queued handlers read fresh flags.
     */
    const uint8_t mute_active = (ui_core_mute_is_active() != 0U) ? 1U : 0U;
    const uint8_t shift_down = button_down(BTN_SHIFT);
    const uint8_t track_modifier_down = (mute_active == 0U) ? button_down(UI_TRACK_MOD_BUTTON) : 0U;
    ui_core_update_shift_state(shift_down);
    if (mute_active == 0U)
    {
        ui_core_update_track_modifier_state(track_modifier_down);
    }
    else
    {
        g_ui_track_state.track_select_armed = 0U;
    }
    ui_core_service_macro_overlay_inputs(shift_down, track_modifier_down);

    for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
    {
        const uint8_t pressed = hall_engine_is_pressed(hall);
        const uint8_t was_pressed = g_ui_track_state.hall_prev_pressed[hall];
        ui_hall_input_service_handle_hall(hall,
                                          pressed,
                                          was_pressed,
                                          ui_get_hall_mode(),
                                          g_ui_track_state.shift_down,
                                          g_ui_track_state.track_select_armed,
                                          mute_active,
                                          g_ui_track_state.hall_prev_pressed,
                                          g_ui_track_state.mode_tap_ms,
                                          g_ui_track_state.cfg_tap_ms,
                                          g_ui_track_state.hall_note_suppressed,
                                          ui_core_set_active_track,
                                          ui_core_set_feedback);
        g_ui_track_state.hall_prev_pressed[hall] = pressed;
    }

    const uint8_t active_track = ui_get_active_track();
    ui_hall_input_service_handle_transpose(g_ui_track_state.shift_down,
                                           g_ui_track_state.track_select_armed,
                                           active_track);
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
    typedef uint8_t (*ui_core_tick_stage_fn_t)(const ui_event_t *ev);
    typedef struct
    {
        ui_core_tick_stage_fn_t handler;
        uint8_t consumes_event;
        uint8_t blocks_downstream;
    } ui_core_tick_stage_t;

    static const ui_core_tick_stage_t k_event_stages[] = {
        { ui_core_handle_mute_event, 1U, 1U },
        { ui_core_is_track_hall_event_consumed, 1U, 1U },
        { ui_core_handle_routing_event, 1U, 1U },
        { ui_core_handle_transport_event, 1U, 1U },
        { ui_page_settings_handle_event, 1U, 1U },
        /* Intentionally before pattern/seq: global shortcuts can fully mask them. */
        { ui_core_handle_global_shortcuts, 1U, 1U },
        { ui_core_handle_pattern_mode_event, 1U, 1U },
        { ui_core_handle_macro_mode_event, 1U, 1U },
        { ui_core_handle_seq_mode_event, 1U, 1U },
    };

    ui_event_t ev;
    ui_param_encoder_context_t encoder_ctx;

    ui_param_capture_encoder_context(&encoder_ctx);
    track_runtime_refresh_track(encoder_ctx.active_track);
    param_registry_batch_begin();
    ui_param_begin_encoder_edit_group(&encoder_ctx);

    for (uint8_t encoder = 0U; encoder < (uint8_t)ENC_COUNT; encoder++)
    {
        const int16_t delta = encoder_consume_delta(encoder);
        if (ui_page_settings_is_open() != 0U)
        {
            ui_page_settings_handle_encoder(encoder, delta);
        }
        else if (ui_page_name_edit_is_open() != 0U)
        {
            (void)ui_page_name_edit_handle_encoder(encoder, delta);
        }
        else if (ui_page_patch_assign_is_open() != 0U)
        {
            (void)ui_page_patch_assign_handle_encoder(encoder, delta);
        }
        else if (ui_page_kit_assign_is_open() != 0U)
        {
            (void)ui_page_kit_assign_handle_encoder(encoder, delta);
        }
        else if (ui_page_audio_rec_is_open() != 0U)
        {
            (void)ui_page_audio_rec_handle_encoder(encoder, delta);
        }
        else
        {
            if (ui_macro_interaction_note_encoder_delta_with_context(&encoder_ctx, encoder, delta) == 0U)
            {
                ui_param_handle_encoder_with_context(&encoder_ctx, encoder, delta);
            }
        }
    }

    ui_param_end_encoder_edit_group();
    param_registry_batch_end();

    ui_event_from_inputs();
    ui_core_runtime_bridge_update_seq_step_hold();

    while (ui_event_pop(&ev))
    {
        /* Must stay first: updates shift/track modifier state consumed by later stages. */
        ui_core_handle_track_selection_event(&ev);

        for (uint8_t stage = 0U; stage < (uint8_t)(sizeof(k_event_stages) / sizeof(k_event_stages[0])); ++stage)
        {
            const ui_core_tick_stage_t *const s = &k_event_stages[stage];
            if ((s->consumes_event != 0U)
                    && (s->blocks_downstream != 0U)
                    && (s->handler(&ev) != 0U))
            {
                goto next_event;
            }
        }

        /*
         * Navigation/page dispatch contract:
         * - navigation may change current page through the navigation bridge
         * - this same event is then dispatched to the page active after navigation
         * - navigation is intentionally non-consuming in this pipeline
         */
        ui_core_navigation_bridge_handle_event(&ev);

        const ui_page_t *dispatch_page = ui_page_get();
        if ((dispatch_page != 0) && (dispatch_page->handle_event != 0))
        {
            dispatch_page->handle_event(&ev);
        }

next_event:
        ;
    }

    ui_core_runtime_bridge_service_looper_record_control(0);
    ui_core_runtime_bridge_service_looper_export_feedback(ui_core_set_feedback);
    sample_capture_model_service();
    ui_hall_mode_flow_service_pending(HAL_GetTick());

    const ui_page_t *active_page = ui_page_get();
    if ((active_page != 0) && (active_page->tick != 0))
    {
        /* Page-local periodic work only; track context sync is explicit in dedicated sync APIs. */
        active_page->tick();
    }
}

uint8_t ui_get_active_track(void)
{
    return g_ui_track_state.active_track;
}

bool ui_resolve_filter_target_track(uint8_t *out_track_id)
{
    /* Consumer-edge refresh: filter routing uses a refreshed projection before the runtime resolver. */
    track_runtime_refresh_track(ui_get_active_track());
    if (ui_core_runtime_bridge_resolve_filter_target_track(out_track_id) == 0U)
    {
        return false;
    }
    return true;
}

ui_track_config_t ui_get_track_config(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return ui_core_get_default_track_config();
    }

    return track_state_get_config(track);
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
            ui_core_runtime_bridge_sync_active_track_context(0U);
        }
        return false;
    }

    const ui_track_config_t config = track_state_get_config(track);

    if (config.family == family)
    {
        if (ui_track_catalog_type_is_available(track, family, config.type, ui_core_get_track_configs()) == false)
        {
            const uint8_t active_track_touched = (track == g_ui_track_state.active_track) ? 1U : 0U;
            if (ui_core_runtime_bridge_apply_track_family_change(track,
                                                                 family,
                                                                 active_track_touched,
                                                                 ui_core_runtime_bridge_post_track_structure_change) == false)
            {
                return false;
            }
        }
        if (track == g_ui_track_state.active_track)
        {
            ui_core_runtime_bridge_sync_active_track_context(0U);
        }
        return true;
    }

    if ((family != UI_TRACK_FAMILY_OFF)
            && (ui_track_catalog_type_count_for_family(family,
                                                       track,
                                                       ui_core_get_track_configs()) == 0U))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_runtime_bridge_sync_active_track_context(0U);
        }
        return false;
    }

    const uint8_t active_track_touched = (track == g_ui_track_state.active_track) ? 1U : 0U;
    if (ui_core_runtime_bridge_apply_track_family_change(track,
                                                          family,
                                                          active_track_touched,
                                                          ui_core_runtime_bridge_post_track_structure_change) == false)
    {
        return false;
    }

    return true;
}

bool ui_set_track_type(uint8_t track, ui_track_type_t type)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        return false;
    }

    const ui_track_config_t config = track_state_get_config(track);
    if (!ui_track_type_is_valid_for_family(config.family, type))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_runtime_bridge_sync_active_track_context(0U);
        }
        return false;
    }

    if (!ui_track_type_is_available(track, config.family, type))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_runtime_bridge_sync_active_track_context(0U);
        }
        return false;
    }

    if (config.type == type)
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_runtime_bridge_sync_active_track_context(0U);
        }
        return true;
    }

    const uint8_t active_track_touched = (track == g_ui_track_state.active_track) ? 1U : 0U;
    if (ui_core_runtime_bridge_apply_track_type_change(track,
                                                       type,
                                                       active_track_touched,
                                                       ui_core_runtime_bridge_post_track_structure_change) == false)
    {
        return false;
    }

    return true;
}

uint8_t ui_count_tracks_with_family(ui_track_family_t family)
{
    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    return track_state_count_tracks_with_family(family);
}

const char *ui_get_track_family_display_name(ui_track_family_t family)
{
    return ui_track_catalog_family_display_name(family);
}

const char *ui_get_track_family_short_name(ui_track_family_t family)
{
    return ui_track_catalog_family_short_name(family);
}

const char *ui_get_track_type_display_name(ui_track_family_t family, ui_track_type_t type)
{
    return ui_track_catalog_type_display_name(family, type);
}

const char *ui_get_track_type_short_name(ui_track_family_t family, ui_track_type_t type)
{
    return ui_track_catalog_type_short_name(family, type);
}

void ui_get_track_runtime_header_label(uint8_t track, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if (ui_core_feedback_try_get_for_track(g_ui_track_state.active_track,
                                           track,
                                           HAL_GetTick(),
                                           out,
                                           out_len) != 0U)
    {
        return;
    }

    const ui_track_config_t config = ui_get_track_config(track);

    if (config.family == UI_TRACK_FAMILY_OFF)
    {
        (void)snprintf(out, out_len, "Off");
        return;
    }

    if (ui_track_family_is_engine(config.family))
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

void ui_get_pattern_stub_state(ui_pattern_stub_state_t *out_state)
{
    if (out_state == 0)
    {
        return;
    }

    ui_core_runtime_bridge_get_pattern_stub_state(out_state);
}

ui_mute_state_t ui_get_mute_state(void)
{
    return ui_core_mute_get_state();
}

uint8_t ui_get_mute_hall_led(uint8_t hall, ui_mute_hall_led_t *out_led)
{
    return ui_core_mute_get_hall_led(hall, out_led);
}

uint8_t ui_is_track_modifier_held(void)
{
    return g_ui_track_state.track_select_armed;
}

uint8_t ui_macro_overlay_is_active(void)
{
    return g_ui_track_state.macro_overlay_active;
}

uint8_t ui_macro_overlay_is_latched(void)
{
    return g_ui_track_state.macro_overlay_latched;
}

uint8_t ui_macro_overlay_get_submode(ui_macro_overlay_submode_t *out_submode)
{
    if ((out_submode == 0) || (g_ui_track_state.macro_overlay_active == 0U))
    {
        return 0U;
    }

    *out_submode = g_ui_track_state.macro_overlay_submode;
    return 1U;
}

void ui_macro_overlay_on_hall_mode_changed(void)
{
    ui_core_macro_overlay_reset();
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
