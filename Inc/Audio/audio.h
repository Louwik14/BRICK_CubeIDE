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
 * @brief Initialise la couche audio bas niveau.
 *
 * @param hsai_tx Handle SAI de transmission (DAC/TX).
 * @param hsai_rx Handle SAI de réception (ADC/RX).
 *
 * Contexte d'appel:
 * - Main loop uniquement (phase d'init).
 *
 * Effets de bord:
 * - Mémorise les handles SAI dans l'état statique du module.
 * - Remet à zéro les buffers DMA RX/TX internes.
 */
void audio_init(SAI_HandleTypeDef *hsai_tx,
                SAI_HandleTypeDef *hsai_rx);

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
    uint16_t max_events_collected_per_half;
    uint16_t max_subsegments_per_half;
} audio_seq_diag_t;

/**
 * @brief Enregistre un callback de traitement bas niveau (API conservée).
 *
 * @param cb Callback utilisateur recevant RX/TX int32 et taille bloc (frames).
 *
 * @note API conservée pour compatibilité. Le chemin actif passe par
 *       audio_process_block_int32() côté audio_float.c.
 */
void audio_set_process_callback(audio_process_fn cb);
void audio_seq_diag_reset(void);
void audio_seq_diag_snapshot(audio_seq_diag_t *out_diag);
