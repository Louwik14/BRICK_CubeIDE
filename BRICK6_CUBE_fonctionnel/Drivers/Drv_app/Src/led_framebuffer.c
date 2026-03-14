/**
 * @file led_framebuffer.c
 * @brief Module applicatif led_framebuffer.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à led_framebuffer.
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

#include "led_framebuffer.h"

#include <string.h>

static uint8_t led_fb_rgb[LED_FB_COUNT * 3U];

/**
 * @brief Point d'entrée led_fb_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_fb_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_fb_init(void)
{
    led_hw_init();
    led_fb_clear();
}

/**
 * @brief Point d'entrée led_fb_clear.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_fb_clear.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_fb_clear(void)
{
    memset(led_fb_rgb, 0, sizeof(led_fb_rgb));
}

/**
 * @brief Point d'entrée led_fb_set.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_fb_set.
 *
 * @param led Paramètre d'entrée de l'API.
 * @param r Paramètre d'entrée de l'API.
 * @param g Paramètre d'entrée de l'API.
 * @param b Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_fb_set(led_id_t led, uint8_t r, uint8_t g, uint8_t b)
{
    if ((uint32_t)led >= LED_FB_COUNT)
    {
        return;
    }

    led_fb_rgb[((uint32_t)led * 3U) + 0U] = r;
    led_fb_rgb[((uint32_t)led * 3U) + 1U] = g;
    led_fb_rgb[((uint32_t)led * 3U) + 2U] = b;
}

/**
 * @brief Point d'entrée led_fb_fill.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_fb_fill.
 *
 * @param r Paramètre d'entrée de l'API.
 * @param g Paramètre d'entrée de l'API.
 * @param b Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_fb_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint32_t i = 0U; i < LED_FB_COUNT; i++)
    {
        led_fb_set((led_id_t)i, r, g, b);
    }
}

/**
 * @brief Point d'entrée led_fb_commit.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_fb_commit.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_fb_commit(void)
{
    if (led_hw_busy())
    {
        return;
    }

    led_hw_send(led_fb_rgb, LED_FB_COUNT);
}
