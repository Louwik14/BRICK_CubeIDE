/**
 * @file audio.c
 * @brief Couche matérielle audio STM32H743 (SAI stereo 24-bit + DMA double buffer).
 *
 * Rôle du module:
 * - Configurer la mécanique de streaming RX/TX DMA en ping-pong (half/full).
 * - Déclencher le traitement bloc du moteur float via audio_process_block_int32().
 *
 * Architecture:
 * - Appelé par: brick6_app_init.c (bootstrap plateforme pré-scheduler).
 * - Appelle: audio_process_block_int32() (audio_float.c),
 *            sans reveil direct du domaine CONTROL.
 *
 * Contraintes temps réel:
 * - Le traitement audio est exécuté dans les callbacks IRQ DMA RX.
 * - Taille bloc temps réel: AUDIO_FRAMES_PER_HALF frames (64 ici).
 * - À 48 kHz, budget par half-buffer ≈ 64 / 48000 s = 1.33 ms.
 * - Aucun malloc/printf/HAL bloquant dans le chemin IRQ.
 */

#include "audio.h"
#include "audio_float.h"
#include "Platform/cpu_load.h"
#include "Platform/memory_layout.h"
#include "Platform/cache_maintenance.h"
#include "Audio/metronome_runtime.h"
#include "IPC/control_audio_fifo_audio.h"
#include "Audio/audio_command_executor.h"
#include "Audio/audio_fx_runtime.h"
#include "Audio/audio_transport_runtime.h"
#include "Audio/audio_waveform_capture_audio.h"
#include "Audio/synth_waveform_audio.h"
#include "Audio/audio_boot_diagnostic_producer.h"
#include "Audio/audio_recorder_capture_audio.h"
#include "Audio/sd_preview_audio.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Mod/mod_lfo_v1_audio.h"
#include "Mod/mod_env3.h"
#include "Board/board_audio.h"
#include "Board/board_audio_format.h"
#include "Audio/brick6_looper_runtime.h"
#include "Audio/Engines/Sampler/brick6_sampler_runtime.h"
#include "Audio/Engines/wavetable_engine.h"

#include <string.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

/* ============================================================
   CONFIG AUDIO : contrat codec/SAI stereo commun aux variantes
   ============================================================ */

/* Deux slots stéréo de 32 bits transportant des samples 24 bits. */
#define AUDIO_TDM_SLOTS          BOARD_AUDIO_TDM_SLOTS

/* Frames traitées par interruption half DMA.
   Doit rester cohérent avec AUDIO_BLOCK_SIZE (audio_float.h). */
#define AUDIO_FRAMES_PER_HALF    BOARD_AUDIO_CONTRACT_FRAMES_PER_HALF

/* Double buffer DMA: [half0 | half1] */
#define AUDIO_FRAMES_TOTAL       BOARD_AUDIO_CONTRACT_FRAMES_TOTAL

/* 1 frame audio = un mot par canal stéréo. */
#define AUDIO_WORDS_PER_FRAME    AUDIO_TDM_SLOTS

/* Taille totale des buffers DMA (en int32) */
#define AUDIO_BUFFER_WORDS       (AUDIO_FRAMES_TOTAL * AUDIO_WORDS_PER_FRAME)

/* ============================================================
   DMA BUFFERS
   ============================================================ */

/*
 * Buffers ping-pong partagés CPU/DMA:
 * - RX: DMA écrit, CPU lit
 * - TX: CPU écrit, DMA lit
 *
 * Politique des buffers DMA:
 * - RX/TX restent dans le contrat D2 non-cacheable de l'image matérielle GOOD
 * - aucune maintenance D-Cache n'est nécessaire pour ces buffers
 * - aucune copie ni changement de format n'est introduit dans le chemin IRQ
 */
static AUDIO_DMA_BUFFER_NONCACHEABLE int32_t rx_buffer[AUDIO_BUFFER_WORDS];
static AUDIO_DMA_BUFFER_NONCACHEABLE int32_t tx_buffer[AUDIO_BUFFER_WORDS];

/* ============================================================
   SAI HANDLES
   ============================================================ */

static volatile audio_init_state_t g_audio_init_state = AUDIO_INIT_NOT_STARTED;
static uint64_t g_audio_sample_clock;
static uint8_t g_audio_sample_clock_valid;

static uint64_t audio_tim5_to_sample_clock(uint32_t tick)
{
    uint32_t tim_kernel_hz = HAL_RCC_GetPCLK1Freq();
    if ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) != RCC_APB1_DIV1)
    {
        tim_kernel_hz *= 2U;
    }
    const uint32_t tim5_hz = tim_kernel_hz / ((uint32_t)TIM5->PSC + 1U);
    if (tim5_hz == 0U) return 0U;
    return (((uint64_t)tick * BOARD_AUDIO_SAMPLE_RATE_HZ)
            + (tim5_hz / 2U)) / tim5_hz;
}

static void audio_sample_clock_init_on_first_callback(void)
{
    if (g_audio_sample_clock_valid != 0U) return;
    const uint64_t callback_sample = audio_tim5_to_sample_clock(TIM5->CNT);
    g_audio_sample_clock = (callback_sample >= AUDIO_FRAMES_PER_HALF)
        ? callback_sample - AUDIO_FRAMES_PER_HALF : 0U;
    g_audio_sample_clock_valid = 1U;
}
/* ============================================================
   INTERNAL PROCESSING
   Hardware layer only: calls float engine
   ============================================================ */

static ITCM_TEXT void process_audio_segment(int32_t *rx, int32_t *tx, uint64_t sample_time, uint32_t frames)
{
    uint32_t cursor = 0U;
    while (cursor < frames)
    {
        const uint16_t remaining = (uint16_t)(frames - cursor);
        uint16_t span = remaining;
        if (span == 0U)
        {
            span = 1U;
        }
        audio_process_block_int32(&rx[cursor * AUDIO_WORDS_PER_FRAME],
                                  &tx[cursor * AUDIO_WORDS_PER_FRAME], span);
        cursor += span;
    }
}

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
static ITCM_TEXT void audio_process_event_segment(int32_t *rx,
                                            int32_t *tx,
                                            uint32_t half_cursor,
                                            uint64_t block_start_sample,
                                            uint16_t block_frames)
{
    brick6_looper_runtime_on_scheduled_start(block_start_sample);
    process_audio_segment(&rx[half_cursor * AUDIO_WORDS_PER_FRAME],
                          &tx[half_cursor * AUDIO_WORDS_PER_FRAME],
                          block_start_sample,
                          block_frames);
}
static ITCM_TEXT void audio_process_half_common_hot(int32_t *rx, int32_t *tx)
{
    const uint32_t command_head_limit =
        control_audio_fifo_audio_head_snapshot();
    uint32_t half_cursor = 0U;
    while (half_cursor < AUDIO_FRAMES_PER_HALF)
    {
        (void)audio_command_executor_apply_due(g_audio_sample_clock,
                                                command_head_limit);
        const uint16_t remaining = (uint16_t)(AUDIO_FRAMES_PER_HALF - half_cursor);
        const uint16_t block_frames = control_audio_fifo_audio_frames_until_due(
            g_audio_sample_clock, remaining, command_head_limit);
        if (block_frames == 0U) continue;

        const uint64_t block_start_sample = g_audio_sample_clock;
        g_audio_sample_clock += (uint64_t)block_frames;
        audio_process_event_segment(rx, tx, half_cursor,
                                    block_start_sample, block_frames);
        half_cursor += block_frames;
    }
}

static void process_half(uint32_t half_index)
{
    const uint32_t offset =
        half_index * AUDIO_FRAMES_PER_HALF * AUDIO_WORDS_PER_FRAME;

    int32_t *rx = &rx_buffer[offset];
    int32_t *tx = &tx_buffer[offset];

    if (half_index > 1U)
    {
        return;
    }

    /* RX DMA -> CPU: la zone est non-cacheable par contrat MPU. */
#if AUDIO_DMA_BUFFER_IS_CACHEABLE
    dcache_invalidate_by_addr_aligned(rx, half_bytes);
#endif
    audio_process_half_common_hot(rx, tx);

#if AUDIO_DMA_BUFFER_IS_CACHEABLE
    dcache_clean_by_addr_aligned(tx, half_bytes);
#endif

}
/* ============================================================
   API
   ============================================================ */

/**
 * @brief Initialise la couche audio matérielle et les buffers DMA.
 *
 * Contexte d'appel:
 * - Main loop (boot).
 */
/**
 * @brief Point d'entrée AUDIO propriétaire pour les programmes et l'I/O de boot.
 *
 * Rôle:
 * - Initialiser dans l'ordre historique les services AUDIO, les programmes et l'I/O.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_boot_init_binding_io(void)
{
    audio_command_executor_init();
    audio_note_engine_adapter_init();
    audio_mod_matrix_init();
    audio_fx_runtime_init();
    audio_transport_runtime_init();
    audio_recorder_capture_audio_init();
    sd_preview_audio_init();
    audio_waveform_capture_init();
    synth_waveform_init();
    audio_boot_diag_producer_init();
    board_audio_init();
    g_audio_init_state = AUDIO_INIT_NOT_STARTED;
    g_audio_sample_clock = 0U;
    g_audio_sample_clock_valid = 0U;
    audio_boot_diag_producer_publish_state(AUDIO_INIT_NOT_STARTED, BOARD_AUDIO_BOOT_OK);

    memset(rx_buffer, 0, sizeof(rx_buffer));
    memset(tx_buffer, 0, sizeof(tx_buffer));
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Le TX peut être consommé par DMA avant le 1er callback: pousser les zéros en RAM. */
#if AUDIO_DMA_BUFFER_IS_CACHEABLE
    dcache_clean_by_addr_aligned(tx_buffer, sizeof(tx_buffer));
#endif

    /* Init mesure charge CPU audio (utilisée ensuite en IRQ). */
    cpu_load_init();
}

/**
 * @brief Démarre les flux audio SAI RX/TX en DMA circulaire.
 *
 * Contexte d'appel:
 * - Bootstrap plateforme pré-scheduler (après audio_domain_init et callback DSP installé).
 */
/**
 * @brief Point d'entrée audio_start.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_start.
 *
 *
 * Contexte d'appel:
 * - bootstrap plateforme pré-scheduler uniquement.
 */
uint8_t audio_start(void)
{
    g_audio_init_state = AUDIO_INIT_CODEC;
    if (board_audio_start_stream(rx_buffer, tx_buffer, AUDIO_BUFFER_WORDS,
                                 &g_audio_init_state) == 0U)
    {
        g_audio_init_state = AUDIO_INIT_ERROR;
        board_audio_boot_diag_t diag;
        board_audio_get_boot_diag(&diag);
        audio_boot_diag_producer_publish_state(AUDIO_INIT_ERROR, diag.last_error);
        return 0U;
    }
    audio_boot_diag_producer_publish_state(g_audio_init_state, BOARD_AUDIO_BOOT_OK);
    return (g_audio_init_state == AUDIO_INIT_READY) ? 1U : 0U;
}

void audio_stop(void)
{
    board_audio_stop_stream();
    g_audio_init_state = AUDIO_INIT_NOT_STARTED;
    audio_boot_diag_producer_publish_state(AUDIO_INIT_NOT_STARTED, BOARD_AUDIO_BOOT_OK);
}

uint64_t audio_sample_clock_now(void)
{
    return g_audio_sample_clock;
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
    if ((g_audio_init_state == AUDIO_INIT_READY)
            && (board_audio_is_rx_callback_handle(hsai) != 0U))
    {
        audio_sample_clock_init_on_first_callback();
        cpu_load_irq_begin();

        process_half(0);

        cpu_load_irq_end();
        audio_boot_diag_producer_publish_cpu((uint8_t)cpu_load_is_valid(),
                                             cpu_load_get_avg_permille());
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
    if ((g_audio_init_state == AUDIO_INIT_READY)
            && (board_audio_is_rx_callback_handle(hsai) != 0U))
    {
        audio_sample_clock_init_on_first_callback();
        cpu_load_irq_begin();

        process_half(1);

        cpu_load_irq_end();
        audio_boot_diag_producer_publish_cpu((uint8_t)cpu_load_is_valid(),
                                             cpu_load_get_avg_permille());
    }
}
