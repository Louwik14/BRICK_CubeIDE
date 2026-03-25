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

#include "Keyboard/keyboard_runtime.h"
#include "led_remap.h"
#include "led_anim.h"
#include "led_layer.h"
#include "UI/ui_core.h"
#include "UI/ui_navigation.h"
#include "UI/ui_page_manager.h"
#include "Seq/seq_led.h"

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

static void led_apply_param_button_scene(led_id_t led)
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

static bool led_hall_mode_uses_keyboard_scene(void)
{
    const ui_hall_mode_t mode = ui_get_hall_mode();
    return (mode == UI_HALL_MODE_KEYBOARD) || (mode == UI_HALL_MODE_ARP);
}

static void led_apply_fixed_scene(void)
{
    led_layer_clear_all();

    if (ui_get_hall_mode() == UI_HALL_MODE_SEQ)
    {
        seq_led_render_active_track_page();
    }
    else
    {
        for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
        {
            if (led_hall_mode_uses_keyboard_scene())
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
            led_apply_param_button_scene((led_id_t)led);
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
