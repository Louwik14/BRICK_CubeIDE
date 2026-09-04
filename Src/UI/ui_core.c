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
#include "pages/ui_page_name_edit.h"
#include "pages/ui_page_template_keyboard.h"
#include "pages/ui_page_template_seq.h"
#include "pages/ui_page_template_play.h"
#include "pages/ui_page_template_macro.h"
#include "UI/pages/ui_page_template_cfg.h"
#include "Storage/sample_capture.h"
#include "Track/track_input_ownership.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "ui_bootstrap.h"
#include "ui_event.h"
#include "ui_navigation.h"
#include "ui_hall_input_service.h"
#include "ui_hall_mode_state.h"
#include "ui_hall_mode_flow.h"
#include "ui_macro_interaction.h"
#include "ui_track_catalog.h"
#include "ui_core_clipboard.h"
#include "ui_core_feedback.h"
#include "ui_core_mute.h"
#include "ui_core_pattern.h"
#include "ui_core_shortcuts.h"
#include "ui_core_seq_transport.h"
#include "ui_edit_context_sync.h"
#include "ui_active_track_sync.h"
#include "ui_page_manager.h"
#include "ui_param.h"
#include "UI/ui_sampler_playhead.h"
#include "App/encoder_control_dispatcher.h"
#include "Track/track_runtime.h"
#include "Track/track_state.h"
#include "Track/control_routing.h"
#include "Track/entity_topology.h"
#include "Keyboard/keyboard_runtime.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"
#include "Storage/audio_recorder.h"
#include "Storage/pattern_live_ram.h"
#include "App/control_domain.h"

#define UI_TRACK_MOD_BUTTON BTN_TRACK

typedef struct
{
    uint8_t active_track;
    uint8_t active_lane;
    uint8_t shift_down;
    uint8_t track_select_armed;
    uint32_t mode_tap_ms[UI_HALL_MODE_COUNT];
    uint32_t cfg_tap_ms[TRACK_COUNT];
    uint8_t hall_prev_pressed[HALL_UI_LANE_COUNT];
    uint8_t step_context_owner[16U];
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
    .active_lane = 0U,
    .shift_down = 0U,
    .track_select_armed = 0U,
    .mode_tap_ms = { 0U },
    .cfg_tap_ms = { 0U },
    .hall_prev_pressed = { 0U },
    .macro_overlay_active = 0U,
    .macro_overlay_latched = 0U,
    .macro_overlay_submode = UI_MACRO_OVERLAY_SUBMODE_CTRL,
    .macro_overlay_last_submode = UI_MACRO_OVERLAY_SUBMODE_CTRL,
    .macro_overlay_latch_repress_armed = 0U,
    .macro_overlay_shift_prev_down = 0U,
    .macro_overlay_track_prev_down = 0U,
};

static void ui_core_set_active_track(uint8_t track);
static void ui_core_set_feedback(const char *message);

static volatile uint32_t g_ui_hall_arbitration_seq;
static volatile ui_hall_arbitration_snapshot_t g_ui_hall_arbitration_snapshot;

static uint16_t ui_core_hall_ui_claim_mask(uint8_t pressed)
{
    (void)pressed;

    const uint16_t all_hall_lanes = (uint16_t)((1UL << HALL_UI_LANE_COUNT) - 1UL);
    const uint8_t active_track = ui_get_active_track();
    const ui_hall_mode_t raw_mode = ui_get_hall_mode();

    if ((ui_core_mute_is_active() != 0U)
        || ((g_ui_track_state.shift_down != 0U)
            && (raw_mode == UI_HALL_MODE_KEYBOARD))
        || (g_ui_track_state.track_select_armed != 0U)
        || (g_ui_track_state.macro_overlay_active != 0U)
        || (raw_mode == UI_HALL_MODE_PATTERN)
        || (ui_page_patch_assign_is_open() != 0U)
        || (ui_page_audio_rec_is_open() != 0U)
        || (ui_page_settings_is_open() != 0U)
        || ((ui_page_get_id() == UI_PAGE_MIDI_FX)
            && (ui_hall_mode_resolve_rout_context(active_track, raw_mode)
                != UI_HALL_ROUT_CONTEXT_NONE)))
    {
        return all_hall_lanes;
    }

    if (raw_mode == UI_HALL_MODE_SEQ)
    {
        return 0U;
    }

    const ui_hall_mode_t input_mode = (raw_mode == UI_HALL_MODE_MUTE)
        ? ui_core_mute_get_passthrough_hall_mode() : raw_mode;
    return (ui_hall_allows_injection(active_track, input_mode) == 0U)
        ? all_hall_lanes : 0U;
}

static void ui_core_publish_hall_arbitration_snapshot(void)
{
    ui_hall_arbitration_snapshot_t next = {
        .consume_press_mask = ui_core_hall_ui_claim_mask(1U),
        .consume_release_mask = ui_core_hall_ui_claim_mask(0U),
        .shift_down = g_ui_track_state.shift_down,
        .track_select_armed = g_ui_track_state.track_select_armed,
        .hall_mode = (uint8_t)ui_get_hall_mode(),
        .context_track = ui_get_active_lane(),
    };

    g_ui_hall_arbitration_seq++;
    __DMB();
    g_ui_hall_arbitration_snapshot = next;
    __DMB();
    g_ui_hall_arbitration_seq++;
    __DMB();
}


static uint8_t ui_core_handle_mute_event(const ui_event_t *ev)
{
    return ui_core_mute_handle_event(ev,
                                     &g_ui_track_state.shift_down,
                                     g_ui_track_state.track_select_armed,
                                     ui_get_hall_mode,
                                     ui_set_hall_mode);
}


static track_config_t ui_core_get_default_track_config(void)
{
    track_config_t config = {
        .family = TRACK_FAMILY_OFF,
        .type = TRACK_TYPE_NONE,
    };

    return config;
}

static const track_config_t *ui_core_get_track_configs(void)
{
    return track_state_get_configs();
}

bool ui_track_family_is_engine(track_family_t family)
{
    return ui_track_catalog_family_is_engine(family);
}

bool ui_track_type_is_valid_for_family(track_family_t family, track_type_t type)
{
    return ui_track_catalog_type_is_valid_for_family(family, type);
}

bool ui_track_type_is_available(uint8_t track, track_family_t family, track_type_t type)
{
    return ui_track_catalog_type_is_available(track, family, type, ui_core_get_track_configs());
}

track_type_t ui_get_default_track_type_for_family(track_family_t family)
{
    return ui_track_catalog_default_type_for_family(family);
}

uint8_t ui_get_track_type_count_for_family(track_family_t family)
{
    return ui_track_catalog_type_count_for_family(family,
                                                  g_ui_track_state.active_track,
                                                  ui_core_get_track_configs());
}

uint8_t ui_get_track_type_index_for_family(track_family_t family, track_type_t type)
{
    return ui_track_catalog_type_index_for_family(family,
                                                  type,
                                                  g_ui_track_state.active_track,
                                                  ui_core_get_track_configs());
}

track_type_t ui_get_track_type_from_family_index(track_family_t family, uint8_t index)
{
    return ui_track_catalog_type_from_family_index(family,
                                                   index,
                                                   g_ui_track_state.active_track,
                                                   ui_core_get_track_configs());
}

static bool ui_core_track_family_is_available(uint8_t track, track_family_t family)
{
    return ui_track_catalog_family_is_available(track, family, ui_core_get_track_configs());
}

static uint8_t ui_core_select_active_track(uint8_t track)
{
    if ((entity_topology_is_active(track) == 0U) || (g_ui_track_state.active_track == track))
    {
        return 0U;
    }

    g_ui_track_state.active_track = track;
    g_ui_track_state.active_lane = track;
    return 1U;
}

static void ui_core_set_active_track(uint8_t track)
{
    entity_topology_descriptor_t entity;
    if ((entity_topology_get((brick_entity_id_t)track, &entity) == 0U)
            || (entity.active == 0U))
    {
        return;
    }

    const uint8_t main_track = (entity.role == ENTITY_ROLE_GROUP_CHILD)
        ? (uint8_t)entity.parent_entity_id : track;
    const uint8_t changed = (uint8_t)((g_ui_track_state.active_track != main_track)
            || (g_ui_track_state.active_lane != track));
    if (changed == 0U)
    {
        ui_edit_context_sync_active_track(0U);
        return;
    }

    if (g_ui_track_state.active_track != main_track)
    {
        (void)ui_core_select_active_track(main_track);
    }
    g_ui_track_state.active_lane = track;
    ui_param_publish_encoder_binding(g_ui_track_state.active_lane,
                                     g_ui_track_state.shift_down);
    ui_edit_context_sync_active_track(1U);
}

void ui_restore_active_track(uint8_t track)
{
    ui_core_set_active_track(track);
}

uint8_t ui_get_track_midi_channel(uint8_t track)
{
    if (track >= BRICK_ENTITY_CAPACITY)
    {
        return 1U;
    }

    return track_state_get_midi_channel(track);
}

bool ui_set_track_midi_channel(uint8_t track, uint8_t channel_1_16)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (channel_1_16 < 1U) || (channel_1_16 > 16U))
    {
        return false;
    }

    const control_track_intent_t intent = {
        .operation = CONTROL_TRACK_SET_MIDI_CHANNEL,
        .track = track,
        .value0 = channel_1_16
    };
    return control_domain_request_track(&intent) != 0U;
}

track_midi_source_t ui_get_track_midi_source(uint8_t track)
{
    if (track >= BRICK_ENTITY_CAPACITY)
    {
        return TRACK_MIDI_SOURCE_ALL;
    }

    return track_state_get_midi_source(track);
}

uint8_t ui_get_track_external_input(uint8_t track)
{
    return track_state_get_external_input(track);
}

bool ui_set_track_external_input(uint8_t track, uint8_t input)
{
    if ((track >= TRACK_COUNT)
            || (input >= ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT)
            || (ui_get_track_family(track) != TRACK_FAMILY_EXTERNAL)
            || (ui_get_track_type(track) != TRACK_TYPE_EXTERNAL))
    {
        return false;
    }
    if (track_state_get_external_input(track) == input)
    {
        return true;
    }
    if (track_input_ownership_can_claim(track, input) == 0U)
    {
        uint8_t owner = TRACK_INPUT_OWNER_NONE;
        if ((track == g_ui_track_state.active_track)
                && (track_input_ownership_get_external_owner(input, &owner) != 0U))
        {
            char feedback[16];
            (void)snprintf(feedback, sizeof(feedback), "USED P%u", (unsigned int)(owner + 1U));
            ui_core_set_feedback(feedback);
        }
        return false;
    }
    const control_track_intent_t intent = {
        .operation = CONTROL_TRACK_SET_EXTERNAL_INPUT,
        .track = track,
        .value0 = input
    };
    if (control_domain_request_track(&intent) == 0U)
    {
        uint8_t owner = TRACK_INPUT_OWNER_NONE;
        if ((track == g_ui_track_state.active_track)
                && (track_input_ownership_get_external_owner(input, &owner) != 0U))
        {
            char feedback[16];
            (void)snprintf(feedback, sizeof(feedback), "USED P%u", (unsigned int)(owner + 1U));
            ui_core_set_feedback(feedback);
        }
        return false;
    }
    return true;
}

bool ui_set_track_midi_source(uint8_t track, track_midi_source_t source)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || ((uint8_t)source >= (uint8_t)TRACK_MIDI_SOURCE_COUNT))
    {
        return false;
    }

    const control_track_intent_t intent = {
        .operation = CONTROL_TRACK_SET_MIDI_SOURCE,
        .track = track,
        .value0 = (uint8_t)source
    };
    return control_domain_request_track(&intent) != 0U;
}

uint8_t ui_track_midi_channel_used_by_other(uint8_t track, uint8_t channel_1_16)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (channel_1_16 < 1U) || (channel_1_16 > 16U))
    {
        return 0U;
    }

    for (uint8_t other = 0U; other < BRICK_ENTITY_CAPACITY; ++other)
    {
        if ((other == track) || (entity_topology_is_active(other) == 0U))
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
    const uint8_t normalized = (shift_down != 0U) ? 1U : 0U;
    if (g_ui_track_state.shift_down != normalized)
    {
        g_ui_track_state.shift_down = normalized;
        ui_param_publish_encoder_binding(ui_get_active_lane(),
                                         g_ui_track_state.shift_down);
    }
}

static void ui_core_update_track_modifier_state(uint8_t track_modifier_down)
{
    const uint8_t normalized = (track_modifier_down != 0U) ? 1U : 0U;
    if (g_ui_track_state.track_select_armed != normalized)
    {
        g_ui_track_state.track_select_armed = normalized;
        ui_param_publish_encoder_binding(ui_get_active_lane(),
                                         g_ui_track_state.shift_down);
    }
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
        ui_param_publish_encoder_binding(ui_get_active_lane(),
                                         g_ui_track_state.shift_down);
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_RELEASE) && (ev->id == (uint8_t)BTN_SHIFT))
    {
        g_ui_track_state.shift_down = 0U;
        ui_param_publish_encoder_binding(ui_get_active_lane(),
                                         g_ui_track_state.shift_down);
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)UI_TRACK_MOD_BUTTON))
    {
        g_ui_track_state.track_select_armed = 1U;
        ui_param_publish_encoder_binding(ui_get_active_lane(),
                                         g_ui_track_state.shift_down);
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_RELEASE) && (ev->id == (uint8_t)UI_TRACK_MOD_BUTTON))
    {
        g_ui_track_state.track_select_armed = 0U;
        ui_param_publish_encoder_binding(ui_get_active_lane(),
                                         g_ui_track_state.shift_down);
        return;
    }
}

static uint8_t ui_core_handle_hall_input_event(const ui_event_t *ev)
{
    if ((ev == 0)
        || ((ev->type != UI_EVENT_HALL_PRESS)
            && (ev->type != UI_EVENT_HALL_RELEASE))
        || (ev->id >= HALL_UI_LANE_COUNT))
    {
        return 0U;
    }

    const uint8_t hall = ev->id;
    const uint8_t pressed = (ev->type == UI_EVENT_HALL_PRESS) ? 1U : 0U;
    const uint8_t was_pressed = g_ui_track_state.hall_prev_pressed[hall];
    const uint8_t consumed = ui_hall_input_service_handle_hall(
        hall,
        pressed,
        was_pressed,
        (ui_hall_mode_t)ev->hall_mode,
        ev->context_track,
        ev->shift_down,
        ev->track_select_armed,
        (ui_core_mute_is_active() != 0U) ? 1U : 0U,
        ev->capture_ms,
        g_ui_track_state.cfg_tap_ms,
        ui_core_set_active_track,
        ui_core_set_feedback);
    g_ui_track_state.hall_prev_pressed[hall] = pressed;
    return consumed;
}

static uint8_t ui_core_handle_step_context_event(const ui_event_t *ev)
{
    if ((ev == 0)
            || (ev->id < (uint8_t)BTN_STEP_1)
            || (ev->id > (uint8_t)BTN_STEP_16))
    {
        return 0U;
    }

    const uint8_t step = (uint8_t)(ev->id - (uint8_t)BTN_STEP_1);
    if (ev->type == UI_EVENT_BUTTON_RELEASE)
    {
        const uint8_t owner = g_ui_track_state.step_context_owner[step];
        g_ui_track_state.step_context_owner[step] = 0U;
        if ((owner == 1U) || (owner == 2U))
        {
            return 1U;
        }
        if (owner == 3U)
        {
            return (ui_core_seq_transport_handle_seq_mode_event(
                ev, UI_HALL_MODE_SEQ, 0U, ui_core_set_feedback) != 0U) ? 1U : 0U;
        }
    }

    if (ev->track_select_armed != 0U)
    {
        if (ev->type == UI_EVENT_BUTTON_PRESS)
        {
            g_ui_track_state.step_context_owner[step] = 1U;
            ui_core_set_active_track(step);
        }
        return 1U;
    }

    if (ev->shift_down != 0U)
    {
        if (ev->type == UI_EVENT_BUTTON_PRESS)
        {
            g_ui_track_state.step_context_owner[step] = 2U;
            (void)ui_hall_mode_flow_handle_shift_step(
                step, ev->capture_ms, g_ui_track_state.mode_tap_ms);
        }
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS)
            && (ev->hall_mode == (uint8_t)UI_HALL_MODE_SEQ))
    {
        g_ui_track_state.step_context_owner[step] = 3U;
    }

    return 0U;
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

static uint8_t ui_core_track_is_sampler_looper(uint8_t track)
{
    return (uint8_t)((track < BRICK_ENTITY_CAPACITY)
        && (track_state_get_family(track) == TRACK_FAMILY_SAMPLER)
        && (track_state_get_type(track) == TRACK_TYPE_LOOPER));
}

static uint8_t ui_core_handle_looper_save(void)
{
    const uint8_t track = ui_get_active_lane();
    if (ui_core_track_is_sampler_looper(track) == 0U)
    {
        return 0U;
    }

    audio_recorder_status_t status;
    if (audio_recorder_get_status_client(AUDIO_RECORDER_CLIENT_LOOPER, &status) == 0U)
    {
        ui_core_set_feedback("NO LOOP");
        return 1U;
    }
    if ((status.state == AUDIO_RECORDER_STATE_PREPARED)
            || (status.state == AUDIO_RECORDER_STATE_RECORDING)
            || (status.state == AUDIO_RECORDER_STATE_DRAINING)
            || (status.state == AUDIO_RECORDER_STATE_FINALIZING))
    {
        ui_core_set_feedback("LOOP BUSY");
        return 1U;
    }
    if ((status.state == AUDIO_RECORDER_STATE_FAILED)
            || (status.error != AUDIO_RECORDER_ERROR_NONE))
    {
        ui_core_set_feedback("LOOP FAIL");
        return 1U;
    }

    uint8_t take_track = 0xFFU;
    (void)audio_recorder_control_looper_take_track(&take_track);
    ui_core_set_feedback(((status.state == AUDIO_RECORDER_STATE_TAKE_READY)
            && (status.frames_committed != 0U)
            && (take_track == track)) ? "LOOP SAVED" : "NO LOOP");
    return 1U;
}

static uint8_t ui_core_handle_routing_event(const ui_event_t *ev)
{
    const uint8_t active_track = ui_get_active_track();
    if ((ev == 0) || (ui_core_track_is_sampler_looper(active_track) == 0U)
            || (ui_page_get_id() != UI_PAGE_MIDI_FX)
            || (ev->track_select_armed != 0U)
            || (ev->type != UI_EVENT_HALL_PRESS)
            || (ev->id >= HALL_UI_LANE_COUNT))
        return 0U;

    const uint8_t hall = ev->id;
    if ((hall >= BRICK_ENTITY_CAPACITY) || (hall == active_track))
    {
        return 1U;
    }

    const control_routing_intent_t intent = {
        .looper = active_track,
        .source = hall,
        .enabled = (control_routing_get_looper_source(active_track, hall) == 0U) ? 1U : 0U
    };
    (void)control_domain_request_routing(&intent);
    return 1U;
}

static uint8_t ui_core_handle_transport_event(const ui_event_t *ev)
{
    (void)ui_core_mute_is_active();
    if ((ev != 0) && (ev->type == UI_EVENT_BUTTON_PRESS)
            && (ev->id == (uint8_t)BTN_PLAY))
    {
        const control_seq_intent_t intent = {
            .operation = CONTROL_SEQ_TRANSPORT_TOGGLE
        };
        (void)control_domain_request_seq(&intent);
        return 1U;
    }
    if ((ev != 0) && (ev->type == UI_EVENT_BUTTON_PRESS)
            && (ev->id == (uint8_t)BTN_REC))
    {
        if (ev->shift_down != 0U)
        {
            ui_page_template_rec_cfg_open_main();
            ui_navigation_request_page_with_availability(UI_PAGE_TEMPLATE_REC_CFG);
        }
        else
        {
            const control_seq_intent_t intent = {
                .operation = CONTROL_SEQ_RECORD_TARGET_ARM,
                .track = ui_get_active_lane()
            };
            (void)control_domain_request_seq(&intent);
        }
        return 1U;
    }
    if ((ev != 0) && (ev->type == UI_EVENT_BUTTON_PRESS)
            && (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
            && ((ev->shift_down != 0U)
                || (ev->track_select_armed != 0U))
            && (ui_get_hall_mode() != UI_HALL_MODE_PATTERN))
    {
        ui_core_transport_enter_pattern(
            (ev->shift_down != 0U)
                ? UI_PATTERN_MODE_RECALL : UI_PATTERN_MODE_STORE);
        return 1U;
    }
    if ((ev != 0) && (ev->type == UI_EVENT_BUTTON_PRESS)
            && ((ev->id == (uint8_t)BTN_TRANSPOSE_UP)
                || (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN))
            && (ev->shift_down == 0U)
            && (ev->track_select_armed == 0U)
            && (ui_hall_allows_injection(ui_get_active_lane(),
                                         (ui_hall_mode_t)ev->hall_mode) != 0U))
    {
        const int8_t delta = (ev->id == (uint8_t)BTN_TRANSPOSE_UP) ? 1 : -1;
        (void)control_domain_request_keyboard(CONTROL_KEYBOARD_STEP_OCTAVE, delta);
        return 1U;
    }
    return 0U;
}

uint8_t ui_core_request_undo(void)
{
    return ui_core_shortcuts_request_undo(ui_core_set_feedback);
}

static uint8_t ui_core_handle_pattern_mode_event(const ui_event_t *ev)
{
    return ui_core_pattern_handle_mode_event(ev, (ui_hall_mode_t)ev->hall_mode,
                                             ev->shift_down,
                                             ev->track_select_armed,
                                             ui_set_hall_mode, ui_core_set_feedback);
}

static uint8_t ui_core_handle_global_shortcuts(const ui_event_t *ev)
{
    if ((ev != 0) && (ev->type == UI_EVENT_BUTTON_PRESS)
            && (ev->id == (uint8_t)BTN_SETTINGS)
            && (ev->shift_down != 0U)
            && (ev->track_select_armed == 0U)
            && (ui_core_mute_is_active() == 0U)
            && (ui_core_handle_looper_save() != 0U))
        return 1U;
    return ui_core_shortcuts_handle_global_event(ev,
        ev->shift_down, ev->track_select_armed,
        ui_core_mute_is_active(), ui_core_set_feedback);
}

static uint8_t ui_core_handle_seq_mode_event(const ui_event_t *ev)
{
    return ui_core_seq_transport_handle_seq_mode_event(
        ev, (ui_hall_mode_t)ev->hall_mode, ev->shift_down, ui_core_set_feedback);
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

    if (ev->id >= HALL_UI_LANE_COUNT)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t ui_core_handle_encoder_event(const ui_event_t *ev,
                                            ui_param_encoder_context_t *ctx)
{
    if ((ev == 0) || (ctx == 0) || (ev->type != UI_EVENT_ENCODER)
            || (ev->id >= (uint8_t)ENC_COUNT) || (ev->value == 0))
    {
        return 0U;
    }

    int16_t delta = ev->value;
    if (ev->id == (uint8_t)ENC_PARAM_A)
    {
        delta = (int16_t)-delta;
    }

    ui_param_capture_encoder_context_for_state(
        ctx, ev->context_track, ev->shift_down);

    if (ui_page_settings_is_open() != 0U)
    {
        ui_page_settings_handle_encoder(ev->id, delta);
        return 1U;
    }
    if (ui_page_name_edit_is_open() != 0U)
    {
        return ui_page_name_edit_handle_encoder(ev->id, delta);
    }
    if (ui_page_patch_assign_is_open() != 0U)
    {
        return ui_page_patch_assign_handle_encoder(ev->id, delta);
    }
    if (ui_page_audio_rec_is_open() != 0U)
    {
        return ui_page_audio_rec_handle_encoder(ev->id, delta);
    }
    if (ui_page_get_id() == UI_PAGE_TEMPLATE_MACRO)
    {
        ui_page_template_macro_handle_encoder(ev->id, delta);
        return 1U;
    }
    if (ui_page_template_keyboard_handle_encoder(ev->id, delta) != 0U)
    {
        return 1U;
    }
    if (ui_page_template_seq_handle_encoder(ev->id, delta) != 0U)
    {
        return 1U;
    }
    if (ui_page_template_play_handle_encoder(ev->id, delta) != 0U)
    {
        return 1U;
    }
    if (ui_page_template_cfg_handle_encoder(ev->id, delta) != 0U)
    {
        return 1U;
    }
    if (ui_page_template_rec_cfg_handle_encoder(ev->id, delta) != 0U)
    {
        return 1U;
    }
    if ((ui_page_get_id() == UI_PAGE_TEMPLATE_MOD)
            || (ui_page_get_id() == UI_PAGE_MIDI_FX)
            || (ui_page_get_id() == UI_PAGE_AUDIO_FX))
    {
        return 0U;
    }
    if (ui_macro_interaction_note_encoder_delta_with_context(
            ctx, ev->id, delta) != 0U)
    {
        return 1U;
    }
    (void)ui_param_handle_encoder_with_context(ctx, ev->id, delta);
    return 1U;
}

void ui_core_init(void)
{
    ui_sampler_playhead_init();
    ui_core_clipboard_init();
    ui_core_feedback_init();
    ui_core_pattern_init();
    sample_capture_model_init();
    ui_macro_interaction_init();
    g_ui_track_state.active_track = 0U;
    g_ui_track_state.active_lane = 0U;
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

    for (uint8_t hall = 0U; hall < HALL_UI_LANE_COUNT; hall++)
    {
        g_ui_track_state.hall_prev_pressed[hall] = 0U;
    }
    for (uint8_t step = 0U; step < 16U; ++step)
    {
        g_ui_track_state.step_context_owner[step] = 0U;
    }

    encoder_control_dispatcher_init();

    ui_bootstrap_init();
    ui_param_publish_encoder_binding(g_ui_track_state.active_lane,
                                     g_ui_track_state.shift_down);
    ui_core_publish_hall_arbitration_snapshot();
}

void ui_core_service_track_selection_inputs(void)
{
    /* Keep the UI-owned modifier and presentation context current before the
     * Hall arbitration projection is published. */
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
        ui_param_publish_encoder_binding(ui_get_active_lane(),
                                         g_ui_track_state.shift_down);
    }

    ui_core_service_macro_overlay_inputs(shift_down, track_modifier_down);

    ui_core_publish_hall_arbitration_snapshot();
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
        { ui_core_handle_step_context_event, 1U, 1U },
        { ui_core_handle_hall_input_event, 1U, 1U },
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
    ui_param_begin_encoder_edit_group(&encoder_ctx);

    while (ui_event_pop(&ev))
    {
        if (control_domain_project_ui_busy() != 0U)
        {
            encoders_discard_pending();
            continue;
        }
        if (ev.type == UI_EVENT_ENCODER)
        {
            (void)ui_core_handle_encoder_event(&ev, &encoder_ctx);
            if ((ui_page_get_id() == UI_PAGE_TEMPLATE_MOD)
                    || (ui_page_get_id() == UI_PAGE_MIDI_FX)
                    || (ui_page_get_id() == UI_PAGE_AUDIO_FX))
            {
                const ui_page_t *const encoder_page = ui_page_get();
                if ((encoder_page != 0) && (encoder_page->handle_event != 0))
                {
                    encoder_page->handle_event(&ev);
                }
            }
            continue;
        }
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
         * - navigation may change the current page before page dispatch
         * - this same event is then dispatched to the page active after navigation
         * - navigation is intentionally non-consuming in this pipeline
         */
        ui_navigation_handle_event(&ev);

        const ui_page_t *dispatch_page = ui_page_get();
        if ((dispatch_page != 0) && (dispatch_page->handle_event != 0))
        {
            dispatch_page->handle_event(&ev);
        }

next_event:
        ;
    }

    ui_param_end_encoder_edit_group();

    sample_capture_model_service();
    ui_hall_mode_flow_service_pending(HAL_GetTick());

    const ui_page_t *active_page = ui_page_get();
    if ((active_page != 0) && (active_page->tick != 0))
    {
        /* Page-local periodic work only; track context sync is explicit in dedicated sync APIs. */
        active_page->tick();
    }

    ui_core_publish_hall_arbitration_snapshot();
}

uint8_t ui_get_active_track(void)
{
    return g_ui_track_state.active_track;
}

uint8_t ui_get_active_lane(void)
{
    if (entity_topology_is_active(
            (brick_entity_id_t)g_ui_track_state.active_lane) != 0U)
    {
        return g_ui_track_state.active_lane;
    }

    return g_ui_track_state.active_track;
}

bool ui_resolve_filter_target_track(uint8_t *out_track_id)
{
    track_runtime_resolved_track_t resolved;
    if ((out_track_id == 0U)
            || (track_runtime_resolve_track(ui_get_active_lane(), &resolved) == 0U)
            || (resolved.has_filter_target == 0U))
    {
        return false;
    }
    *out_track_id = resolved.filter_track_id;
    return true;
}

track_config_t ui_get_track_config(uint8_t track)
{
    if (track >= BRICK_ENTITY_CAPACITY)
    {
        return ui_core_get_default_track_config();
    }

    return track_state_get_config(track);
}

track_family_t ui_get_track_family(uint8_t track)
{
    return ui_get_track_config(track).family;
}

track_type_t ui_get_track_type(uint8_t track)
{
    return ui_get_track_config(track).type;
}

bool ui_set_track_family(uint8_t track, track_family_t family)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || ((uint8_t)family >= (uint8_t)TRACK_FAMILY_COUNT))
    {
        return false;
    }

    if (entity_topology_is_active(track) == 0U)
    {
        return false;
    }

    const track_config_t config = track_state_get_config(track);

    if (!ui_core_track_family_is_available(track, family))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_edit_context_sync_active_track(0U);
        }
        return false;
    }

    if ((family == TRACK_FAMILY_EXTERNAL)
            && (track_input_ownership_can_claim(
                    track, track_input_ownership_get_external_input(track)) == 0U))
    {
        if (track == g_ui_track_state.active_track)
        {
            uint8_t owner = TRACK_INPUT_OWNER_NONE;
            const uint8_t input = track_input_ownership_get_external_input(track);
            if (track_input_ownership_get_external_owner(input, &owner) != 0U)
            {
                char feedback[16];
                (void)snprintf(feedback, sizeof(feedback), "USED P%u", (unsigned int)(owner + 1U));
                ui_core_set_feedback(feedback);
            }
        }
        return false;
    }

    if (config.family == family)
    {
        if (ui_track_catalog_type_is_available(track, family, config.type, ui_core_get_track_configs()) == false)
        {
            const control_track_intent_t intent = {
                .operation = CONTROL_TRACK_SET_STRUCTURE,
                .track = track,
                .value0 = (uint8_t)family,
                .value1 = (family == TRACK_FAMILY_OFF)
                    ? (uint8_t)TRACK_TYPE_NONE
                    : (uint8_t)ui_track_catalog_first_available_type(
                        family, track, ui_core_get_track_configs())
            };
            if (control_domain_request_track(&intent) == 0U) return false;
        }
        if (track == g_ui_track_state.active_track)
        {
            ui_edit_context_sync_active_track(0U);
        }
        return true;
    }

    if ((family != TRACK_FAMILY_OFF)
            && (ui_track_catalog_type_count_for_family(family,
                                                       track,
                                                       ui_core_get_track_configs()) == 0U))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_edit_context_sync_active_track(0U);
        }
        return false;
    }

    const track_type_t next_type = (family == TRACK_FAMILY_OFF)
        ? TRACK_TYPE_NONE
        : ((ui_track_catalog_type_is_valid_for_family(family, config.type))
            ? config.type
            : ui_track_catalog_first_available_type(
                family, track, ui_core_get_track_configs()));
    const control_track_intent_t intent = {
        .operation = CONTROL_TRACK_SET_STRUCTURE,
        .track = track,
        .value0 = (uint8_t)family,
        .value1 = (uint8_t)next_type
    };
    if (control_domain_request_track(&intent) == 0U)
    {
        if ((track == g_ui_track_state.active_track) && (family == TRACK_FAMILY_EXTERNAL))
        {
            const uint8_t input = track_input_ownership_get_external_input(track);
            uint8_t owner = TRACK_INPUT_OWNER_NONE;
            if (track_input_ownership_get_external_owner(input, &owner) != 0U)
            {
                char feedback[16];
                (void)snprintf(feedback, sizeof(feedback), "USED P%u", (unsigned int)(owner + 1U));
                ui_core_set_feedback(feedback);
            }
        }
        return false;
    }

    return true;
}

bool ui_set_track_type(uint8_t track, track_type_t type)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || ((uint8_t)type >= (uint8_t)TRACK_TYPE_COUNT))
    {
        return false;
    }

    if (entity_topology_is_active(track) == 0U)
    {
        return false;
    }

    const track_config_t config = track_state_get_config(track);
    if (!ui_track_type_is_valid_for_family(config.family, type))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_edit_context_sync_active_track(0U);
        }
        return false;
    }

    if (!ui_track_type_is_available(track, config.family, type))
    {
        if (type == TRACK_TYPE_LOOPER)
        {
            ui_core_set_feedback("LOOPER LIMIT");
        }
        if (track == g_ui_track_state.active_track)
        {
            ui_edit_context_sync_active_track(0U);
        }
        return false;
    }

    if (config.type == type)
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_edit_context_sync_active_track(0U);
        }
        return true;
    }

    const control_track_intent_t intent = {
        .operation = CONTROL_TRACK_SET_TYPE,
        .track = track,
        .value1 = (uint8_t)type
    };
    if (control_domain_request_track(&intent) == 0U) return false;

    return true;
}

uint8_t ui_count_tracks_with_family(track_family_t family)
{
    if ((uint8_t)family >= (uint8_t)TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    return track_state_count_tracks_with_family(family);
}

const char *ui_get_track_family_display_name(track_family_t family)
{
    return ui_track_catalog_family_display_name(family);
}

const char *ui_get_track_family_short_name(track_family_t family)
{
    return ui_track_catalog_family_short_name(family);
}

const char *ui_get_track_type_display_name(track_family_t family, track_type_t type)
{
    return ui_track_catalog_type_display_name(family, type);
}

const char *ui_get_track_type_short_name(track_family_t family, track_type_t type)
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

    const track_config_t config = ui_get_track_config(track);

    if (config.family == TRACK_FAMILY_OFF)
    {
        (void)snprintf(out, out_len, "Off");
        return;
    }

    if (ui_track_family_is_engine(config.family))
    {
        (void)snprintf(out, out_len, "%s", ui_get_track_type_display_name(config.family, config.type));
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

    out_state->active_bank = 0U;
    out_state->active_pattern = 0U;
    out_state->queued_valid = 0U;
    out_state->queued_bank = 0U;
    out_state->queued_pattern = 0U;
    (void)pattern_live_get_active(&out_state->active_bank, &out_state->active_pattern);
    (void)pattern_live_get_queued(&out_state->queued_valid,
                                  &out_state->queued_bank,
                                  &out_state->queued_pattern);
    out_state->substate = ui_core_pattern_get_substate();
    out_state->selected_bank = ui_core_pattern_get_selected_bank();
    out_state->mode = ui_core_pattern_get_mode();
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

uint8_t ui_core_hall_arbitration_snapshot_read(
    ui_hall_arbitration_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0)
    {
        return 0U;
    }

    for (uint8_t attempt = 0U; attempt < 3U; ++attempt)
    {
        const uint32_t before = g_ui_hall_arbitration_seq;
        if ((before & 1U) != 0U)
        {
            continue;
        }

        __DMB();
        const ui_hall_arbitration_snapshot_t snapshot =
            g_ui_hall_arbitration_snapshot;
        __DMB();
        if (before == g_ui_hall_arbitration_seq)
        {
            *out_snapshot = snapshot;
            return 1U;
        }
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    return 0U;
}
