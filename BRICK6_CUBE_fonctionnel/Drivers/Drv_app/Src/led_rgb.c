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

#include "Keyboard/keyboard_runtime.h"
#include "led_remap.h"
#include "led_anim.h"
#include "led_layer.h"
#include "UI/ui_core.h"
#include "UI/ui_navigation.h"
#include "UI/ui_page_manager.h"
#include "Seq/seq_led.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_runtime.h"

#define LED_FIXED_HALF_BRIGHTNESS 128U
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
#define LED_FIXED_BLUE_R          0U
#define LED_FIXED_BLUE_G          0U
#define LED_FIXED_BLUE_B          LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_ORANGE_R        LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_ORANGE_G        64U
#define LED_FIXED_ORANGE_B        0U
#define LED_FIXED_RED_R           LED_FIXED_HALF_BRIGHTNESS
#define LED_FIXED_RED_G           0U
#define LED_FIXED_RED_B           0U

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

static void led_apply_param_button_scene(led_id_t led, uint8_t held_plock_sets)
{
    uint8_t r = LED_FIXED_GREEN_R;
    uint8_t g = LED_FIXED_GREEN_G;
    uint8_t b = LED_FIXED_GREEN_B;

    const button_id_t active_button = ui_navigation_get_button_for_page(ui_page_get_id());
    if (led == led_remap_param_led_for_button(active_button))
    {
        r = LED_FIXED_WHITE_R;
        g = LED_FIXED_WHITE_G;
        b = LED_FIXED_WHITE_B;
    }

    if (held_plock_sets != 0U)
    {
        uint8_t match_set = 0U;
        if ((led == led_remap_param_led_for_button(BTN_PARAM_1))
                && ((held_plock_sets & seq_param_iface_set_to_mask((uint8_t)SEQ_PLOCK_SET_COLORS)) != 0U))
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

    led_layer_set(LED_LAYER_UI, led, r, g, b);
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

static bool led_hall_mode_uses_keyboard_scene(void)
{
    const ui_hall_mode_t mode = ui_get_hall_mode();
    return (mode == UI_HALL_MODE_KEYBOARD) || (mode == UI_HALL_MODE_ARP);
}

static void led_apply_fixed_scene(void)
{
    led_layer_clear_all();
    const uint8_t held_plock_sets = led_seq_collect_held_plock_set_mask();

    if (ui_get_hall_mode() == UI_HALL_MODE_SEQ)
    {
        seq_led_render_active_track_page();
    }
    else
    {
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
        {
            if (ui_get_hall_mode() == UI_HALL_MODE_PATTERN)
            {
                led_apply_pattern_hall_scene(hall);
            }
            else if (led_hall_mode_uses_keyboard_scene())
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
        else if (led_remap_is_param_led((led_id_t)led))
        {
            led_apply_param_button_scene((led_id_t)led, held_plock_sets);
        }
        else
        {
            if ((led_id_t)led == LED_REC)
            {
                uint8_t rec_on = 0U;
                if (seq_runtime_rec_is_armed() != 0U)
                {
                    if (seq_runtime_rec_is_pattern_pending_start() != 0U)
                    {
                        rec_on = (uint8_t)(((HAL_GetTick() / 150U) & 0x1U) != 0U ? 1U : 0U);
                    }
                    else if (seq_runtime_get_rec_count_in_remaining_steps() > 0U)
                    {
                        const uint32_t remaining_steps = seq_runtime_get_rec_count_in_remaining_steps();
                        rec_on = (uint8_t)((remaining_steps & 0x1U) == 0U ? 1U : 0U);
                    }
                    else
                    {
                        rec_on = 1U;
                    }
                }

                if (rec_on != 0U)
                {
                    led_layer_set(LED_LAYER_UI,
                                  (led_id_t)led,
                                  LED_FIXED_RED_R,
                                  LED_FIXED_RED_G,
                                  LED_FIXED_RED_B);
                }
                else
                {
                    led_layer_set(LED_LAYER_UI, (led_id_t)led, 0U, 0U, 0U);
                }
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
