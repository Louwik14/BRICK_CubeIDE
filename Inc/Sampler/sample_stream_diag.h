#pragma once

#include <stdint.h>
#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_page_cache_contract.h"
#include "Sampler/sample_stream_limits.h"

/* Temporary GDB instrumentation. All times are wrapping DWT cycle counts. */
typedef enum {
    STREAM_DIAG_NEED = 1, STREAM_DIAG_RESERVED, STREAM_DIAG_LOADING,
    STREAM_DIAG_DMA_START, STREAM_DIAG_DMA_COMPLETE, STREAM_DIAG_DMA_ERROR,
    STREAM_DIAG_READY, STREAM_DIAG_FAILED, STREAM_DIAG_RESERVE_FAIL,
    STREAM_DIAG_DELAYED, STREAM_DIAG_QUEUE_FULL, STREAM_DIAG_AUDIO_MISS,
    STREAM_DIAG_AUDIO_NOT_READY, STREAM_DIAG_AUDIO_BAD_KEY,
    STREAM_DIAG_AUDIO_BAD_EPOCH, STREAM_DIAG_AUDIO_UNDERRUN
} sample_stream_diag_event_t;

typedef struct {
    uint32_t sequence, cycles, event, reader_slot;
    sample_audio_key_t key;
    uint32_t page, frame, page_state, extra;
    uint32_t active_readers, scheduler_pending, sd_pending, sd_state;
} sample_stream_diag_trace_t;

typedef struct {
    sample_audio_key_t key;
    uint32_t active, slot, frame, current_page, next_page;
    uint32_t pages_used, refills, misses, not_ready, underruns;
    uint32_t min_ready_distance;
    uint32_t need_page, t_need, t_reserved, t_dma_start;
    uint32_t t_dma_complete, t_ready, t_first_use;
    uint32_t registration_epoch, page_generation;
    uint32_t dma_owner;
    uint32_t prev_need_page, prev_t_need, prev_t_reserved, prev_t_dma_start;
    uint32_t prev_t_dma_complete, prev_t_ready, prev_t_first_use, prev_dma_owner;
} sample_stream_diag_reader_t;

typedef struct {
    uint32_t valid, cycles, event, reader_slot;
    sample_audio_key_t key;
    uint32_t page, frame, page_state, prev_state, next_state;
    uint32_t active_readers, scheduler_pending, sd_pending, sd_state;
    uint32_t sd_owner, sd_operation, sd_fault, sd_irq_error;
    sample_stream_diag_reader_t refill;
    uint32_t reserved, loading, ready, failed;
    uint32_t dma_starts, dma_completions, dma_errors, busy, queue_full;
    uint32_t t_need, t_reserved, t_dma_start, t_dma_complete, t_ready, t_first_use;
} sample_stream_diag_snapshot_t;

typedef struct {
    uint32_t magic, version, cycle_hz, frozen, trace_next, trace_count;
    uint32_t active_readers, requests, reserved, loading, ready, failed;
    uint32_t dma_starts, dma_completions, dma_errors, busy, queue_full;
    uint32_t reserve_fail, delayed, audio_miss, audio_not_ready;
    uint32_t audio_bad_key, audio_bad_epoch, underruns;
    uint32_t max_need_dma, max_dma_complete, max_complete_ready, max_need_ready;
    uint32_t scheduler_owner, scheduler_pending, sd_pending, sd_state;
    sample_stream_diag_reader_t reader[SAMPLE_STREAM_TARGET_MAX_VOICES];
    sample_stream_diag_snapshot_t first;
    sample_stream_diag_trace_t trace[64];
} sample_stream_diag_t;

extern volatile sample_stream_diag_t g_sample_stream_diag;
void sample_stream_diag_init(void);
void sample_stream_diag_bind(uint8_t slot, sample_audio_key_t key, uint32_t epoch, uint32_t frame);
void sample_stream_diag_unbind(uint8_t slot);
void sample_stream_diag_need(uint8_t slot, sample_audio_key_t key, uint32_t frame, uint32_t page);
void sample_stream_diag_use(uint8_t slot, uint32_t frame, uint32_t page, uint32_t ready_distance, uint32_t generation);
void sample_stream_diag_fault(uint8_t slot, sample_audio_key_t key, uint32_t frame,
                              uint32_t page, sample_page_state_t state, uint32_t event);
void sample_stream_diag_page(sample_audio_key_t key, uint32_t page, sample_page_state_t old_state,
                             sample_page_state_t new_state, uint32_t generation);
void sample_stream_diag_scheduler(uint32_t event, uint8_t slot, sample_audio_key_t key,
                                  uint32_t page, uint32_t extra);
void sample_stream_diag_dma(uint32_t event, uint32_t owner, uint32_t lba, uint32_t extra);
void sample_stream_diag_dma_owner(uint32_t owner, sample_audio_key_t key, uint32_t page);
