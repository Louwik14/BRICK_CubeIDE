/**
 * @file audio.c
 * @brief Couche matérielle audio STM32H743 (2x SAI TDM4 + DMA double buffer).
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
 * - Taille bloc temps réel: AUDIO_FRAMES_PER_HALF frames (64 ici).
 * - À 48 kHz, budget par half-buffer ≈ 64 / 48000 s = 1.33 ms.
 * - Aucun malloc/printf/HAL bloquant dans le chemin IRQ.
 */

#include "audio.h"
#include "audio_float.h"
#include "engine_tasklet.h"
#include "cpu_load.h"
#include "memory_layout.h"

#include <string.h>
#include <stdint.h>

/* ============================================================
   CONFIG AUDIO : STM32H743 + 2x AK4619 (2x TDM4)
   ============================================================ */

/* TDM4 = 4 slots x 32-bit par SAI */
#define AUDIO_TDM_SLOTS          4

/* Frames traitées par interruption half DMA.
   Doit rester cohérent avec AUDIO_BLOCK_SIZE (audio_float.h). */
#define AUDIO_FRAMES_PER_HALF    64

/* Double buffer DMA: [half0 | half1] */
#define AUDIO_FRAMES_TOTAL       (AUDIO_FRAMES_PER_HALF * 2)

/* 1 frame TDM par SAI = 4 mots (slots) */
#define AUDIO_WORDS_PER_FRAME    AUDIO_TDM_SLOTS

/* Taille totale des buffers DMA (en int32) par SAI */
#define AUDIO_BUFFER_WORDS       (AUDIO_FRAMES_TOTAL * AUDIO_WORDS_PER_FRAME)

/* ============================================================
   DMA BUFFERS
   ============================================================ */

/* Buffers statiques ping-pong RX/TX (aucune allocation dynamique). */
static DMA_BUFFER int32_t rx1_buffer[AUDIO_BUFFER_WORDS];
static DMA_BUFFER int32_t tx1_buffer[AUDIO_BUFFER_WORDS];
static DMA_BUFFER int32_t rx2_buffer[AUDIO_BUFFER_WORDS];
static DMA_BUFFER int32_t tx2_buffer[AUDIO_BUFFER_WORDS];

/* ============================================================
   SAI HANDLES
   ============================================================ */

static SAI_HandleTypeDef *sai1_tx = NULL;
static SAI_HandleTypeDef *sai1_rx = NULL;
static SAI_HandleTypeDef *sai2_tx = NULL;
static SAI_HandleTypeDef *sai2_rx = NULL;

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

    int32_t *rx1 = &rx1_buffer[offset];
    int32_t *tx1 = &tx1_buffer[offset];
    int32_t *rx2 = &rx2_buffer[offset];
    int32_t *tx2 = &tx2_buffer[offset];

    /* Frontière moteur float (un bloc fixe par IRQ, déclenché une seule fois). */
    audio_process_block_int32(rx1, tx1, rx2, tx2, AUDIO_FRAMES_PER_HALF);
}

/* ============================================================
   API
   ============================================================ */

/**
 * @brief Initialise la couche audio matérielle et les buffers DMA.
 *
 * @param hsai_tx Handle SAI TX.
 * @param hsai_rx Handle SAI RX.
 *
 * Contexte d'appel:
 * - Main loop (boot).
 */
void audio_init(SAI_HandleTypeDef *hsai1_tx,
                SAI_HandleTypeDef *hsai1_rx,
                SAI_HandleTypeDef *hsai2_tx,
                SAI_HandleTypeDef *hsai2_rx)
{
    sai1_tx = hsai1_tx;
    sai1_rx = hsai1_rx;
    sai2_tx = hsai2_tx;
    sai2_rx = hsai2_rx;

    memset(rx1_buffer, 0, sizeof(rx1_buffer));
    memset(tx1_buffer, 0, sizeof(tx1_buffer));
    memset(rx2_buffer, 0, sizeof(rx2_buffer));
    memset(tx2_buffer, 0, sizeof(tx2_buffer));

    /* Init mesure charge CPU audio (utilisée ensuite en IRQ). */
    cpu_load_init();
}

/**
 * @brief Démarre les flux audio SAI RX/TX en DMA circulaire.
 *
 * Contexte d'appel:
 * - Main loop (après audio_init et callback DSP installé).
 */
void audio_start(void)
{
    if(!sai1_tx || !sai1_rx || !sai2_tx || !sai2_rx)
        return;

    /*
     * Démarrage recommandé avec SAI2 maître d'horloge :
     * - armer d'abord le lien esclave SAI1
     * - armer ensuite le lien maître SAI2
     */
    HAL_SAI_Receive_DMA(sai1_rx,
                        (uint8_t *)rx1_buffer,
                        AUDIO_BUFFER_WORDS);
    HAL_SAI_Transmit_DMA(sai1_tx,
                         (uint8_t *)tx1_buffer,
                         AUDIO_BUFFER_WORDS);

    HAL_SAI_Receive_DMA(sai2_rx,
                        (uint8_t *)rx2_buffer,
                        AUDIO_BUFFER_WORDS);
    HAL_SAI_Transmit_DMA(sai2_tx,
                         (uint8_t *)tx2_buffer,
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
    if(hsai == sai1_rx)
    {
        /* SAI1 esclave: IRQ ignorée pour éviter un double traitement DSP. */
        return;
    }

    if(hsai == sai2_rx)
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
    if(hsai == sai1_rx)
    {
        /* SAI1 esclave: IRQ ignorée pour éviter un double traitement DSP. */
        return;
    }

    if(hsai == sai2_rx)
    {
        process_half(1);

        /* Tick scheduler en frames audio. */
        engine_tasklet_notify_frames(AUDIO_FRAMES_PER_HALF);
    }
}
