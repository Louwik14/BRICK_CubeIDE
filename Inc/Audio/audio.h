#pragma once

#include <stdint.h>

#include "Board/board_audio.h"

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
 * - audio_boot_init_binding_io()/audio_start(): contexte main loop (non IRQ).
 * - Callbacks HAL_SAI_RxHalfCpltCallback / HAL_SAI_RxCpltCallback: contexte IRQ,
 *   budget strict (pas de blocage, pas d'allocation dynamique, pas de logs).
 */

/**
 * @brief Initialise la couche audio bas niveau.
 *
 * Contexte d'appel:
 * - Main loop uniquement (phase d'init).
 *
 * Effets de bord:
 * - Mémorise les handles SAI dans l'état statique du module.
 * - Remet à zéro les buffers DMA RX/TX internes.
 */
void audio_boot_init_binding_io(void);

/**
 * @brief Initialise le codec et démarre les flux DMA audio vérifiés.
 *
 * Contexte d'appel:
 * - Main loop uniquement.
 *
 * Effets de bord:
 * - Lance le bootstrap codec/SAI via le backend Board.
 * - Active les interruptions half/full transfer via HAL.
 * - Retourne 1 uniquement lorsque l'état atteint AUDIO_INIT_READY.
 */
uint8_t audio_start(void);
void audio_stop(void);

/* Exceptional physical boot diagnostic. Nominal CONTROL behavior must not
 * depend on this publication. */
typedef struct
{
    audio_init_state_t state;
    board_audio_boot_error_t error;
} audio_boot_diag_snapshot_t;

void audio_boot_diag_read(audio_boot_diag_snapshot_t *out_diag);
