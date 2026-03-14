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

#include "led_anim.h"
#include "led_layer.h"

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
    led_anim_tick(dt_ms);
    led_layer_commit();
}
