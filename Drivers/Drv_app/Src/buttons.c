/**
 * @file buttons.c
 * @brief Module applicatif buttons.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à buttons.
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

#include "buttons.h"

#include <string.h>

#include "buttons_hw.h"

#define BUTTONS_DEBOUNCE_MS 10U

static button_state_t button_states[BTN_COUNT];

/**
 * @brief Point d'entrée buttons_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à buttons_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void buttons_init(void)
{
    memset(button_states, 0, sizeof(button_states));
    buttons_hw_init();
    buttons_hw_read();

    for (uint32_t i = 0U; i < (uint32_t)BTN_COUNT; i++)
    {
        uint8_t raw = buttons_hw_get((button_id_t)i);
        button_states[i].state = raw;
        button_states[i].prev_state = raw;
    }
}

/**
 * @brief Point d'entrée buttons_update.
 *
 * Rôle:
 * - Exécuter le traitement associé à buttons_update.
 *
 * @param dt_ms Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void buttons_update(uint32_t dt_ms)
{
    buttons_hw_read();

    for (uint32_t i = 0U; i < (uint32_t)BTN_COUNT; i++)
    {
        button_state_t *s = &button_states[i];
        uint8_t raw = buttons_hw_get((button_id_t)i);



        if (raw != s->state)
        {
            uint32_t acc = (uint32_t)s->debounce + dt_ms;
            if (acc >= BUTTONS_DEBOUNCE_MS)
            {
                s->prev_state = s->state;
                s->state = raw;
                s->debounce = 0U;

                if ((s->prev_state == 0U) && (s->state != 0U))
                {
                    s->pressed = 1U;
                }
                else if ((s->prev_state != 0U) && (s->state == 0U))
                {
                    s->released = 1U;
                }
            }
            else
            {
                s->debounce = (uint16_t)acc;
            }
        }
        else
        {
            s->debounce = 0U;
        }
    }
}

/**
 * @brief Point d'entrée button_pressed.
 *
 * Rôle:
 * - Exécuter le traitement associé à button_pressed.
 *
 * @param btn Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint8_t button_pressed(button_id_t btn)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    uint8_t v = button_states[(uint32_t)btn].pressed;
    button_states[(uint32_t)btn].pressed = 0U;
    return v;
}

/**
 * @brief Point d'entrée button_released.
 *
 * Rôle:
 * - Exécuter le traitement associé à button_released.
 *
 * @param btn Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint8_t button_released(button_id_t btn)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    uint8_t v = button_states[(uint32_t)btn].released;
    button_states[(uint32_t)btn].released = 0U;
    return v;
}
/**
 * @brief Point d'entrée button_down.
 *
 * Rôle:
 * - Exécuter le traitement associé à button_down.
 *
 * @param btn Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint8_t button_down(button_id_t btn)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    return button_states[(uint32_t)btn].state;
}
