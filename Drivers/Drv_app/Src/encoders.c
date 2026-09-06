/**
 * @file encoders.c
 * @brief Module applicatif encoders.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à encoders.
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

#include "encoders.h"

#include "encoders_hw.h"

/**
 * @brief Point d'entrée encoders_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à encoders_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void encoders_init(void)
{
    encoders_hw_init();
}

void encoders_start_fast_poll(void)
{
    encoders_fast_poll_init();
}

void encoders_discard_pending(void)
{
    encoders_hw_discard_pending();
}

/**
 * @brief Point d'entrée encoders_update.
 *
 * Rôle:
 * - Exécuter le traitement associé à encoders_update.
 *
 * @param dt_ms Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/**
 * @brief Point d'entrée encoder_get_delta.
 *
 * Rôle:
 * - Exécuter le traitement associé à encoder_get_delta.
 *
 * @param encoder Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/**
 * @brief Point d'entrée encoder_consume_delta.
 *
 * Rôle:
 * - Exécuter le traitement associé à encoder_consume_delta.
 *
 * @param encoder Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint8_t encoder_detent_event_pop(encoder_detent_event_t *out_event)
{
    return encoders_hw_pop_detent_event(out_event);
}

uint32_t encoder_detent_event_pending_count(void)
{
    return encoders_hw_get_detent_pending_count();
}

uint32_t encoder_detent_event_overflow_count(void)
{
    return encoders_hw_get_detent_overflow_count();
}
