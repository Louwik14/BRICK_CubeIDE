/**
 * @file led_rgb.c
 * @brief Module applicatif led_rgb.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à led_rgb.
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

#include "led_rgb.h"

#include <stdbool.h>
#include "stm32h7xx_hal.h"

#include "App/Hall/hall_engine.h"
#include "Keyboard/keyboard_runtime.h"
#include "Track/entity_topology.h"
#include "Track/track_runtime.h"
#include "Track/track_state.h"
#include "buttons.h"
#include "led_remap.h"
#include "led_anim.h"
#include "led_layer.h"
#include "Storage/project_control.h"
#include "Storage/sample_capture.h"
#include "UI/ui_core.h"
#include "UI/ui_core_mute.h"
#include "UI/ui_hall_mode_projection.h"
#include "UI/ui_navigation.h"
#include "UI/ui_macro_interaction.h"
#include "App/Hall/hall_keymap.h"
#include "Param/param_macro.h"
#include "UI/ui_page_manager.h"
#include "UI/ui_step_led_ownership.h"
#include "UI/ui_track_led_projection.h"
#include "UI/pages/ui_page_patch_assign.h"
#include "Seq/seq_led.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_runtime.h"

#define LED_FIXED_HALF_BRIGHTNESS 128U
#define LED_FIXED_DIM_WHITE       13U
#define LED_FIXED_WHITE_R         LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_WHITE_G         LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_WHITE_B         LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_GREEN_R         0U
#define LED_FIXED_GREEN_G         LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_GREEN_B         0U
#define LED_FIXED_LIGHT_BLUE_R    32U
#define LED_FIXED_LIGHT_BLUE_G    96U
#define LED_FIXED_LIGHT_BLUE_B    LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_DARK_BLUE_R     0U
#define LED_FIXED_DARK_BLUE_G     24U
#define LED_FIXED_DARK_BLUE_B     88U
#define LED_FIXED_VIOLET_R        LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_VIOLET_G        0U
#define LED_FIXED_VIOLET_B        LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_LIGHT_VIOLET_R  LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_LIGHT_VIOLET_G  48U
#define LED_FIXED_LIGHT_VIOLET_B  LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_DARK_VIOLET_R   64U
#define LED_FIXED_DARK_VIOLET_G   0U
#define LED_FIXED_DARK_VIOLET_B   88U
#define LED_FIXED_BLUE_R          0U
#define LED_FIXED_BLUE_G          0U
#define LED_FIXED_BLUE_B          LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_ORANGE_R        LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_ORANGE_G        64U
#define LED_FIXED_ORANGE_B        0U
#define LED_FIXED_RED_R           LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_RED_G           0U
#define LED_FIXED_RED_B           0U
#define LED_FIXED_TEAL_R          0U
#define LED_FIXED_TEAL_G          LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_TEAL_B          96U

static uint8_t led_seq_collect_held_plock_set_mask(uint8_t *out_has_play)
{
    if (out_has_play != NULL) *out_has_play = 0U;
    if (ui_get_hall_mode() != UI_HALL_MODE_SEQ)
    {
        return 0U;
    }

    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    seq_track_id_t held_track = 0U;
    const uint8_t held_count = seq_edit_collect_held_steps(&held_track,
                                                           held_steps,
                                                           (uint8_t)SEQ_STEPS_PER_PAGE,
                                                           0U);
    if (held_count == 0U)
    {
        return 0U;
    }

    uint8_t set_mask = 0U;
    for (uint8_t i = 0U; i < held_count; ++i)
    {
        const seq_step_id_t step = held_steps[i];
        const uint8_t has_play = seq_model_step_has_play_data(held_track, step);
        if ((out_has_play != NULL) && (has_play != 0U)) *out_has_play = 1U;
        const uint8_t plock_count = seq_model_step_plock_count(held_track, step);
        for (uint8_t p = 0U; p < plock_count; ++p)
        {
            seq_plock_entry_t entry;
            if (seq_model_step_plock_get_at(held_track, step, p, &entry) == 0U)
            {
                continue;
            }

            set_mask |= seq_param_iface_set_to_mask(entry.set_id);
        }
    }

    return set_mask;
}

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_rgb_color_t;

/*
 * Omnichord chord-zone colors.
 * Pairing is driven by hall & 0x03 so halls 0/8, 1/9, 2/10 and 3/11
 * share the same group color.
 */
static const led_rgb_color_t g_led_keyboard_omni_chord_colors[4] = {
    { 128U, 32U, 32U },
    { 128U, 80U, 0U },
    { 96U, 96U, 0U },
    { 80U, 0U, 128U },
};

static const led_rgb_color_t g_led_macro_colors[PERSIST_CONTROL_MACRO_COUNT] = {
    { 128U, 48U, 0U },
    { 128U, 88U, 0U },
    { 112U, 128U, 0U },
    { 56U, 128U, 0U },
    { 0U, 128U, 24U },
    { 0U, 128U, 88U },
    { 0U, 112U, 128U },
    { 0U, 56U, 128U },
    { 0U, 0U, 128U },
    { 56U, 0U, 128U },
    { 104U, 0U, 128U },
    { 128U, 0U, 96U },
    { 128U, 0U, 40U },
    { 128U, 24U, 48U }
};


static led_rgb_color_t led_macro_color(uint8_t macro)
{
    if (macro >= PERSIST_CONTROL_MACRO_COUNT)
    {
        return g_led_macro_colors[0U];
    }

    return g_led_macro_colors[macro];
}

static led_rgb_color_t led_scale_color(led_rgb_color_t color, uint8_t scale)
{
    color.r = (uint8_t)(((uint16_t)color.r * (uint16_t)scale) / 255U);
    color.g = (uint8_t)(((uint16_t)color.g * (uint16_t)scale) / 255U);
    color.b = (uint8_t)(((uint16_t)color.b * (uint16_t)scale) / 255U);
    return color;
}

static button_id_t led_param_button_for_led(led_id_t led);

static void led_apply_param_button_scene(led_id_t led,
                                         uint8_t held_plock_sets,
                                         uint8_t held_has_play,
                                         led_id_t active_param_led)
{
    const button_id_t button = led_param_button_for_led(led);
    if (ui_navigation_is_ensemble_button_available(button) == 0U)
    {
        led_layer_set(LED_LAYER_UI, led, 0U, 0U, 0U);
        return;
    }

    uint8_t r = LED_FIXED_GREEN_R;
    uint8_t g = LED_FIXED_GREEN_G;
    uint8_t b = LED_FIXED_GREEN_B;

    if (led == active_param_led)
    {
        r = LED_FIXED_WHITE_R;
        g = LED_FIXED_WHITE_G;
        b = LED_FIXED_WHITE_B;
    }

    if ((held_plock_sets != 0U) || (held_has_play != 0U))
    {
        uint8_t match_set = 0U;
        if ((led == led_remap_param_led_for_button(BTN_PARAM_1))
                && ((held_plock_sets & seq_param_iface_set_to_mask((uint8_t)SEQ_PLOCK_SET_ENV)) != 0U))
        {
            match_set = 1U;
        }
        else if ((led == led_remap_param_led_for_button(BTN_PARAM_2))
                 && ((held_plock_sets & seq_param_iface_set_to_mask((uint8_t)SEQ_PLOCK_SET_TONE)) != 0U))
        {
            match_set = 1U;
        }
        else if ((led == led_remap_param_led_for_button(BTN_PARAM_5))
                 && (held_has_play != 0U))
        {
            match_set = 1U;
        }

        if (match_set != 0U)
        {
            r = LED_FIXED_ORANGE_R;
            g = LED_FIXED_ORANGE_G;
            b = LED_FIXED_ORANGE_B;
        }
    }

    led_layer_set(LED_LAYER_UI, led, r, g, b);
}

static button_id_t led_param_button_for_led(led_id_t led)
{
    for (button_id_t button = BTN_PARAM_1; button <= BTN_TRACK; ++button)
    {
        if (led_remap_param_led_for_button(button) == led)
        {
            return button;
        }
    }

    return BTN_COUNT;
}

static void led_apply_default_hall_scene(uint8_t hall)
{
    const led_id_t led = led_remap_led_for_hall(hall);
    entity_topology_descriptor_t entity = { 0 };

    if ((entity_topology_get((brick_entity_id_t)hall, &entity) == 0U)
            || (entity.active == 0U))
    {
        led_layer_set(LED_LAYER_UI, led, 0U, 0U, 0U);
        return;
    }

    if (entity.role == ENTITY_ROLE_GROUP_CHILD)
    {
        led_layer_set(LED_LAYER_UI,
                      led,
                      LED_FIXED_LIGHT_BLUE_R,
                      LED_FIXED_LIGHT_BLUE_G,
                      LED_FIXED_LIGHT_BLUE_B);
        return;
    }

    if (entity.role == ENTITY_ROLE_GROUP_MASTER)
    {
        led_layer_set(LED_LAYER_UI,
                      led,
                      LED_FIXED_VIOLET_R,
                      LED_FIXED_VIOLET_G,
                      LED_FIXED_VIOLET_B);
        return;
    }

    led_layer_set(LED_LAYER_UI,
                  led,
                  LED_FIXED_DARK_BLUE_R,
                  LED_FIXED_DARK_BLUE_G,
                  LED_FIXED_DARK_BLUE_B);
}

static void led_apply_keyboard_hall_scene(uint8_t hall)
{
    const led_id_t led = led_remap_led_for_hall(hall);

    if (!keyboard_runtime_get_omnichord())
    {
        if (hall < 8U)
        {
            led_layer_set(LED_LAYER_UI, led, LED_FIXED_LIGHT_BLUE_R, LED_FIXED_LIGHT_BLUE_G, LED_FIXED_LIGHT_BLUE_B);
        }
        else
        {
            led_layer_set(LED_LAYER_UI, led, LED_FIXED_DARK_BLUE_R, LED_FIXED_DARK_BLUE_G, LED_FIXED_DARK_BLUE_B);
        }
        return;
    }

    if (((hall >= 4U) && (hall <= 7U)) || (hall >= 12U))
    {
        led_layer_set(LED_LAYER_UI, led, LED_FIXED_BLUE_R, LED_FIXED_BLUE_G, LED_FIXED_BLUE_B);
        return;
    }

    const led_rgb_color_t color = g_led_keyboard_omni_chord_colors[hall & 0x03U];
    led_layer_set(LED_LAYER_UI, led, color.r, color.g, color.b);
}

static void led_apply_audio_rec_hall_scene(uint8_t hall)
{
    const led_id_t led = led_remap_led_for_hall(hall);
    if (hall >= TRACK_ACTIVE_COUNT)
    {
        led_layer_set(LED_LAYER_UI, led, 0U, 0U, 0U);
        return;
    }

    if (sample_capture_model_source_track_is_enabled(hall) == 0U)
    {
        led_layer_set(LED_LAYER_UI, led, LED_FIXED_DIM_WHITE, LED_FIXED_DIM_WHITE, LED_FIXED_DIM_WHITE);
        return;
    }

    led_layer_set(LED_LAYER_UI, led, LED_FIXED_RED_R, 0U, LED_FIXED_RED_B);
}

static void led_apply_pattern_hall_scene(uint8_t hall)
{
    const led_id_t led = led_remap_led_for_hall(hall);
    ui_pattern_stub_state_t state = { 0 };
    ui_get_pattern_stub_state(&state);

    uint8_t r = 0U;
    uint8_t g = 0U;
    uint8_t b = 0U;

    if (state.substate == UI_PATTERN_SUBSTATE_BANK_SELECT)
    {
        r = LED_FIXED_DARK_BLUE_R;
        g = LED_FIXED_DARK_BLUE_G;
        b = LED_FIXED_DARK_BLUE_B;

        if (hall == state.active_bank)
        {
            r = LED_FIXED_RED_R;
            g = LED_FIXED_RED_G;
            b = LED_FIXED_RED_B;
        }
    }
    else
    {
        r = LED_FIXED_LIGHT_BLUE_R;
        g = LED_FIXED_LIGHT_BLUE_G;
        b = LED_FIXED_LIGHT_BLUE_B;

        if ((state.active_bank == state.selected_bank) && (hall == state.active_pattern))
        {
            r = LED_FIXED_RED_R;
            g = LED_FIXED_RED_G;
            b = LED_FIXED_RED_B;
        }
    }

    led_layer_set(LED_LAYER_UI, led, r, g, b);
}

static void led_apply_macro_hall(uint8_t hall)
{
    hall_key_metadata_t key;
    if (hall_keymap_metadata(hall, &key) == 0U
        || key.kind != HALL_KEY_KIND_WHITE || key.white_index == 0U) return;
    const uint8_t macro = (uint8_t)(key.white_index - 1U);
    if (macro >= PERSIST_CONTROL_MACRO_COUNT) return;
    uint8_t scale = project_control_macros_view()->macros[macro].lock_count != 0U ? 140U : 35U;
    if (ui_macro_interaction_get_mode() == UI_MACRO_ASSIGN)
    {
        uint8_t held;
        if (ui_macro_interaction_get_held_macro(&held) != 0U && held == macro)
            scale = 255U;
    }
    else
    {
        const float amount = param_macro_get_amount(macro);
        if (amount > 0.0f) scale = (uint8_t)(140.0f + amount * 115.0f);
    }
    const led_rgb_color_t color = led_scale_color(led_macro_color(macro), scale);
    led_layer_set(LED_LAYER_UI, led_remap_led_for_hall(hall), color.r, color.g, color.b);
}

static void led_apply_track_select_hall_scene(uint8_t hall)
{
    const led_id_t led = led_remap_led_for_hall(hall);
    ui_track_led_projection_t projection = { 0 };
    led_rgb_color_t color = { 0U, 0U, 0U };

    if ((ui_track_led_project_hall(hall, &projection) != 0U)
            && (projection.visible != 0U))
    {
        switch (projection.color)
        {
            case UI_TRACK_LED_COLOR_TOP_LEVEL_OFF:
                color = (led_rgb_color_t){ LED_FIXED_LIGHT_BLUE_R,
                                           LED_FIXED_LIGHT_BLUE_G,
                                           LED_FIXED_LIGHT_BLUE_B };
                break;
            case UI_TRACK_LED_COLOR_TOP_LEVEL_ACTIVE:
                color = (led_rgb_color_t){ LED_FIXED_DARK_BLUE_R,
                                           LED_FIXED_DARK_BLUE_G,
                                           LED_FIXED_DARK_BLUE_B };
                break;
            case UI_TRACK_LED_COLOR_GROUP_CHILD_INACTIVE:
                color = (led_rgb_color_t){ LED_FIXED_LIGHT_VIOLET_R,
                                           LED_FIXED_LIGHT_VIOLET_G,
                                           LED_FIXED_LIGHT_VIOLET_B };
                break;
            case UI_TRACK_LED_COLOR_GROUP_CHILD_ACTIVE:
                color = (led_rgb_color_t){ LED_FIXED_DARK_VIOLET_R,
                                           LED_FIXED_DARK_VIOLET_G,
                                           LED_FIXED_DARK_VIOLET_B };
                break;
            case UI_TRACK_LED_COLOR_FOCUS:
                color = (led_rgb_color_t){ LED_FIXED_WHITE_R,
                                           LED_FIXED_WHITE_G,
                                           LED_FIXED_WHITE_B };
                break;
            default:
                break;
        }
    }

    led_layer_set(LED_LAYER_UI, led, color.r, color.g, color.b);
}

static uint8_t led_apply_mute_hall_scene(uint8_t hall)
{
    ui_mute_hall_led_t mute_led = { 0 };
    if (ui_get_mute_hall_led(hall, &mute_led) == 0U)
    {
        return 0U;
    }

    const led_id_t led = led_remap_led_for_hall(hall);
    if (mute_led.visible == 0U)
    {
        led_layer_set(LED_LAYER_UI, led, 0U, 0U, 0U);
        return 1U;
    }

    uint8_t led_on = 1U;
    if (mute_led.blink != 0U)
    {
        led_on = (((HAL_GetTick() / 200U) & 0x1U) != 0U) ? 1U : 0U;
    }

    if (led_on == 0U)
    {
        led_layer_set(LED_LAYER_UI, led, 0U, 0U, 0U);
    }
    else if (mute_led.muted != 0U)
    {
        entity_topology_descriptor_t entity = { 0 };
        const uint8_t is_group_child =
            (entity_topology_get((brick_entity_id_t)hall, &entity) != 0U)
            && (entity.role == ENTITY_ROLE_GROUP_CHILD);
        led_layer_set(LED_LAYER_UI,
                      led,
                      is_group_child ? LED_FIXED_ORANGE_R : LED_FIXED_RED_R,
                      is_group_child ? LED_FIXED_ORANGE_G : LED_FIXED_RED_G,
                      is_group_child ? LED_FIXED_ORANGE_B : LED_FIXED_RED_B);
    }
    else
    {
        entity_topology_descriptor_t entity = { 0 };
        const uint8_t is_group_child =
            (entity_topology_get((brick_entity_id_t)hall, &entity) != 0U)
            && (entity.role == ENTITY_ROLE_GROUP_CHILD);
        led_layer_set(LED_LAYER_UI,
                      led,
                      is_group_child ? LED_FIXED_TEAL_R : LED_FIXED_GREEN_R,
                      is_group_child ? LED_FIXED_TEAL_G : LED_FIXED_GREEN_G,
                      is_group_child ? LED_FIXED_TEAL_B : LED_FIXED_GREEN_B);
    }

    return 1U;
}

static uint8_t led_apply_patch_assign_hall_scene(uint8_t hall)
{
    uint8_t target_on = 0U;
    if (ui_page_patch_assign_get_target_hall_led(hall, &target_on) == 0U)
    {
        return 0U;
    }

    const led_id_t led = led_remap_led_for_hall(hall);
    if (target_on != 0U)
    {
        led_layer_set(LED_LAYER_UI, led, LED_FIXED_GREEN_R, LED_FIXED_GREEN_G, LED_FIXED_GREEN_B);
    }
    else
    {
        led_layer_set(LED_LAYER_UI, led, 0U, 0U, 0U);
    }

    return 1U;
}

static bool led_hall_mode_uses_keyboard_scene(ui_hall_mode_t mode)
{
    return (mode == UI_HALL_MODE_KEYBOARD);
}

static bool led_hall_mode_uses_seq_scene(ui_hall_mode_t mode)
{
    return (ui_step_led_ownership_hall_mode_needs_step_leds(mode) != 0U);
}

static void led_apply_normal_rec_scene(led_id_t led)
{
    uint8_t blink = 0U;
    const sample_capture_rec_led_state_t audio_rec =
        sample_capture_model_rec_led_state();

    if (audio_rec == SAMPLE_CAPTURE_REC_LED_WAITING)
    {
        blink = (uint8_t)(((HAL_GetTick() / 150U) & 0x1U) != 0U ? 1U : 0U);
        led_layer_set(LED_LAYER_UI, led,
                      blink ? LED_FIXED_VIOLET_R : 0U,
                      blink ? LED_FIXED_VIOLET_G : 0U,
                      blink ? LED_FIXED_VIOLET_B : 0U);
        return;
    }

    if (audio_rec == SAMPLE_CAPTURE_REC_LED_ACTIVE)
    {
        led_layer_set(LED_LAYER_UI, led,
                      LED_FIXED_VIOLET_R,
                      LED_FIXED_VIOLET_G,
                      LED_FIXED_VIOLET_B);
        return;
    }

    if (seq_runtime_rec_is_armed() != 0U)
    {
        blink = (uint8_t)(((HAL_GetTick() / 150U) & 0x1U) != 0U ? 1U : 0U);
        if (seq_runtime_rec_is_pattern_pending_start() != 0U)
        {
            led_layer_set(LED_LAYER_UI, led, blink ? LED_FIXED_RED_R : 0U, 0U, 0U);
            return;
        }

        if (seq_runtime_get_rec_count_in_remaining_steps() > 0U)
        {
            led_layer_set(LED_LAYER_UI, led, blink ? LED_FIXED_RED_R : 0U, 0U, 0U);
            return;
        }

        led_layer_set(LED_LAYER_UI, led, LED_FIXED_RED_R, LED_FIXED_RED_G, LED_FIXED_RED_B);
        return;
    }

    led_layer_set(LED_LAYER_UI, led, 0U, 0U, 0U);
}

static void led_apply_fixed_scene(void)
{
    led_layer_clear_all();

    uint8_t held_has_play = 0U;
    const uint8_t held_plock_sets = led_seq_collect_held_plock_set_mask(&held_has_play);
    const ui_hall_mode_t hall_mode = ui_get_hall_mode();
    const uint8_t active_page_id = ui_page_get_id();
    const uint8_t track_overlay_active =
        ui_hall_mode_track_overlay_active(
            button_down(BTN_SHIFT),
            ui_is_track_modifier_held(),
            ui_core_mute_is_active());
    const button_id_t active_button = ui_navigation_get_button_for_page(active_page_id);
    const led_id_t active_param_led = led_remap_param_led_for_button(active_button);
    if (track_overlay_active != 0U)
    {
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
        {
            led_apply_track_select_hall_scene(hall);
        }
    }
    else if (hall_mode == UI_HALL_MODE_MACRO)
    {
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
            led_apply_macro_hall(hall);
    }
    else if (ui_page_patch_assign_is_open() != 0U)
    {
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
        {
            (void)led_apply_patch_assign_hall_scene(hall);
        }
    }
    else if (hall_mode == UI_HALL_MODE_MUTE)
    {
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
        {
            (void)led_apply_mute_hall_scene(hall);
        }
    }
    else if (ui_step_led_ownership_page_needs_step_leds(active_page_id) != 0U)
    {
        seq_led_render_active_track_page(ui_get_active_lane());
    }
    else if (led_hall_mode_uses_seq_scene(hall_mode))
    {
        seq_led_render_active_track_page(ui_get_active_lane());
    }
    else
    {
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
        {
            if (led_apply_mute_hall_scene(hall) != 0U)
            {
                continue;
            }
            if (hall_mode == UI_HALL_MODE_PATTERN)
            {
                led_apply_pattern_hall_scene(hall);
            }
            else if (hall_mode == UI_HALL_MODE_AUDIO_REC)
            {
                led_apply_audio_rec_hall_scene(hall);
            }
            else if (led_hall_mode_uses_keyboard_scene(hall_mode))
            {
                led_apply_keyboard_hall_scene(hall);
            }
            else
            {
                led_apply_default_hall_scene(hall);
            }
        }
    }

    for (uint32_t led = 0U; led < LED_FB_COUNT; led++)
    {
        if (led_remap_is_hall_led((led_id_t)led))
        {
            continue;
        }
        else if (led_remap_is_seq_led((led_id_t)led))
        {
            continue;
        }
        else if (led_remap_is_param_led((led_id_t)led))
        {
            led_apply_param_button_scene((led_id_t)led,
                                         held_plock_sets,
                                         held_has_play,
                                         active_param_led);
        }
        else
        {
            if ((led_id_t)led == LED_REC)
            {
                led_apply_normal_rec_scene((led_id_t)led);
            }
            else
            {
                led_layer_set(LED_LAYER_UI,
                              (led_id_t)led,
                              LED_FIXED_WHITE_R,
                              LED_FIXED_WHITE_G,
                              LED_FIXED_WHITE_B);
            }
        }
    }
}

/**
 * @brief Point d'entrée led_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_init(void)
{
    led_fb_init();
    led_layer_init();
    led_anim_init();
    led_apply_fixed_scene();
    led_layer_commit();
}

/**
 * @brief Point d'entrée led_set.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_set.
 *
 * @param led Paramètre d'entrée de l'API.
 * @param r Paramètre d'entrée de l'API.
 * @param g Paramètre d'entrée de l'API.
 * @param b Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_set(led_id_t led, uint8_t r, uint8_t g, uint8_t b)
{
    led_fb_set(led, r, g, b);
}

/**
 * @brief Point d'entrée led_fill.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_fill.
 *
 * @param r Paramètre d'entrée de l'API.
 * @param g Paramètre d'entrée de l'API.
 * @param b Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_fill(uint8_t r, uint8_t g, uint8_t b)
{
    led_fb_fill(r, g, b);
}

/**
 * @brief Point d'entrée led_clear.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_clear.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_clear(void)
{
    led_fb_clear();
}

/**
 * @brief Point d'entrée led_show.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_show.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_show(void)
{
    led_layer_commit();
}

/**
 * @brief Point d'entrée led_service.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_service.
 *
 * @param dt_ms Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_service(uint32_t dt_ms)
{
    (void)dt_ms;

    led_anim_stop_all();
    led_apply_fixed_scene();
    led_layer_commit();
}
