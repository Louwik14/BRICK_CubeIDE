/**
 * @file drv_encoders.c
 * @brief Module applicatif drv_encoders.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à drv_encoders.
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

#include "drv_encoders.h"

/**
 * @brief Point d'entrée drv_encoders_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_encoders_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_encoders_init(void)
{
    encoders_init();
}

/**
 * @brief Point d'entrée drv_encoders_poll.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_encoders_poll.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_encoders_poll(void)
{
    encoders_update(0U);
}

/**
 * @brief Point d'entrée drv_encoder_get_delta.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_encoder_get_delta.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
int16_t drv_encoder_get_delta(uint8_t id)
{
    return encoder_consume_delta(id);
}

/**
 * @brief Point d'entrée drv_encoder_reset.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_encoder_reset.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_encoder_reset(uint8_t id)
{
    encoder_reset_delta(id);
}
