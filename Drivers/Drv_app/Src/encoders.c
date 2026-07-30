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

#include <limits.h>

#include "encoders_hw.h"

#define ENCODER_MAX_CONSUME_PER_TICK 4

static int32_t enc_accumulated_delta[ENC_COUNT];

static int32_t encoder_compute_drain(int32_t backlog)
{
    int32_t abs_backlog;
    int32_t drain_mag;

    if (backlog == 0)
    {
        return 0;
    }

    if (backlog == INT32_MIN)
    {
        abs_backlog = INT32_MAX;
    }
    else
    {
        abs_backlog = (backlog > 0) ? backlog : -backlog;
    }

    /*
     * Adaptive drain:
     * - tiny backlog: fine-grain restitution
     * - large backlog: accelerates progressively up to a bounded max
     */
    drain_mag = (abs_backlog / 3) + 1;
    if (drain_mag > ENCODER_MAX_CONSUME_PER_TICK)
    {
        drain_mag = ENCODER_MAX_CONSUME_PER_TICK;
    }

    return (backlog > 0) ? drain_mag : -drain_mag;
}

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
static int16_t encoder_clamp_to_i16(int32_t value)
{
    if (value > (int32_t)INT16_MAX)
    {
        return INT16_MAX;
    }

    if (value < (int32_t)INT16_MIN)
    {
        return INT16_MIN;
    }

    return (int16_t)value;
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
static int32_t encoder_accumulate_saturating(int32_t current, int32_t delta)
{
    int64_t sum = (int64_t)current + (int64_t)delta;

    if (sum > (int64_t)INT32_MAX)
    {
        sum = (int64_t)INT32_MAX;
    }
    else if (sum < (int64_t)INT32_MIN)
    {
        sum = (int64_t)INT32_MIN;
    }

    return (int32_t)sum;
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
        const int32_t delta = (int32_t)encoders_hw_get_delta(i);

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

    return encoder_clamp_to_i16(enc_accumulated_delta[encoder]);
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

    const int32_t backlog = enc_accumulated_delta[encoder];
    if (backlog == 0)
    {
        return 0;
    }

    const int32_t drain = encoder_compute_drain(backlog);

    enc_accumulated_delta[encoder] = backlog - drain;
    return (int16_t)drain;
}

void encoder_reset_delta(uint8_t encoder)
{
    if (encoder >= (uint8_t)ENC_COUNT)
    {
        return;
    }

    enc_accumulated_delta[encoder] = 0;
}

#if BRICK_TEST_BUILD
uint8_t encoder_test_inject_delta(uint8_t encoder, int16_t delta)
{
    if (encoder >= (uint8_t)ENC_COUNT)
    {
        return 0U;
    }

    enc_accumulated_delta[encoder] =
        encoder_accumulate_saturating(enc_accumulated_delta[encoder],
                                      (int32_t)delta);
    return 1U;
}
#endif
