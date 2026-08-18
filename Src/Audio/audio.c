/**
 * @file audio.c
 * @brief Couche matérielle audio STM32H743 (SAI stereo 24-bit + DMA double buffer).
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
#include "Core/live_clock.h"
#include "Core/live_parameter_audio_queue.h"
#include "Core/live_parameter_audio_runtime.h"
#include "memory_layout.h"
#include "cache_maintenance.h"
#include "Audio/metronome_runtime.h"
#include "Audio/control_audio_queue.h"
#include "Audio/audio_fx_runtime.h"
#include "Audio/audio_waveform_capture.h"
#include "Audio/audio_note_admission.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Board/board_audio.h"
#include "Board/board_audio_format.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Sampler/sample_stream_time.h"
#include "Core/brick6_stream_service_task.h"

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
 * - RX reste dans la section cacheable existante et est invalidé avant lecture CPU
 * - TX reste dans la section D2 cacheable et est nettoyé avant consommation DMA
 * - aucune copie ni changement de format n'est introduit dans le chemin IRQ
 */
static AUDIO_DMA_BUFFER_CACHEABLE int32_t rx_buffer[AUDIO_BUFFER_WORDS];
static AUDIO_DMA_BUFFER_CACHEABLE int32_t tx_buffer[AUDIO_BUFFER_WORDS];

/* ============================================================
   SAI HANDLES
   ============================================================ */

static volatile audio_init_state_t g_audio_init_state = AUDIO_INIT_NOT_STARTED;
static uint64_t g_audio_sample_clock;
/* ============================================================
   INTERNAL PROCESSING
   Hardware layer only: calls float engine
   ============================================================ */

static void audio_apply_control_events_at_sample(uint64_t sample_time)
{
    control_audio_event_t event;
    while ((control_audio_queue_audio_peek(&event) != 0U)
            && (event.due_sample <= sample_time))
    {
        (void)control_audio_queue_audio_pop();
        if (event.kind <= (uint8_t)CONTROL_AUDIO_EVENT_NOTE_ON)
        {
            (void)audio_note_admission_apply(&event);
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_BOUNDARY_EDGE)
        {
            brick6_looper_runtime_on_boundary_edge(event.entity_id,
                                                   event.due_sample);
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_METRONOME_CLICK)
        {
            metronome_runtime_trigger_at(
                0U, (event.flags != 0U) ? METRONOME_CLICK_ACCENT
                                        : METRONOME_CLICK_NORMAL);
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_CLOSE_ENTITY)
        {
            audio_note_admission_close_entity(event.entity_id);
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_CLOSE_ALL)
        {
            audio_note_admission_close_all();
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_BINDING_INTENT)
        {
            audio_note_admission_close_entity(event.entity_id);
            audio_note_engine_adapter_install_intent(&event);
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_MOD_MATRIX_SNAPSHOT)
        {
            audio_mod_matrix_apply_snapshot(&event);
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_LOOPER_TRANSPORT_START)
        {
            brick6_looper_runtime_on_transport_start();
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_LOOPER_TRANSPORT_STOP)
        {
            brick6_looper_runtime_on_transport_stop();
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_LOOPER_RECORD_STOP)
        {
            brick6_looper_runtime_arm_record_stop(event.due_sample);
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_LOOPER_PREPARE_REPLACE)
        {
            brick6_looper_runtime_prepare_replace(event.entity_id);
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_LOOPER_RECORD_START)
        {
            brick6_looper_runtime_arm_live_record_start(
                event.entity_id, event.note, event.source_generation,
                event.velocity, event.due_sample);
        }
        else if (event.kind == (uint8_t)CONTROL_AUDIO_EVENT_MULTI_STOP)
        {
            brick6_sampler_runtime_stop_multi_instrument(event.param_id);
        }
    }
}
static void process_audio_segment(int32_t *rx, int32_t *tx, uint64_t sample_time, uint32_t frames)
{
    uint32_t cursor = 0U;
    while (cursor < frames)
    {
        const uint64_t now = sample_time + cursor;
        audio_apply_control_events_at_sample(now);
        (void)live_parameter_audio_queue_consume_due(now);
        (void)live_parameter_audio_runtime_apply_due(now);
        const uint16_t remaining = (uint16_t)(frames - cursor);
        uint16_t span = remaining;
        if (span == 0U)
        {
            span = 1U;
        }
        live_parameter_audio_runtime_process(now, span);
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
static void audio_process_event_segment(int32_t *rx,
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
ITCM_AUDIT_32_TEXT static void process_half(uint32_t half_index)
{
    const uint32_t offset =
        half_index * AUDIO_FRAMES_PER_HALF * AUDIO_WORDS_PER_FRAME;
    const size_t half_bytes = (size_t)AUDIO_FRAMES_PER_HALF
                            * (size_t)AUDIO_WORDS_PER_FRAME
                            * sizeof(int32_t);

    int32_t *rx = &rx_buffer[offset];
    int32_t *tx = &tx_buffer[offset];

    if (half_index > 1U)
    {
        return;
    }

    /* RX DMA -> CPU: invalider avant lecture CPU du half-buffer traite. */
    dcache_invalidate_by_addr_aligned(rx, half_bytes);
    uint32_t half_cursor = 0U;
    while (half_cursor < AUDIO_FRAMES_PER_HALF)
    {
        const uint16_t remaining = (uint16_t)(AUDIO_FRAMES_PER_HALF - half_cursor);
        uint16_t block_frames = remaining;
        block_frames = control_audio_queue_audio_frames_until_due(
            g_audio_sample_clock, block_frames);
        block_frames = live_parameter_audio_queue_frames_until_deadline(
            g_audio_sample_clock, block_frames);
        if (block_frames == 0U)
        {
            block_frames = 1U;
        }

        const uint64_t block_start_sample = g_audio_sample_clock;
        g_audio_sample_clock += (uint64_t)block_frames;
        audio_process_event_segment(rx,
                                        tx,
                                        half_cursor,
                                        block_start_sample,
                                        block_frames);
        half_cursor += block_frames;
    }

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
 * @brief Point d'entrée audio_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_init.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_init(void)
{
    live_clock_init();
    control_audio_queue_init();
    audio_note_admission_init();
    audio_note_engine_adapter_init();
    audio_fx_runtime_init();
    audio_waveform_capture_init();
    board_audio_init();
    g_audio_init_state = AUDIO_INIT_NOT_STARTED;
    g_audio_sample_clock = 0U;

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
uint8_t audio_start(void)
{
    g_audio_init_state = AUDIO_INIT_CODEC;
    if (board_audio_start_stream(rx_buffer, tx_buffer, AUDIO_BUFFER_WORDS,
                                 &g_audio_init_state) == 0U)
    {
        g_audio_init_state = AUDIO_INIT_ERROR;
        return 0U;
    }
    return (g_audio_init_state == AUDIO_INIT_READY) ? 1U : 0U;
}

void audio_stop(void)
{
    board_audio_stop_stream();
    g_audio_init_state = AUDIO_INIT_NOT_STARTED;
}

audio_init_state_t audio_get_init_state(void)
{
    return g_audio_init_state;
}

board_audio_boot_error_t audio_get_boot_error(void)
{
    board_audio_boot_diag_t diag;
    board_audio_get_boot_diag(&diag);
    return diag.last_error;
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
        audio_note_engine_adapter_audio_publish_snapshot();
        live_clock_audio_publish_anchor(
            g_audio_sample_clock);
        cpu_load_irq_begin();

        process_half(0);

        sample_stream_time_advance_from_audio_irq(AUDIO_FRAMES_PER_HALF);
        brick6_stream_service_task_notify_audio_irq();

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
    if ((g_audio_init_state == AUDIO_INIT_READY)
            && (board_audio_is_rx_callback_handle(hsai) != 0U))
    {
        audio_note_engine_adapter_audio_publish_snapshot();
        live_clock_audio_publish_anchor(
            g_audio_sample_clock);
        cpu_load_irq_begin();

        process_half(1);

        sample_stream_time_advance_from_audio_irq(AUDIO_FRAMES_PER_HALF);
        brick6_stream_service_task_notify_audio_irq();

        /* Tick scheduler en frames audio. */
        engine_tasklet_notify_frames(AUDIO_FRAMES_PER_HALF);

        cpu_load_irq_end();
    }
}
