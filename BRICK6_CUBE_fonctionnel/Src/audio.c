/**
 * @file audio.c
 * @brief Couche matérielle audio STM32H743 (SAI TDM8 + DMA double buffer).
 *
 * Rôle du module:
 * - Configurer la mécanique de streaming RX/TX DMA en ping-pong (half/full).
 * - Déclencher le traitement bloc du moteur float via audio_process_block_int32().
 *
 * Architecture:
 * - Appelé par: brick6_app_init.c (audio_init, audio_start).
 * - Appelle: audio_process_block_int32() (audio_float.c),
 *            engine_tasklet_notify_frames() (scheduler applicatif).
 *
 * Contraintes temps réel:
 * - Le traitement audio est exécuté dans les callbacks IRQ DMA RX.
 * - Taille bloc temps réel: AUDIO_FRAMES_PER_HALF frames (32 ici).
 * - À 48 kHz, budget par half-buffer ≈ 32 / 48000 s = 666.7 µs.
 * - Aucun malloc/printf/HAL bloquant dans le chemin IRQ.
 */

#include "audio.h"
#include "audio_float.h"
#include "engine_tasklet.h"

#include <string.h>
#include <stdint.h>

/* ============================================================
   CONFIG AUDIO : STM32H743 + CS42448 TDM8
   ============================================================ */

/* TDM8 = 8 slots x 32-bit */
#define AUDIO_TDM_SLOTS          8

/* Frames traitées par interruption half DMA.
   Doit rester cohérent avec AUDIO_BLOCK_SIZE (audio_float.h). */
#define AUDIO_FRAMES_PER_HALF    32

/* Double buffer DMA: [half0 | half1] */
#define AUDIO_FRAMES_TOTAL       (AUDIO_FRAMES_PER_HALF * 2)

/* 1 frame TDM = 8 mots (slots) */
#define AUDIO_WORDS_PER_FRAME    AUDIO_TDM_SLOTS

/* Taille totale des buffers DMA (en int32) */
#define AUDIO_BUFFER_WORDS       (AUDIO_FRAMES_TOTAL * AUDIO_WORDS_PER_FRAME)

/* ============================================================
   DMA BUFFERS
   ============================================================ */

/* Buffers statiques ping-pong RX/TX (aucune allocation dynamique). */
static int32_t rx_buffer[AUDIO_BUFFER_WORDS];
static int32_t tx_buffer[AUDIO_BUFFER_WORDS];

/* ============================================================
   SAI HANDLES
   ============================================================ */

static SAI_HandleTypeDef *sai_tx = NULL;
static SAI_HandleTypeDef *sai_rx = NULL;

/* ============================================================
   INTERNAL PROCESSING
   Hardware layer only: calls float engine
   ============================================================ */

/**
 * @brief Traite une demi-zone DMA (half-buffer index 0 ou 1).
 *
 * @param half_index Index de moitié DMA: 0 = première moitié, 1 = seconde.
 *
 * Contexte d'appel:
 * - IRQ DMA RX uniquement (via callbacks HAL).
 *
 * Effets de bord:
 * - Lit rx_buffer[half], écrit tx_buffer[half].
 * - Appelle le moteur audio float avec AUDIO_FRAMES_PER_HALF frames.
 */
static void process_half(uint32_t half_index)
{
    uint32_t offset =
        half_index * AUDIO_FRAMES_PER_HALF * AUDIO_WORDS_PER_FRAME;

    int32_t *rx = &rx_buffer[offset];
    int32_t *tx = &tx_buffer[offset];

    /* Frontière moteur float (un bloc fixe par IRQ). */
    audio_process_block_int32(rx, tx, AUDIO_FRAMES_PER_HALF);
}

/* ============================================================
   API
   ============================================================ */

/** Voir audio.h */
void audio_init(SAI_HandleTypeDef *hsai_tx,
                SAI_HandleTypeDef *hsai_rx)
{
    sai_tx = hsai_tx;
    sai_rx = hsai_rx;

    memset(rx_buffer, 0, sizeof(rx_buffer));
    memset(tx_buffer, 0, sizeof(tx_buffer));
}

/** Voir audio.h */
void audio_start(void)
{
    if(!sai_tx || !sai_rx)
        return;

    /* Démarrage RX puis TX pour remplir d'abord les données entrantes. */
    HAL_SAI_Receive_DMA(sai_rx,
                        (uint8_t *)rx_buffer,
                        AUDIO_BUFFER_WORDS);

    HAL_SAI_Transmit_DMA(sai_tx,
                         (uint8_t *)tx_buffer,
                         AUDIO_BUFFER_WORDS);
}

/* ============================================================
   DMA IRQ CALLBACKS : AUDIO RUNS HERE
   ============================================================ */

/**
 * @brief Callback HAL de fin de transfert sur la 1ère moitié RX DMA.
 *
 * @param hsai Instance SAI ayant déclenché l'interruption.
 *
 * Contexte d'appel:
 * - IRQ DMA RX.
 *
 * Effets de bord:
 * - Traite la moitié 0 du buffer.
 * - Notifie le scheduler applicatif en nombre de frames traitées.
 */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if(hsai == sai_rx)
    {
        process_half(0);

        /* Tick scheduler en frames audio. */
        engine_tasklet_notify_frames(AUDIO_FRAMES_PER_HALF);
    }
}

/**
 * @brief Callback HAL de fin de transfert sur la 2ème moitié RX DMA.
 *
 * @param hsai Instance SAI ayant déclenché l'interruption.
 *
 * Contexte d'appel:
 * - IRQ DMA RX.
 *
 * Effets de bord:
 * - Traite la moitié 1 du buffer.
 * - Notifie le scheduler applicatif en nombre de frames traitées.
 */
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if(hsai == sai_rx)
    {
        process_half(1);

        /* Tick scheduler en frames audio. */
        engine_tasklet_notify_frames(AUDIO_FRAMES_PER_HALF);
    }
}
