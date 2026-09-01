#include "pages/ui_page_template_keyboard.h"

#include <stdio.h>

#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_engine.h"
#include "Keyboard/keyboard_runtime.h"
#include "pages/ui_page_calibration.h"
#include "stm32h7xx_hal.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_keyboard_family = {
    .family_title = "KEYBOARD",
#if defined(BRICK6_VARIANT_LOWCOST)
    .nav_labels = { "PLAY", "MODE", "VEL", "-" },
#else
    .nav_labels = { "PLAY", "MODE", "-", "-" },
#endif
    .subpages = {
        {
            .title = "PLAY",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "MODE",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
#if defined(BRICK6_VARIANT_LOWCOST)
            .title = "VELOCITY",
#else
            .title = "-",
#endif
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_keyboard_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_KEYBOARD);
}

static uint8_t ui_page_template_keyboard_virtual_slot_text(uint8_t slot,
                                                            char *out_name,
                                                            uint32_t out_name_len,
                                                            char *out_value,
                                                            uint32_t out_value_len);

static ui_template_page_state_t g_ui_template_keyboard_state = {
    .family = 0,
    .family_resolver = ui_page_template_keyboard_resolve_family,
    .virtual_slot_text = ui_page_template_keyboard_virtual_slot_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

#if defined(BRICK6_VARIANT_LOWCOST)
static uint8_t g_ui_keyboard_velocity_settings_dirty;
static uint32_t g_ui_keyboard_velocity_settings_dirty_since;

static void ui_page_template_keyboard_mark_settings_dirty(void)
{
    g_ui_keyboard_velocity_settings_dirty = 1U;
    g_ui_keyboard_velocity_settings_dirty_since = HAL_GetTick();
}
#endif

static uint8_t ui_page_template_keyboard_virtual_slot_text(uint8_t slot,
                                                            char *out_name,
                                                            uint32_t out_name_len,
                                                            char *out_value,
                                                            uint32_t out_value_len)
{
    static const char *const roots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char *const scales[] = { "MAJOR", "MINOR", "DORIAN", "MIXOLYD", "PENTA", "BLUES", "CHROM" };
    if (g_ui_template_keyboard_state.active_subpage == 0U)
    {
        if (slot == 0U) { (void)snprintf(out_name,out_name_len,"ROOT"); (void)snprintf(out_value,out_value_len,"%s",roots[keyboard_runtime_get_root_index()%12U]); return 1U; }
        if (slot == 1U) { uint8_t v=keyboard_runtime_get_scale_index(); (void)snprintf(out_name,out_name_len,"SCALE"); (void)snprintf(out_value,out_value_len,"%s",scales[(v<7U)?v:0U]); return 1U; }
        if (slot == 2U) { (void)snprintf(out_name,out_name_len,"OMNI"); (void)snprintf(out_value,out_value_len,"%s",keyboard_runtime_get_omnichord()?"ON":"OFF"); return 1U; }
        return 0U;
    }
    if (g_ui_template_keyboard_state.active_subpage == 1U)
    {
        if (slot == 0U) { (void)snprintf(out_name,out_name_len,"ORDER"); (void)snprintf(out_value,out_value_len,"%s",keyboard_runtime_get_note_order()==NOTE_ORDER_FIFTHS?"FIFTHS":"NATURAL"); return 1U; }
        if (slot == 1U) { (void)snprintf(out_name,out_name_len,"CHORD OVR"); (void)snprintf(out_value,out_value_len,"%s",keyboard_runtime_get_chord_override()?"ON":"OFF"); return 1U; }
        if (slot == 2U) { (void)snprintf(out_name,out_name_len,"MONO LAST"); (void)snprintf(out_value,out_value_len,"%s",keyboard_runtime_get_mono_last()?"ON":"OFF"); return 1U; }
        return 0U;
    }
#if defined(BRICK6_VARIANT_LOWCOST)
    static const char *const profile_labels[HALL_VEL_PROFILE_COUNT] = { "DEFAULT", "USER" };
    static const char *const mode_labels[HALL_VEL_MODE_USER] = { "DV", "TIME", "ENERGY" };
    static const char *const curve_labels[HALL_VEL_CURVE_COUNT] = {
        "LINEAR", "SOFT", "HARD", "LOG", "EXP"
    };
    const uint8_t profile = hall_get_velocity_profile();
    const uint8_t mode = hall_get_velocity_mode();
    const uint8_t curve = hall_get_velocity_curve();

    if (g_ui_template_keyboard_state.active_subpage != 2U)
    {
        return 0U;
    }

    switch (slot)
    {
        case 0U:
            (void)snprintf(out_name, out_name_len, "PROFILE");
            (void)snprintf(out_value, out_value_len, "%s",
                           profile_labels[(profile < HALL_VEL_PROFILE_COUNT) ? profile : 0U]);
            return 1U;
        case 1U:
            (void)snprintf(out_name, out_name_len, "MODE");
            (void)snprintf(out_value, out_value_len, "%s",
                           mode_labels[(mode < HALL_VEL_MODE_USER) ? mode : 0U]);
            return 1U;
        case 2U:
            (void)snprintf(out_name, out_name_len, "CURVE");
            (void)snprintf(out_value, out_value_len, "%s",
                           curve_labels[(curve < HALL_VEL_CURVE_COUNT) ? curve : 0U]);
            return 1U;
        case 3U:
            (void)snprintf(out_name, out_name_len, "USER CAL");
            (void)snprintf(out_value, out_value_len, "%s",
                           (hall_engine_user_velocity_profile_is_valid() != 0U) ? "READY" : "NO CAL");
            return 1U;
        default:
            return 0U;
    }
#else
    return 0U;
#endif
}

#if defined(BRICK6_VARIANT_LOWCOST)
static void ui_page_template_keyboard_leave(void)
{
    if (g_ui_keyboard_velocity_settings_dirty != 0U)
    {
        hall_calibration_save();
        g_ui_keyboard_velocity_settings_dirty = 0U;
    }
    ui_template_page_leave();
}

static void ui_page_template_keyboard_tick(void)
{
    ui_template_page_tick();
    if ((g_ui_keyboard_velocity_settings_dirty != 0U)
        && ((HAL_GetTick() - g_ui_keyboard_velocity_settings_dirty_since) >= 500U))
    {
        hall_calibration_save();
        g_ui_keyboard_velocity_settings_dirty = 0U;
    }
}
#endif

static void ui_page_template_keyboard_handle_event(const ui_event_t *ev)
{
    ui_template_page_handle_event(ev);
}

void ui_page_template_keyboard_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)TRACK_FAMILY_COUNT; ++family)
    {
        const track_family_t track_family = (track_family_t)family;
        for (uint8_t type = 0U; type < (uint8_t)TRACK_TYPE_COUNT; ++type)
        {
            const track_type_t track_type = (track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_KEYBOARD,
                                        track_family,
                                        track_type,
                                        &g_ui_template_keyboard_family);
        }
    }
}

uint8_t ui_page_template_keyboard_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((ui_page_get_id() != UI_PAGE_TEMPLATE_KEYBOARD) || (encoder >= 4U) || (delta == 0)) return 0U;
    if (g_ui_template_keyboard_state.active_subpage == 0U)
    {
        if (encoder == 0U) { int32_t v=(int32_t)keyboard_runtime_get_root_index()+((delta>0)?1:-1); if(v<0)v=0;if(v>11)v=11;keyboard_runtime_set_root((uint8_t)v); }
        else if (encoder == 1U) { int32_t v=(int32_t)keyboard_runtime_get_scale_index()+((delta>0)?1:-1);if(v<0)v=0;if(v>6)v=6;keyboard_runtime_set_scale((uint8_t)v); }
        else if (encoder == 2U) keyboard_runtime_set_omnichord(delta>0);
        return 1U;
    }
    if (g_ui_template_keyboard_state.active_subpage == 1U)
    {
        if (encoder == 0U) keyboard_runtime_set_note_order((delta>0)?NOTE_ORDER_FIFTHS:NOTE_ORDER_NATURAL);
        else if (encoder == 1U) keyboard_runtime_set_chord_override(delta>0);
        else if (encoder == 2U) keyboard_runtime_set_mono_last(delta>0);
        return 1U;
    }
#if defined(BRICK6_VARIANT_LOWCOST)
    if (g_ui_template_keyboard_state.active_subpage != 2U)
    {
        return 0U;
    }

    if (encoder == 0U)
    {
        const uint8_t next = (delta > 0)
            ? (uint8_t)HALL_VEL_PROFILE_USER
            : (uint8_t)HALL_VEL_PROFILE_DEFAULT;
        if (next != hall_get_velocity_profile())
        {
            hall_set_velocity_profile(next);
            ui_page_template_keyboard_mark_settings_dirty();
        }
    }
    else if (encoder == 1U)
    {
        int32_t next = (int32_t)hall_get_velocity_mode() + ((delta > 0) ? 1 : -1);
        if (next < (int32_t)HALL_VEL_MODE_DV_PEAK)
        {
            next = (int32_t)HALL_VEL_MODE_DV_PEAK;
        }
        else if (next > (int32_t)HALL_VEL_MODE_ENERGY)
        {
            next = (int32_t)HALL_VEL_MODE_ENERGY;
        }
        if ((uint8_t)next != hall_get_velocity_mode())
        {
            hall_set_velocity_mode((uint8_t)next);
            ui_page_template_keyboard_mark_settings_dirty();
        }
    }
    else if (encoder == 2U)
    {
        int32_t next = (int32_t)hall_get_velocity_curve() + ((delta > 0) ? 1 : -1);
        if (next < (int32_t)HALL_VEL_CURVE_LINEAR)
        {
            next = (int32_t)HALL_VEL_CURVE_LINEAR;
        }
        else if (next >= (int32_t)HALL_VEL_CURVE_COUNT)
        {
            next = (int32_t)HALL_VEL_CURVE_COUNT - 1;
        }
        if ((uint8_t)next != hall_get_velocity_curve())
        {
            hall_set_velocity_curve((uint8_t)next);
            ui_page_template_keyboard_mark_settings_dirty();
        }
    }
    else
    {
        ui_page_user_calibration_open(UI_PAGE_TEMPLATE_KEYBOARD);
    }

    return 1U;
#else
    (void)encoder;
    (void)delta;
    return 0U;
#endif
}

const ui_page_t g_ui_page_template_keyboard = {
    .enter = ui_template_page_enter,
#if defined(BRICK6_VARIANT_LOWCOST)
    .leave = ui_page_template_keyboard_leave,
#else
    .leave = ui_template_page_leave,
#endif
    .handle_event = ui_page_template_keyboard_handle_event,
#if defined(BRICK6_VARIANT_LOWCOST)
    .tick = ui_page_template_keyboard_tick,
#else
    .tick = ui_template_page_tick,
#endif
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_keyboard_state,
};
