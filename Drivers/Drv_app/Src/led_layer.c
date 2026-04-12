/**
 * @file led_layer.c
 * @brief Module applicatif led_layer.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à led_layer.
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

#include "led_layer.h"

#include <string.h>

static uint8_t led_layers[LED_LAYER_COUNT][LED_FB_COUNT][3U];

/**
 * @brief Point d'entrée sat_add_u8.
 *
 * Rôle:
 * - Exécuter le traitement associé à sat_add_u8.
 *
 * @param a Paramètre d'entrée de l'API.
 * @param b Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static uint8_t sat_add_u8(uint8_t a, uint8_t b)
{
    uint16_t sum = (uint16_t)a + (uint16_t)b;

    if (sum > 255U)
    {
        return 255U;
    }

    return (uint8_t)sum;
}

/**
 * @brief Point d'entrée led_layer_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_layer_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_layer_init(void)
{
    memset(led_layers, 0, sizeof(led_layers));
}

/**
 * @brief Point d'entrée led_layer_clear.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_layer_clear.
 *
 * @param layer Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_layer_clear(uint32_t layer)
{
    if (layer >= (uint32_t)LED_LAYER_COUNT)
    {
        return;
    }

    memset(led_layers[layer], 0, sizeof(led_layers[layer]));
}

/**
 * @brief Point d'entrée led_layer_clear_all.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_layer_clear_all.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_layer_clear_all(void)
{
    memset(led_layers, 0, sizeof(led_layers));
}

/**
 * @brief Point d'entrée led_layer_set.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_layer_set.
 *
 * @param layer Paramètre d'entrée de l'API.
 * @param led Paramètre d'entrée de l'API.
 * @param r Paramètre d'entrée de l'API.
 * @param g Paramètre d'entrée de l'API.
 * @param b Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_layer_set(uint32_t layer, led_id_t led, uint8_t r, uint8_t g, uint8_t b)
{
    if ((layer >= (uint32_t)LED_LAYER_COUNT) || ((uint32_t)led >= LED_FB_COUNT))
    {
        return;
    }

    led_layers[layer][(uint32_t)led][0] = r;
    led_layers[layer][(uint32_t)led][1] = g;
    led_layers[layer][(uint32_t)led][2] = b;
}

/**
 * @brief Point d'entrée led_layer_fill.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_layer_fill.
 *
 * @param layer Paramètre d'entrée de l'API.
 * @param r Paramètre d'entrée de l'API.
 * @param g Paramètre d'entrée de l'API.
 * @param b Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_layer_fill(uint32_t layer, uint8_t r, uint8_t g, uint8_t b)
{
    if (layer >= (uint32_t)LED_LAYER_COUNT)
    {
        return;
    }

    for (uint32_t led = 0U; led < LED_FB_COUNT; led++)
    {
        led_layer_set(layer, (led_id_t)led, r, g, b);
    }
}

/**
 * @brief Point d'entrée led_layer_compose.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_layer_compose.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_layer_compose(void)
{
    led_fb_clear();

    for (uint32_t led = 0U; led < LED_FB_COUNT; led++)
    {
        uint8_t r = 0U;
        uint8_t g = 0U;
        uint8_t b = 0U;

        for (uint32_t layer = 0U; layer < (uint32_t)LED_LAYER_COUNT; layer++)
        {
            r = sat_add_u8(r, led_layers[layer][led][0]);
            g = sat_add_u8(g, led_layers[layer][led][1]);
            b = sat_add_u8(b, led_layers[layer][led][2]);
        }

        led_fb_set((led_id_t)led, r, g, b);
    }
}

/**
 * @brief Point d'entrée led_layer_commit.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_layer_commit.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_layer_commit(void)
{
    led_layer_compose();
    led_fb_commit();
}
