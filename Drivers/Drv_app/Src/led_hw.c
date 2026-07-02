/**
 * @file led_hw.c
 * @brief Module applicatif led_hw.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à led_hw.
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

#include "led_hw.h"

#include "Storage/cache_maintenance.h"
#include "Storage/memory_layout.h"
#include "tim.h"

#include <stddef.h>
#include <string.h>

#define LED_HW_BITS_PER_LED 24U
#define LED_HW_RESET_SLOTS 100U

#define WS2812_0 84U
#define WS2812_1 175U

#define LED_HW_BUFFER_SIZE ((LED_HW_COUNT * LED_HW_BITS_PER_LED) + LED_HW_RESET_SLOTS)

/*
 * Shared CPU/DMA emission buffer:
 * - CPU writes PWM symbols
 * - TIM1 DMA reads payload
 * Kept in DMA section and 32B aligned for future D-cache enablement.
 */
static DMA_BUFFER uint32_t pwm_buffer[LED_HW_BUFFER_SIZE];
static volatile uint8_t dma_busy = 0U;

/**
 * @brief Point d'entrée led_hw_encode.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_hw_encode.
 *
 * @param rgb Paramètre d'entrée de l'API.
 * @param count Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void led_hw_encode(const uint8_t *rgb, uint32_t count)
{
    uint32_t idx = 0U;

    for (uint32_t i = 0U; i < count; i++)
    {
        const uint8_t colors[3] =
        {
            rgb[(i * 3U) + 1U],
            rgb[(i * 3U) + 0U],
            rgb[(i * 3U) + 2U]
        };

        for (uint32_t c = 0U; c < 3U; c++)
        {
            uint8_t byte = colors[c];

            for (int32_t bit = 7; bit >= 0; bit--)
            {
                if ((byte & (1U << bit)) != 0U)
                {
                    pwm_buffer[idx++] = WS2812_1;
                }
                else
                {
                    pwm_buffer[idx++] = WS2812_0;
                }
            }
        }
    }

    while (idx < LED_HW_BUFFER_SIZE)
    {
        pwm_buffer[idx++] = 0U;
    }
}

/**
 * @brief Point d'entrée HAL_TIM_PWM_PulseFinishedCallback.
 *
 * Rôle:
 * - Exécuter le traitement associé à HAL_TIM_PWM_PulseFinishedCallback.
 *
 * @param htim Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
        HAL_TIM_PWM_Stop_DMA(&htim1, TIM_CHANNEL_3);
        dma_busy = 0U;
    }
}

/**
 * @brief Point d'entrée led_hw_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_hw_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_hw_init(void)
{
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
    memset(pwm_buffer, 0, sizeof(pwm_buffer));
    dma_busy = 0U;
}

/**
 * @brief Point d'entrée led_hw_busy.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_hw_busy.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool led_hw_busy(void)
{
    return (dma_busy != 0U);
}

/**
 * @brief Point d'entrée led_hw_send.
 *
 * Rôle:
 * - Exécuter le traitement associé à led_hw_send.
 *
 * @param rgb Paramètre d'entrée de l'API.
 * @param count Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void led_hw_send(const uint8_t *rgb, uint32_t count)
{
    if ((rgb == NULL) || (count == 0U) || (count > LED_HW_COUNT) || led_hw_busy())
    {
        return;
    }

    led_hw_encode(rgb, count);

    dcache_clean_by_addr_aligned(pwm_buffer, sizeof(pwm_buffer));

    dma_busy = 1U;

    HAL_TIM_PWM_Stop_DMA(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_SET_COUNTER(&htim1, 0U);

    if (HAL_TIM_PWM_Start_DMA(&htim1, TIM_CHANNEL_3, pwm_buffer, LED_HW_BUFFER_SIZE) != HAL_OK)
    {
        dma_busy = 0U;
    }
}
