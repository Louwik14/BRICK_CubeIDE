#include "ui_core_mute.h"

#include <string.h>

#include "buttons.h"
#include "App/Hall/hall_engine.h"
#include "Core/track_mute.h"
#include "Core/track_runtime.h"
#include "ui_page_manager.h"
#include "ui_step_led_ownership.h"
#include "Core/entity_topology.h"

#define UI_TRACK_MOD_BUTTON BTN_TRACK

typedef struct
{
    uint8_t active;
    ui_mute_submode_t submode;
    ui_hall_mode_t prev_mode;
    uint8_t prev_mode_valid;
    uint8_t hold_quick_prepare_armed;
    uint8_t initial_state[SEQ_LANE_CAPACITY];
    uint8_t prepared_state[SEQ_LANE_CAPACITY];
} ui_core_mute_state_t;

static ui_core_mute_state_t g_ui_core_mute = {
    .active = 0U,
    .submode = UI_MUTE_SUBMODE_NONE,
    .prev_mode = UI_HALL_MODE_SEQ,
    .prev_mode_valid = 0U,
    .hold_quick_prepare_armed = 0U,
    .initial_state = { 0U },
    .prepared_state = { 0U }
};

static uint8_t ui_core_get_track_runtime_mute(uint8_t track, uint8_t *out_muted, uint8_t *out_available)
{
    if ((out_muted == 0) || (out_available == 0) || (track >= SEQ_LANE_CAPACITY))
    {
        return 0U;
    }

    *out_muted = 0U;
    *out_available = 0U;

    *out_available = track_mute_is_available(track);
    *out_muted = (*out_available != 0U) ? track_mute_get(track) : 0U;
    return 1U;
}

static uint8_t ui_core_apply_track_runtime_mute(uint8_t track, uint8_t muted)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return 0U;
    }

    return track_mute_set(track, muted);
}

static void ui_core_mute_capture_current_to_buffer(uint8_t *dst)
{
    if (dst == 0)
    {
        return;
    }

    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        uint8_t muted = 0U;
        uint8_t available = 0U;
        (void)ui_core_get_track_runtime_mute(track, &muted, &available);
        dst[track] = (available != 0U) ? muted : 0U;
    }
}

static void ui_core_mute_cancel_prepared_to_current_state(void)
{
    ui_core_mute_capture_current_to_buffer(g_ui_core_mute.initial_state);
    memcpy(g_ui_core_mute.prepared_state, g_ui_core_mute.initial_state, sizeof(g_ui_core_mute.prepared_state));
    g_ui_core_mute.hold_quick_prepare_armed = 0U;
}

static void ui_core_mute_exit_to_previous_mode(ui_core_mute_set_hall_mode_fn set_hall_mode)
{
    if (set_hall_mode == 0)
    {
        return;
    }

    const ui_hall_mode_t target = (g_ui_core_mute.prev_mode_valid != 0U)
            ? g_ui_core_mute.prev_mode
            : UI_HALL_MODE_SEQ;
    set_hall_mode(target);
}

static void ui_core_mute_enter_quick(ui_core_mute_get_hall_mode_fn get_hall_mode,
                                     ui_core_mute_set_hall_mode_fn set_hall_mode)
{
    if ((get_hall_mode == 0) || (set_hall_mode == 0))
    {
        return;
    }

    if (g_ui_core_mute.active == 0U)
    {
        const ui_hall_mode_t current_mode = get_hall_mode();
        g_ui_core_mute.prev_mode = (current_mode == UI_HALL_MODE_MUTE) ? UI_HALL_MODE_SEQ : current_mode;
        g_ui_core_mute.prev_mode_valid = 1U;
    }

    g_ui_core_mute.active = 1U;
    g_ui_core_mute.submode = UI_MUTE_SUBMODE_QUICK;
    g_ui_core_mute.hold_quick_prepare_armed = 0U;
    set_hall_mode(UI_HALL_MODE_MUTE);
}

static void ui_core_mute_enter_hold_quick(ui_core_mute_set_hall_mode_fn set_hall_mode)
{
    if ((set_hall_mode == 0) || (g_ui_core_mute.active == 0U))
    {
        return;
    }

    g_ui_core_mute.active = 1U;
    g_ui_core_mute.submode = UI_MUTE_SUBMODE_HOLD_QUICK;
    g_ui_core_mute.hold_quick_prepare_armed = 0U;
    set_hall_mode(UI_HALL_MODE_MUTE);
}

static void ui_core_mute_enter_prepare(ui_core_mute_set_hall_mode_fn set_hall_mode)
{
    if ((set_hall_mode == 0) || (g_ui_core_mute.active == 0U))
    {
        return;
    }

    ui_core_mute_capture_current_to_buffer(g_ui_core_mute.initial_state);
    memcpy(g_ui_core_mute.prepared_state, g_ui_core_mute.initial_state, sizeof(g_ui_core_mute.prepared_state));
    g_ui_core_mute.submode = UI_MUTE_SUBMODE_PREPARE;
    g_ui_core_mute.hold_quick_prepare_armed = 0U;
    set_hall_mode(UI_HALL_MODE_MUTE);
    ui_core_mute_cancel_prepared_if_step_led_owner(ui_page_get_id());
}

static void ui_core_mute_apply_prepared_and_exit(ui_core_mute_set_hall_mode_fn set_hall_mode)
{
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        if ((entity_topology_is_active((brick_entity_id_t)track) == 0U)
                || (track_runtime_has_capability(track, TRACK_CAPABILITY_MUTE) == 0U))
        {
            continue;
        }
        (void)ui_core_apply_track_runtime_mute(track, g_ui_core_mute.prepared_state[track]);
    }

    ui_core_mute_exit_to_previous_mode(set_hall_mode);
}

static void ui_core_mute_toggle_quick_track(uint8_t track)
{
    uint8_t muted = 0U;
    uint8_t available = 0U;
    if ((ui_core_get_track_runtime_mute(track, &muted, &available) == 0U) || (available == 0U))
    {
        return;
    }

    (void)ui_core_apply_track_runtime_mute(track, (muted == 0U) ? 1U : 0U);
}

static void ui_core_mute_toggle_prepared_track(uint8_t track)
{
    if ((entity_topology_is_active((brick_entity_id_t)track) == 0U)
            || (track_runtime_has_capability(track, TRACK_CAPABILITY_MUTE) == 0U))
    {
        return;
    }

    g_ui_core_mute.prepared_state[track] ^= 1U;
}

void ui_core_mute_init(void)
{
    g_ui_core_mute.active = 0U;
    g_ui_core_mute.submode = UI_MUTE_SUBMODE_NONE;
    g_ui_core_mute.prev_mode = UI_HALL_MODE_SEQ;
    g_ui_core_mute.prev_mode_valid = 0U;
    g_ui_core_mute.hold_quick_prepare_armed = 0U;
    memset(g_ui_core_mute.initial_state, 0, sizeof(g_ui_core_mute.initial_state));
    memset(g_ui_core_mute.prepared_state, 0, sizeof(g_ui_core_mute.prepared_state));
}

void ui_core_mute_reset(void)
{
    ui_core_mute_init();
}

void ui_core_mute_cancel_prepared_if_step_led_owner(uint8_t page_id)
{
    if ((g_ui_core_mute.active == 0U)
        || (g_ui_core_mute.submode != UI_MUTE_SUBMODE_PREPARE)
        || (ui_step_led_ownership_page_needs_step_leds(page_id) == 0U))
    {
        return;
    }

    ui_core_mute_cancel_prepared_to_current_state();
}

uint8_t ui_core_mute_is_active(void)
{
    return g_ui_core_mute.active;
}

ui_mute_submode_t ui_core_mute_get_submode(void)
{
    return g_ui_core_mute.submode;
}

ui_hall_mode_t ui_core_mute_get_passthrough_hall_mode(void)
{
    if ((g_ui_core_mute.active == 0U)
        || (g_ui_core_mute.prev_mode_valid == 0U)
        || (g_ui_core_mute.prev_mode == UI_HALL_MODE_MUTE))
    {
        return UI_HALL_MODE_SEQ;
    }

    return g_ui_core_mute.prev_mode;
}

ui_mute_state_t ui_core_mute_get_state(void)
{
    ui_mute_state_t state = {
        .active = g_ui_core_mute.active,
        .submode = g_ui_core_mute.submode
    };
    return state;
}

uint8_t ui_core_mute_get_hall_led(uint8_t hall, ui_mute_hall_led_t *out_led)
{
    if ((out_led == 0) || (hall >= HALL_UI_LANE_COUNT))
    {
        return 0U;
    }

    out_led->visible = 0U;
    out_led->blink = 0U;
    out_led->muted = 0U;

    if (g_ui_core_mute.active == 0U)
    {
        return 0U;
    }

    if (hall >= SEQ_LANE_CAPACITY)
    {
        return 1U;
    }

    if (entity_topology_is_active((brick_entity_id_t)hall) == 0U)
    {
        return 1U;
    }

    if (g_ui_core_mute.submode == UI_MUTE_SUBMODE_PREPARE)
    {
        out_led->muted = g_ui_core_mute.prepared_state[hall];
    }
    else
    {
        uint8_t muted = 0U;
        uint8_t runtime_available = 0U;
        (void)ui_core_get_track_runtime_mute(hall, &muted, &runtime_available);
        if (runtime_available != 0U)
        {
            out_led->muted = muted;
        }
    }
    out_led->visible = 1U;
    if ((g_ui_core_mute.submode == UI_MUTE_SUBMODE_PREPARE)
        && (g_ui_core_mute.prepared_state[hall] != g_ui_core_mute.initial_state[hall]))
    {
        out_led->blink = 1U;
    }

    return 1U;
}

uint8_t ui_core_mute_handle_event(const ui_event_t *ev,
                                  uint8_t *io_shift_down,
                                  uint8_t track_select_armed,
                                  ui_core_mute_get_hall_mode_fn get_hall_mode,
                                  ui_core_mute_set_hall_mode_fn set_hall_mode,
                                  ui_core_mute_suppress_hall_note_fn suppress_hall_note)
{
    if ((ev == 0) || (io_shift_down == 0) || (get_hall_mode == 0) || (set_hall_mode == 0))
    {
        return 0U;
    }

    if ((g_ui_core_mute.active == 0U)
        && (ev->type == UI_EVENT_BUTTON_PRESS)
        && (ev->id == (uint8_t)BTN_TRANSPOSE_UP)
        && (*io_shift_down != 0U)
        && (track_select_armed == 0U))
    {
        ui_core_mute_enter_quick(get_hall_mode, set_hall_mode);
        return 1U;
    }

    if (g_ui_core_mute.active == 0U)
    {
        return 0U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) || (ev->type == UI_EVENT_BUTTON_RELEASE))
    {
        if (ev->id == (uint8_t)BTN_SHIFT)
        {
            *io_shift_down = (ev->type == UI_EVENT_BUTTON_PRESS) ? 1U : 0U;

            if ((ev->type == UI_EVENT_BUTTON_PRESS)
                && (g_ui_core_mute.submode == UI_MUTE_SUBMODE_QUICK)
                && (button_down(BTN_TRANSPOSE_UP) != 0U))
            {
                ui_core_mute_enter_hold_quick(set_hall_mode);
            }
            else if ((ev->type == UI_EVENT_BUTTON_RELEASE)
                     && (g_ui_core_mute.submode == UI_MUTE_SUBMODE_HOLD_QUICK))
            {
                g_ui_core_mute.hold_quick_prepare_armed = 1U;
            }
            else if ((ev->type == UI_EVENT_BUTTON_PRESS)
                     && (g_ui_core_mute.submode == UI_MUTE_SUBMODE_HOLD_QUICK)
                     && (g_ui_core_mute.hold_quick_prepare_armed != 0U))
            {
                if (button_down(BTN_TRANSPOSE_UP) != 0U)
                {
                    ui_core_mute_enter_prepare(set_hall_mode);
                }
            }

            return 1U;
        }

        if (ev->id == (uint8_t)BTN_TRANSPOSE_UP)
        {
            if ((ev->type == UI_EVENT_BUTTON_RELEASE)
                && (g_ui_core_mute.submode == UI_MUTE_SUBMODE_QUICK))
            {
                ui_core_mute_exit_to_previous_mode(set_hall_mode);
            }
            else if ((ev->type == UI_EVENT_BUTTON_PRESS)
                     && (g_ui_core_mute.submode == UI_MUTE_SUBMODE_HOLD_QUICK)
                     && (*io_shift_down == 0U))
            {
                ui_core_mute_exit_to_previous_mode(set_hall_mode);
            }
            else if ((ev->type == UI_EVENT_BUTTON_PRESS)
                     && (g_ui_core_mute.submode == UI_MUTE_SUBMODE_PREPARE))
            {
                ui_core_mute_apply_prepared_and_exit(set_hall_mode);
            }

            return 1U;
        }

        if ((ev->id == (uint8_t)BTN_TRANSPOSE_DOWN) || (ev->id == (uint8_t)UI_TRACK_MOD_BUTTON))
        {
            return 1U;
        }

        /*
         * Do not globally monopolize button events while mute is active:
         * non-mute controls (transport, param navigation, SHIFT+HALL path) must
         * continue through the central ui_core pipeline.
         */
        return 0U;
    }

    if ((ev->type == UI_EVENT_HALL_PRESS) && (ev->id < SEQ_LANE_CAPACITY))
    {
        if (suppress_hall_note != 0)
        {
            suppress_hall_note(ev->id);
        }

        if ((g_ui_core_mute.submode == UI_MUTE_SUBMODE_QUICK)
            || (g_ui_core_mute.submode == UI_MUTE_SUBMODE_HOLD_QUICK))
        {
            ui_core_mute_toggle_quick_track(ev->id);
        }
        else if (g_ui_core_mute.submode == UI_MUTE_SUBMODE_PREPARE)
        {
            ui_core_mute_toggle_prepared_track(ev->id);
        }
        return 1U;
    }

    if ((ev->type == UI_EVENT_HALL_RELEASE) && (ev->id < SEQ_LANE_CAPACITY))
    {
        return 1U;
    }

    return 0U;
}
