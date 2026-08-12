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
#include "Board/board_audio.h"
#include "Board/board_audio_format.h"
#include "Core/brick6_looper_runtime.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_runtime_exec.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "NoteFx/note_fx_pipeline.h"
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
#define AUDIO_SEQ_MAX_BLOCK_EVENTS 128U

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

/* ============================================================
   INTERNAL PROCESSING
   Hardware layer only: calls float engine
   ============================================================ */

static void audio_apply_seq_event_at_sample(const seq_runtime_audio_event_t *event,
                                            uint64_t event_sample_time)
{
    if (event == NULL)
    {
        return;
    }

    if (event->type == SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE)
    {
        brick6_looper_runtime_on_boundary_edge(event->track, event_sample_time);
    }
    else if (event->type == SEQ_RUNTIME_AUDIO_EVENT_METRO_CLICK)
    {
        metronome_runtime_trigger_at(0U,
                                     (event->velocity != 0U) ? METRONOME_CLICK_ACCENT
                                                             : METRONOME_CLICK_NORMAL);
    }
    else
    {
        seq_runtime_audio_event_t applied_event = *event;
        /* The application seam owns the actual integer sample after offset conversion. */
        applied_event.sample_offset_in_block = 0U;
        applied_event.sample_abs = event_sample_time;
        seq_runtime_audio_apply_event(&applied_event);
    }
}
static void process_audio_segment(int32_t *rx, int32_t *tx, uint64_t sample_time, uint32_t frames)
{
    uint32_t cursor = 0U;
    while (cursor < frames)
    {
        const uint64_t now = sample_time + cursor;
        note_fx_pipeline_process(now, 1U, seq_runtime_get_samples_per_step_q16());
        (void)live_parameter_audio_queue_consume_due(now);
        (void)live_parameter_audio_runtime_apply_due(now);
        const uint16_t remaining = (uint16_t)(frames - cursor);
        uint16_t span = note_fx_pipeline_frames_until_deadline(now, remaining);
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
static uint16_t audio_seq_collect_frames_until_next_internal_pulse(uint16_t remaining_frames)
{
    const seq_runtime_state_t *const state = seq_runtime_exec_state_const();
    if ((state == NULL)
        || (remaining_frames == 0U)
        || (state->running == 0U)
        || (state->samples_per_step_q16 == 0U)
        || ((seq_runtime_get_clock_source() != SEQ_CLOCK_SRC_INTERNAL)))
    {
        return remaining_frames;
    }

    const uint64_t block_start_sample = seq_runtime_exec_get_audio_timeline_sample();
    const uint64_t block_start_q16 = block_start_sample << 16;
    const uint64_t block_end_q16 = (block_start_sample + (uint64_t)remaining_frames) << 16;
    const uint64_t next_pulse_q16 = state->step_sample_q16 + (uint64_t)state->samples_per_step_q16;

    if ((next_pulse_q16 <= block_start_q16) || (next_pulse_q16 >= block_end_q16))
    {
        return remaining_frames;
    }

    const uint64_t next_pulse_sample = next_pulse_q16 >> 16;
    if (next_pulse_sample <= block_start_sample)
    {
        return remaining_frames;
    }

    const uint64_t frames_until_pulse = next_pulse_sample - block_start_sample;
    if ((frames_until_pulse == 0U) || (frames_until_pulse > (uint64_t)remaining_frames))
    {
        return remaining_frames;
    }

    return (uint16_t)frames_until_pulse;
}

static void audio_process_seq_event_segment(int32_t *rx,
                                            int32_t *tx,
                                            uint32_t half_cursor,
                                            uint64_t block_start_sample,
                                            uint16_t block_frames,
                                            seq_runtime_audio_event_t *events,
                                            uint16_t event_count)
{
    uint32_t cursor = 0U;
    uint16_t event_index = 0U;

    while (cursor < block_frames)
    {
        uint32_t next_event_offset = block_frames;
        if (event_index < event_count)
        {
            next_event_offset = events[event_index].sample_offset_in_block;
            if (next_event_offset > block_frames)
            {
                next_event_offset = block_frames;
            }
        }

        if (next_event_offset > cursor)
        {
            const uint32_t segment_frames = next_event_offset - cursor;
            const uint64_t segment_sample = block_start_sample + (uint64_t)cursor;
            const uint32_t frame_offset = half_cursor + cursor;
            brick6_looper_runtime_on_scheduled_start(segment_sample);
            process_audio_segment(&rx[frame_offset * AUDIO_WORDS_PER_FRAME],
                                  &tx[frame_offset * AUDIO_WORDS_PER_FRAME],
                                  segment_sample,
                                  segment_frames);
            cursor = next_event_offset;
            continue;
        }

        while ((event_index < event_count)
               && (events[event_index].sample_offset_in_block <= cursor))
        {
            audio_apply_seq_event_at_sample(&events[event_index],
                                            block_start_sample + (uint64_t)cursor);
            event_index++;
        }
    }

    while (event_index < event_count)
    {
        audio_apply_seq_event_at_sample(&events[event_index],
                                        block_start_sample + (uint64_t)block_frames);
        event_index++;
    }

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
    note_fx_pipeline_begin_audio_half(AUDIO_FRAMES_PER_HALF);
    seq_play_scheduler_audio_begin_half(SEQ_PLAY_SCHEDULER_HALF_EVENT_QUOTA);

    uint32_t half_cursor = 0U;
    while (half_cursor < AUDIO_FRAMES_PER_HALF)
    {
        const uint16_t remaining = (uint16_t)(AUDIO_FRAMES_PER_HALF - half_cursor);
        uint16_t block_frames = audio_seq_collect_frames_until_next_internal_pulse(remaining);
        block_frames = note_fx_pipeline_frames_until_deadline(
            seq_runtime_exec_get_audio_timeline_sample(), block_frames);
        block_frames = live_parameter_audio_queue_frames_until_deadline(
            seq_runtime_exec_get_audio_timeline_sample(), block_frames);
        if (block_frames == 0U)
        {
            block_frames = 1U;
        }

        seq_runtime_audio_event_t block_events[AUDIO_SEQ_MAX_BLOCK_EVENTS];
        const uint16_t event_count = seq_runtime_audio_collect_block_events(block_events,
                                                                            AUDIO_SEQ_MAX_BLOCK_EVENTS,
                                                                            block_frames);
        const uint64_t block_start_sample =
            seq_runtime_exec_get_audio_timeline_sample() - (uint64_t)block_frames;
        audio_process_seq_event_segment(rx,
                                        tx,
                                        half_cursor,
                                        block_start_sample,
                                        block_frames,
                                        block_events,
                                        event_count);
        half_cursor += block_frames;
    }

    note_fx_pipeline_end_audio_half();
    seq_play_scheduler_audio_end_half();

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
    board_audio_init();
    g_audio_init_state = AUDIO_INIT_NOT_STARTED;

    memset(rx_buffer, 0, sizeof(rx_buffer));
    memset(tx_buffer, 0, sizeof(tx_buffer));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
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
        live_clock_audio_publish_anchor(
            seq_runtime_exec_get_audio_timeline_sample());
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
        live_clock_audio_publish_anchor(
            seq_runtime_exec_get_audio_timeline_sample());
        cpu_load_irq_begin();

        process_half(1);

        sample_stream_time_advance_from_audio_irq(AUDIO_FRAMES_PER_HALF);
        brick6_stream_service_task_notify_audio_irq();

        /* Tick scheduler en frames audio. */
        engine_tasklet_notify_frames(AUDIO_FRAMES_PER_HALF);

        cpu_load_irq_end();
    }
}
