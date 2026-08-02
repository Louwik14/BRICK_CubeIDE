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
#include "Core/track_topology.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "buttons.h"
#include "led_remap.h"
#include "led_anim.h"
#include "led_layer.h"
#include "Storage/project_v1.h"
#include "Storage/sample_capture.h"
#include "UI/ui_core.h"
#include "UI/ui_core_runtime_bridge.h"
#include "UI/ui_hall_mode_projection.h"
#include "UI/ui_navigation.h"
#include "UI/ui_macro_interaction.h"
#include "UI/ui_page_manager.h"
#include "UI/ui_step_led_ownership.h"
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
#define LED_FIXED_BLUE_R          0U
#define LED_FIXED_BLUE_G          0U
#define LED_FIXED_BLUE_B          LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_ORANGE_R        LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_ORANGE_G        64U
#define LED_FIXED_ORANGE_B        0U
#define LED_FIXED_RED_R           LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_RED_G           0U
#define LED_FIXED_RED_B           0U
#define LED_MACRO_PRESSURE_RAW_NOISE_FLOOR 400U
#define LED_MACRO_PRESSURE_LED_MARGIN 75U

static uint8_t led_seq_collect_held_plock_set_mask(void)
{
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

static const led_rgb_color_t g_led_macro_scene_colors[PROJECT_V1_MACRO_SCENE_COUNT] = {
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
    { 128U, 24U, 48U },
    { 96U, 48U, 128U },
    { 48U, 96U, 128U }
};

static uint8_t g_led_macro_pressure_scale[PROJECT_V1_MACRO_SCENE_COUNT];

static led_rgb_color_t led_macro_scene_color(uint8_t scene)
{
    if (scene >= PROJECT_V1_MACRO_SCENE_COUNT)
    {
        return g_led_macro_scene_colors[0U];
    }

    return g_led_macro_scene_colors[scene];
}

static led_rgb_color_t led_scale_color(led_rgb_color_t color, uint8_t scale)
{
    color.r = (uint8_t)(((uint16_t)color.r * (uint16_t)scale) / 255U);
    color.g = (uint8_t)(((uint16_t)color.g * (uint16_t)scale) / 255U);
    color.b = (uint8_t)(((uint16_t)color.b * (uint16_t)scale) / 255U);
    return color;
}

static uint8_t led_macro_pressure_depth_scale(uint8_t hall, uint8_t base_scale)
{
    uint16_t min_value = 0U;
    uint16_t max_value = 0U;
    uint16_t raw_value = 0U;
    uint16_t range = 0U;
    uint16_t delta = 0U;
    uint16_t amount_start = 0U;
    uint8_t amount_u8 = 0U;
    uint8_t target_scale = base_scale;
    uint8_t current_scale = 0U;

    if (hall >= PROJECT_V1_MACRO_SCENE_COUNT)
    {
        return base_scale;
    }

    min_value = hall_engine_get_min(hall);
    max_value = hall_engine_get_max(hall);
    raw_value = hall_engine_get_raw(hall);
    if (max_value > min_value)
    {
        range = (uint16_t)(max_value - min_value);
        delta = (raw_value > min_value) ? (uint16_t)(raw_value - min_value) : 0U;
        amount_start = (uint16_t)(LED_MACRO_PRESSURE_RAW_NOISE_FLOOR
                                  + LED_MACRO_PRESSURE_LED_MARGIN);
        if (range > amount_start)
        {
            if (delta > amount_start)
            {
                uint32_t amount =
                    (((uint32_t)(delta - amount_start) * 255UL)
                     / (uint32_t)(range - amount_start));
                if (amount > 255UL)
                {
                    amount = 255UL;
                }
                amount_u8 = (uint8_t)amount;
            }
        }

        target_scale = (uint8_t)((uint16_t)base_scale
                                 + ((((uint16_t)255U - (uint16_t)base_scale)
                                     * (uint16_t)amount_u8) / 255U));
    }

    current_scale = g_led_macro_pressure_scale[hall];
    if (current_scale == 0U)
    {
        current_scale = base_scale;
    }

    if (target_scale > current_scale)
    {
        current_scale = (uint8_t)(current_scale + (((uint16_t)(target_scale - current_scale) + 1U) / 2U));
    }
    else if (target_scale < current_scale)
    {
        uint8_t step = (uint8_t)(((uint16_t)(current_scale - target_scale) + 3U) / 4U);
        if (step == 0U)
        {
            step = 1U;
        }
        current_scale = (step >= (uint8_t)(current_scale - target_scale))
            ? target_scale
            : (uint8_t)(current_scale - step);
    }

    if (current_scale < base_scale)
    {
        current_scale = base_scale;
    }

    g_led_macro_pressure_scale[hall] = current_scale;
    return current_scale;
}

static button_id_t led_param_button_for_led(led_id_t led);

static void led_apply_param_button_scene(led_id_t led,
                                         uint8_t held_plock_sets,
                                         button_id_t macro_button,
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

    if (held_plock_sets != 0U)
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
                 && ((held_plock_sets & seq_param_iface_set_to_mask((uint8_t)SEQ_PLOCK_SET_PLAY)) != 0U))
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

    if ((macro_button != BTN_COUNT) && (led == led_remap_param_led_for_button(macro_button)))
    {
        r = LED_FIXED_ORANGE_R;
        g = LED_FIXED_ORANGE_G;
        b = LED_FIXED_ORANGE_B;
    }

    led_layer_set(LED_LAYER_UI, led, r, g, b);
}

static button_id_t led_macro_param_to_button(param_id_t param)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);

    switch (rule.domain)
    {
        case TRACK_RUNTIME_PARAM_DOMAIN_ENV:
            return BTN_PARAM_1;

        case TRACK_RUNTIME_PARAM_DOMAIN_TONE:
            return BTN_PARAM_2;

        case TRACK_RUNTIME_PARAM_DOMAIN_MOD:
            return BTN_PARAM_3;

        case TRACK_RUNTIME_PARAM_DOMAIN_MIX:
            return BTN_PARAM_4;

        case TRACK_RUNTIME_PARAM_DOMAIN_PLAY:
            return BTN_PARAM_5;

        default:
            return BTN_COUNT;
    }
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

    led_layer_set(LED_LAYER_UI,
                  led,
                  LED_FIXED_GREEN_R,
                  LED_FIXED_GREEN_G,
                  LED_FIXED_GREEN_B);
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

static void led_apply_route_destination_hall_scene(led_id_t led)
{
    led_layer_set(LED_LAYER_UI, led, 0U, (uint8_t)(LED_FIXED_GREEN_G / 2U), 0U);
}

static void led_apply_sampler_looper_routing_hall_scene(uint8_t hall, uint8_t destination_track)
{
    const led_id_t led = led_remap_led_for_hall(hall);
    if (hall >= UI_ACTIVE_TRACK_COUNT)
    {
        led_layer_set(LED_LAYER_UI, led, 0U, 0U, 0U);
        return;
    }

    if (hall == destination_track)
    {
        led_apply_route_destination_hall_scene(led);
        return;
    }

    if (ui_core_runtime_bridge_get_looper_route_enabled(destination_track, hall) == 0U)
    {
        led_layer_set(LED_LAYER_UI, led, LED_FIXED_DIM_WHITE, LED_FIXED_DIM_WHITE, LED_FIXED_DIM_WHITE);
        return;
    }

    led_layer_set(LED_LAYER_UI, led, LED_FIXED_RED_R, LED_FIXED_ORANGE_G, 0U);
}

static void led_apply_audio_rec_hall_scene(uint8_t hall)
{
    const led_id_t led = led_remap_led_for_hall(hall);
    if (hall >= UI_ACTIVE_TRACK_COUNT)
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

static void led_apply_macro_scene_hall_scene(uint8_t hall)
{
    const led_id_t led = led_remap_led_for_hall(hall);
    led_rgb_color_t color = led_macro_scene_color(hall);
    uint8_t held_scene = PROJECT_V1_MACRO_SCENE_COUNT;
    uint8_t scale = (project_v1_macro_scene_has_locks(hall) != 0U) ? 180U : 45U;

    if ((ui_macro_interaction_get_held_scene(&held_scene) != 0U) && (held_scene == hall))
    {
        scale = 255U;
    }

    color = led_scale_color(color, scale);
    led_layer_set(LED_LAYER_UI, led, color.r, color.g, color.b);
}

static void led_apply_macro_switch_hall_scene(uint8_t hall)
{
    const led_id_t led = led_remap_led_for_hall(hall);
    led_rgb_color_t color = led_macro_scene_color(hall);
    uint8_t scale = (project_v1_macro_scene_has_locks(hall) != 0U) ? 170U : 45U;
    scale = led_macro_pressure_depth_scale(hall, scale);

    color = led_scale_color(color, scale);
    led_layer_set(LED_LAYER_UI, led, color.r, color.g, color.b);
}

static void led_apply_track_select_hall_scene(uint8_t hall)
{
    const led_id_t led = led_remap_led_for_hall(hall);
    uint8_t r = 0U;
    uint8_t g = 0U;
    uint8_t b = 0U;

    if (hall < UI_ACTIVE_TRACK_COUNT)
    {
        if (ui_get_track_family(hall) != UI_TRACK_FAMILY_OFF)
        {
            r = LED_FIXED_DARK_BLUE_R;
            g = LED_FIXED_DARK_BLUE_G;
            b = LED_FIXED_DARK_BLUE_B;

            if (hall == ui_get_active_track())
            {
                r = LED_FIXED_WHITE_R;
                g = LED_FIXED_WHITE_G;
                b = LED_FIXED_WHITE_B;
            }
        }
    }

    led_layer_set(LED_LAYER_UI, led, r, g, b);
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
        led_layer_set(LED_LAYER_UI, led, LED_FIXED_RED_R, LED_FIXED_RED_G, LED_FIXED_RED_B);
    }
    else
    {
        led_layer_set(LED_LAYER_UI, led, LED_FIXED_GREEN_R, LED_FIXED_GREEN_G, LED_FIXED_GREEN_B);
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

    const uint8_t held_plock_sets = led_seq_collect_held_plock_set_mask();
    button_id_t macro_button = BTN_COUNT;
    param_id_t macro_param = PARAM_COUNT;
    const ui_hall_mode_t hall_mode = ui_get_hall_mode();
    const uint8_t active_track = ui_get_active_track();
    const uint8_t active_page_id = ui_page_get_id();
    const ui_hall_rout_context_t rout_context =
        ui_hall_mode_resolve_rout_context(active_track, hall_mode);
    const button_id_t active_button = ui_navigation_get_button_for_page(active_page_id);
    const led_id_t active_param_led = led_remap_param_led_for_button(active_button);
    if (ui_macro_interaction_get_active_lock_param(&macro_param) != 0U)
    {
        macro_button = led_macro_param_to_button(macro_param);
    }

    if (ui_page_patch_assign_is_open() != 0U)
    {
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
        {
            (void)led_apply_patch_assign_hall_scene(hall);
        }
    }
    else if (ui_macro_overlay_is_active() != 0U)
    {
        ui_macro_overlay_submode_t overlay_submode = UI_MACRO_OVERLAY_SUBMODE_CTRL;
        (void)ui_macro_overlay_get_submode(&overlay_submode);
        const uint8_t switch_mode =
            (uint8_t)(overlay_submode == UI_MACRO_OVERLAY_SUBMODE_CTRL);
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
        {
            if (switch_mode != 0U)
            {
                led_apply_macro_switch_hall_scene(hall);
            }
            else
            {
                led_apply_macro_scene_hall_scene(hall);
            }
        }
    }
    else if (ui_step_led_ownership_page_needs_step_leds(active_page_id) != 0U)
    {
        seq_led_render_active_track_page();
    }
    else if (hall_mode == UI_HALL_MODE_MUTE)
    {
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
        {
            (void)led_apply_mute_hall_scene(hall);
        }
    }
    else if (ui_is_track_modifier_held() != 0U)
    {
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
        {
            led_apply_track_select_hall_scene(hall);
        }
    }
    else if (led_hall_mode_uses_seq_scene(hall_mode))
    {
        seq_led_render_active_track_page();
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
            else if (rout_context == UI_HALL_ROUT_CONTEXT_SAMPLER_LOOPER)
            {
                led_apply_sampler_looper_routing_hall_scene(hall, active_track);
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
                                         macro_button,
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
