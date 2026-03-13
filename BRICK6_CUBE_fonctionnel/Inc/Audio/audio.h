#pragma once

#include "stm32h7xx_hal.h"
#include <stdint.h>

/**
 * @file audio.h
 * @brief Couche bas niveau audio SAI + DMA (double buffer) pour STM32H743.
 *
 * Rôle du module:
 * - Encapsuler l'initialisation et le démarrage SAI RX/TX en DMA.
 * - Gérer les callbacks DMA half/full et déléguer le traitement bloc au moteur float.
 *
 * Architecture:
 * - Appelé par l'initialisation applicative (brick6_app_init).
 * - Les IRQ DMA SAI appellent en interne audio_process_block_int32().
 *
 * Contraintes temps réel:
 * - audio_init()/audio_start(): contexte main loop (non IRQ).
 * - Callbacks HAL_SAI_RxHalfCpltCallback / HAL_SAI_RxCpltCallback: contexte IRQ,
 *   budget strict (pas de blocage, pas d'allocation dynamique, pas de logs).
 */

/**
 * @brief Initialise la couche audio bas niveau pour deux liens SAI TDM4.
 *
 * @param hsai1_tx Handle SAI1 TX (tracks 1/2).
 * @param hsai1_rx Handle SAI1 RX (tracks 1/2).
 * @param hsai2_tx Handle SAI2 TX (tracks 3/4).
 * @param hsai2_rx Handle SAI2 RX (tracks 3/4, master trigger DSP).
 *
 * Contexte d'appel:
 * - Main loop uniquement (phase d'init).
 *
 * Effets de bord:
 * - Mémorise les handles SAI dans l'état statique du module.
 * - Remet à zéro les buffers DMA RX/TX internes.
 */
void audio_init(SAI_HandleTypeDef *hsai1_tx,
                SAI_HandleTypeDef *hsai1_rx,
                SAI_HandleTypeDef *hsai2_tx,
                SAI_HandleTypeDef *hsai2_rx);

/**
 * @brief Démarre les flux DMA audio RX puis TX.
 *
 * Contexte d'appel:
 * - Main loop uniquement.
 *
 * Effets de bord:
 * - Lance HAL_SAI_Receive_DMA() puis HAL_SAI_Transmit_DMA().
 * - Active les interruptions half/full transfer via HAL.
 */
void audio_start(void);

/* User DSP callback (héritage API historique, non utilisé par audio.c actuel). */
typedef void (*audio_process_fn)(int32_t *rx,
                                 int32_t *tx,
                                 uint32_t frames);

typedef struct
{
    uint32_t sai1_irq_count;
    uint32_t sai2_irq_count;
    uint32_t dsp_blocks;
    int32_t  last_sample_sai1;
    int32_t  last_sample_sai2;
    uint8_t  desync_flag;
} audio_debug_diag_t;

/**
 * @brief Enregistre un callback de traitement bas niveau (API conservée).
 *
 * @param cb Callback utilisateur recevant RX/TX int32 et taille bloc (frames).
 *
 * @note API conservée pour compatibilité. Le chemin actif passe par
 *       audio_process_block_int32() côté audio_float.c.
 */
void audio_set_process_callback(audio_process_fn cb);

void audio_debug_get_diag(audio_debug_diag_t *out_diag);
void audio_debug_print_diag(void);
