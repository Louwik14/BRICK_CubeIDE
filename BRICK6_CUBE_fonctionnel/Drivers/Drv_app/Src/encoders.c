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

#define ENCODER_MAX_STEP_PER_TICK 8

static int16_t enc_accumulated_delta[ENC_COUNT];

/**
 * @brief Point d'entrée encoder_clamp_step.
 *
 * Rôle:
 * - Exécuter le traitement associé à encoder_clamp_step.
 *
 * @param value Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static int16_t encoder_clamp_step(int16_t value)
{
    if (value > ENCODER_MAX_STEP_PER_TICK)
    {
        return ENCODER_MAX_STEP_PER_TICK;
    }

    if (value < -ENCODER_MAX_STEP_PER_TICK)
    {
        return -ENCODER_MAX_STEP_PER_TICK;
    }

    return value;
}

/**
 * @brief Point d'entrée encoder_accumulate_saturating.
 *
 * Rôle:
 * - Exécuter le traitement associé à encoder_accumulate_saturating.
 *
 * @param current Paramètre d'entrée de l'API.
 * @param delta Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static int16_t encoder_accumulate_saturating(int16_t current, int16_t delta)
{
    int32_t sum = (int32_t)current + (int32_t)delta;

    if (sum > 32767)
    {
        sum = 32767;
    }
    else if (sum < -32768)
    {
        sum = -32768;
    }

    return (int16_t)sum;
}

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
    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        enc_accumulated_delta[i] = 0;
    }

    encoders_hw_init();
    encoders_fast_poll_init();
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
void encoders_update(uint32_t dt_ms)
{
    (void)dt_ms;

    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        int16_t delta = (int16_t)encoders_hw_get_delta(i);
        delta = encoder_clamp_step(delta);

        if (delta == 0)
        {
            continue;
        }

        enc_accumulated_delta[i] = encoder_accumulate_saturating(enc_accumulated_delta[i], delta);
    }
}

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
int16_t encoder_get_delta(uint8_t encoder)
{
    if (encoder >= (uint8_t)ENC_COUNT)
    {
        return 0;
    }

    return enc_accumulated_delta[encoder];
}

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
int16_t encoder_consume_delta(uint8_t encoder)
{
    if (encoder >= (uint8_t)ENC_COUNT)
    {
        return 0;
    }

    const int16_t delta = enc_accumulated_delta[encoder];
    enc_accumulated_delta[encoder] = 0;
    return delta;
}
