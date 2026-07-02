/**
 * @file encoders_hw.c
 * @brief Module applicatif encoders_hw.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à encoders_hw.
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

#include "encoders_hw.h"

#include <limits.h>

#include "main.h"
#include "tim.h"

typedef struct
{
    GPIO_TypeDef *port_a;
    uint32_t pin_a;
    GPIO_TypeDef *port_b;
    uint32_t pin_b;
} encoder_hw_pin_t;

static const encoder_hw_pin_t enc_hw_pins[ENC_COUNT] = {
    {GPIOB, GPIO_PIN_0, GPIOB, GPIO_PIN_1},
    {GPIOH, GPIO_PIN_6, GPIOH, GPIO_PIN_7},
    {GPIOB, GPIO_PIN_12, GPIOB, GPIO_PIN_13},
    {GPIOD, GPIO_PIN_12, GPIOD, GPIO_PIN_13},
};

static uint8_t enc_prev_state[ENC_COUNT];
static volatile int16_t enc_raw_delta[ENC_COUNT];

static const int8_t quad_table[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

/**
 * @brief Point d'entrée enc_read_pin.
 *
 * Rôle:
 * - Exécuter le traitement associé à enc_read_pin.
 *
 * @param port Paramètre d'entrée de l'API.
 * @param pin Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline uint8_t enc_read_pin(GPIO_TypeDef *port, uint32_t pin)
{
    return ((port->IDR & pin) != 0U) ? 1U : 0U;
}

/**
 * @brief Point d'entrée enc_read_state.
 *
 * Rôle:
 * - Exécuter le traitement associé à enc_read_state.
 *
 * @param encoder Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static uint8_t enc_read_state(uint8_t encoder)
{
    const encoder_hw_pin_t *h = &enc_hw_pins[encoder];
    const uint8_t a = enc_read_pin(h->port_a, h->pin_a);
    const uint8_t b = enc_read_pin(h->port_b, h->pin_b);
    return (uint8_t)((a << 1) | b);
}

/**
 * @brief Point d'entrée encoders_hw_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à encoders_hw_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void encoders_hw_init(void)
{
    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        enc_prev_state[i] = enc_read_state(i);
        enc_raw_delta[i] = 0;
    }
}

/**
 * @brief Point d'entrée encoders_fast_poll_init.
 *
 * Rôle:
 * - Démarrer le timer dédié au polling rapide des encodeurs à 5 kHz.
 *
 *
 * Contexte d'appel:
 * - init uniquement.
 */
void encoders_fast_poll_init(void)
{
    if (HAL_TIM_Base_Start_IT(&htim7) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief Point d'entrée encoders_fast_poll_irq.
 *
 * Rôle:
 * - Exécuter la capture matérielle minimale des encodeurs depuis l'IRQ timer dédiée.
 *
 *
 * Contexte d'appel:
 * - IRQ timer encodeurs uniquement, chemin ultra court.
 */
void encoders_fast_poll_irq(void)
{
    encoders_hw_read();
}

/**
 * @brief Point d'entrée encoders_hw_read.
 *
 * Rôle:
 * - Exécuter le traitement associé à encoders_hw_read.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void encoders_hw_read(void)
{
    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        const uint8_t prev = enc_prev_state[i];
        const uint8_t now = enc_read_state(i);

        if (now == prev)
        {
            continue;
        }

        const uint8_t idx = (uint8_t)((prev << 2) | now);
        const int8_t step = quad_table[idx];

        enc_prev_state[i] = now;

        if (step == 0)
        {
            continue;
        }

        const int32_t sum = (int32_t)enc_raw_delta[i] + (int32_t)step;
        if (sum > (int32_t)INT16_MAX)
        {
            enc_raw_delta[i] = INT16_MAX;
        }
        else if (sum < (int32_t)INT16_MIN)
        {
            enc_raw_delta[i] = INT16_MIN;
        }
        else
        {
            enc_raw_delta[i] = (int16_t)sum;
        }
    }
}

/**
 * @brief Point d'entrée encoders_hw_get_delta.
 *
 * Rôle:
 * - Exécuter le traitement associé à encoders_hw_get_delta.
 *
 * @param encoder Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
int16_t encoders_hw_get_delta(uint8_t encoder)
{
    if (encoder >= (uint8_t)ENC_COUNT)
    {
        return 0;
    }

    int16_t delta;

    __disable_irq();
    delta = enc_raw_delta[encoder];
    enc_raw_delta[encoder] = 0;
    __enable_irq();

    return delta;
}
