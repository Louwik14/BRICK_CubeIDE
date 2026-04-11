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
 * - Taille bloc temps réel: AUDIO_FRAMES_PER_HALF frames (64 ici).
 * - À 48 kHz, budget par half-buffer ≈ 64 / 48000 s = 1.33 ms.
 * - Aucun malloc/printf/HAL bloquant dans le chemin IRQ.
 */

#include "audio.h"
#include "audio_float.h"
#include "engine_tasklet.h"
#include "cpu_load.h"
#include "memory_layout.h"
#include "cache_maintenance.h"
#include "Seq/seq_runtime.h"

#include <string.h>
#include <stdint.h>

/* ============================================================
   CONFIG AUDIO : STM32H743 + CS42448 TDM8
   ============================================================ */

/* TDM8 = 8 slots x 32-bit */
#define AUDIO_TDM_SLOTS          8

/* Frames traitées par interruption half DMA.
   Doit rester cohérent avec AUDIO_BLOCK_SIZE (audio_float.h). */
#define AUDIO_FRAMES_PER_HALF    64

/* Double buffer DMA: [half0 | half1] */
#define AUDIO_FRAMES_TOTAL       (AUDIO_FRAMES_PER_HALF * 2)

/* 1 frame TDM = 8 mots (slots) */
#define AUDIO_WORDS_PER_FRAME    AUDIO_TDM_SLOTS

/* Taille totale des buffers DMA (en int32) */
#define AUDIO_BUFFER_WORDS       (AUDIO_FRAMES_TOTAL * AUDIO_WORDS_PER_FRAME)
#define AUDIO_SEQ_MAX_BLOCK_EVENTS 32U

/* ============================================================
   DMA BUFFERS
   ============================================================ */

/*
 * Buffers ping-pong partagés CPU/DMA:
 * - RX: DMA écrit, CPU lit
 * - TX: CPU écrit, DMA lit
 *
 * Politique de cette passe (test audio uniquement):
 * - RX/TX audio restent en D2 mais en section cacheable
 * - cohérence CPU/DMA assurée par maintenance D-cache explicite en IRQ
 * - les autres buffers DMA critiques conservent la section DMA_BUFFER non-cacheable
 */
static AUDIO_DMA_BUFFER_CACHEABLE int32_t rx_buffer[AUDIO_BUFFER_WORDS];
static AUDIO_DMA_BUFFER_CACHEABLE int32_t tx_buffer[AUDIO_BUFFER_WORDS];

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
/**
 * @brief Point d'entrée process_half.
 *
 * Rôle:
 * - Exécuter le traitement associé à process_half.
 *
 * @param half_index Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void process_half(uint32_t half_index)
{
    const uint32_t offset =
        half_index * AUDIO_FRAMES_PER_HALF * AUDIO_WORDS_PER_FRAME;
    const size_t half_bytes = (size_t)AUDIO_FRAMES_PER_HALF
                            * (size_t)AUDIO_WORDS_PER_FRAME
                            * sizeof(int32_t);

    int32_t *rx = &rx_buffer[offset];
    int32_t *tx = &tx_buffer[offset];

    /* RX DMA -> CPU: invalider avant lecture CPU du half-buffer traité. */
    dcache_invalidate_by_addr_aligned(rx, half_bytes);

    seq_runtime_audio_event_t block_events[AUDIO_SEQ_MAX_BLOCK_EVENTS];
    const uint16_t event_count = seq_runtime_audio_collect_block_events(block_events,
                                                                        AUDIO_SEQ_MAX_BLOCK_EVENTS,
                                                                        AUDIO_FRAMES_PER_HALF);

    uint32_t cursor = 0U;
    uint16_t event_index = 0U;
    while (event_index < event_count)
    {
        uint16_t event_offset = block_events[event_index].sample_offset_in_block;
        if (event_offset > AUDIO_FRAMES_PER_HALF)
        {
            event_offset = AUDIO_FRAMES_PER_HALF;
        }

        if ((uint32_t)event_offset > cursor)
        {
            const uint32_t segment_frames = (uint32_t)event_offset - cursor;
            audio_process_block_int32(&rx[cursor * AUDIO_WORDS_PER_FRAME],
                                      &tx[cursor * AUDIO_WORDS_PER_FRAME],
                                      segment_frames);
            cursor = (uint32_t)event_offset;
        }

        while ((event_index < event_count)
               && (block_events[event_index].sample_offset_in_block == event_offset))
        {
            seq_runtime_audio_apply_event(&block_events[event_index]);
            event_index++;
        }
    }

    if (cursor < AUDIO_FRAMES_PER_HALF)
    {
        audio_process_block_int32(&rx[cursor * AUDIO_WORDS_PER_FRAME],
                                  &tx[cursor * AUDIO_WORDS_PER_FRAME],
                                  AUDIO_FRAMES_PER_HALF - cursor);
    }

    /* CPU -> TX DMA: clean après écriture CPU et avant lecture DMA. */
    dcache_clean_by_addr_aligned(tx, half_bytes);
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
/**
 * @brief Point d'entrée audio_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_init.
 *
 * @param hsai_tx Paramètre d'entrée de l'API.
 * @param hsai_rx Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_init(SAI_HandleTypeDef *hsai_tx,
                SAI_HandleTypeDef *hsai_rx)
{
    sai_tx = hsai_tx;
    sai_rx = hsai_rx;

    memset(rx_buffer, 0, sizeof(rx_buffer));
    memset(tx_buffer, 0, sizeof(tx_buffer));

    /* Le TX peut être consommé par DMA avant le 1er callback: pousser les zéros en RAM. */
    dcache_clean_by_addr_aligned(tx_buffer, sizeof(tx_buffer));

    /* Init mesure charge CPU audio (utilisée ensuite en IRQ). */
    cpu_load_init();
}

/**
 * @brief Démarre les flux audio SAI RX/TX en DMA circulaire.
 *
 * Contexte d'appel:
 * - Main loop (après audio_init et callback DSP installé).
 */
/**
 * @brief Point d'entrée audio_start.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_start.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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
/**
 * @brief Point d'entrée HAL_SAI_RxHalfCpltCallback.
 *
 * Rôle:
 * - Exécuter le traitement associé à HAL_SAI_RxHalfCpltCallback.
 *
 * @param hsai Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if(hsai == sai_rx)
    {
        cpu_load_irq_begin();

        process_half(0);

        /* Tick scheduler en frames audio. */
        engine_tasklet_notify_frames(AUDIO_FRAMES_PER_HALF);

        cpu_load_irq_end();
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
/**
 * @brief Point d'entrée HAL_SAI_RxCpltCallback.
 *
 * Rôle:
 * - Exécuter le traitement associé à HAL_SAI_RxCpltCallback.
 *
 * @param hsai Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if(hsai == sai_rx)
    {
        cpu_load_irq_begin();

        process_half(1);

        /* Tick scheduler en frames audio. */
        engine_tasklet_notify_frames(AUDIO_FRAMES_PER_HALF);

        cpu_load_irq_end();
    }
}
