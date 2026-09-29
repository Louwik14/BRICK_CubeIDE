#pragma once

#include <stdint.h>
#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_page_cache_contract.h"
#include "Sampler/sample_page_lease.h"
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

/* Two lease generations per physical reader: the current lookahead and the
 * page which has just become current at an audio page boundary. */
typedef struct {
    uint32_t watch_page, audio_page, lease_slot, event, reason;
    sample_audio_key_t key, storage_key;
    uint32_t audio_cycles, audio_seq, audio_result;
    uint32_t audio_r0_first, audio_r0_count, audio_r1_first, audio_r1_count;
    uint32_t storage_cycles, storage_seq, storage_ok, storage_contains;
    uint32_t storage_r0_first, storage_r0_count, storage_r1_first, storage_r1_count;
    uint32_t examined_page, examined_state, candidate_cycles;
    uint32_t reserve_cycles, reserve_result;
    uint32_t storage_reads, storage_rejects, candidate_found, candidate_none;
    uint32_t pending_seen, reserve_attempts;
    uint32_t storage_contains_reads, storage_missing_reads;
    uint32_t fault_cycles, fault_seq;
    sample_audio_key_t fault_key;
    uint32_t fault_r0_first, fault_r0_count, fault_r1_first, fault_r1_count;
} sample_stream_diag_boundary_t;

typedef enum {
    STREAM_BOUNDARY_AUDIO_PUBLISH = 1, STREAM_BOUNDARY_STORAGE_READ,
    STREAM_BOUNDARY_READ_REJECT, STREAM_BOUNDARY_READY_SKIP,
    STREAM_BOUNDARY_LOADING_BLOCK, STREAM_BOUNDARY_PENDING_ONLY,
    STREAM_BOUNDARY_CANDIDATE_FOUND, STREAM_BOUNDARY_CANDIDATE_NONE,
    STREAM_BOUNDARY_RESERVE_ATTEMPT, STREAM_BOUNDARY_RESERVE_FAILED,
    STREAM_BOUNDARY_RESERVED, STREAM_BOUNDARY_OTHER_CANDIDATE,
    STREAM_BOUNDARY_NO_WORK, STREAM_BOUNDARY_EARLIER_LOADING
} sample_stream_diag_boundary_event_t;

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
    sample_stream_diag_boundary_t boundary[SAMPLE_STREAM_TARGET_MAX_VOICES][2];
    uint32_t gate_polls, gate_pending, service_calls;
    uint32_t gate_deferred_load, gate_acquire_fail;
    uint32_t first_gate_polls, first_service_calls;
    uint32_t first_gate_pending, first_gate_deferred_load, first_gate_acquire_fail;
    uint32_t boundary_slot_mask_lo, boundary_slot_mask_hi;
    uint32_t first_gate_owner;
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
void sample_stream_diag_lease_audio(uint8_t reader, uint8_t lease_slot,
    sample_audio_key_t key, uint32_t audio_page, uint32_t watch_page,
    const sample_page_lease_range_t ranges[2], uint32_t result, uint32_t seq);
void sample_stream_diag_lease_storage(uint8_t lease_slot, uint32_t ok,
    uint32_t seq, const sample_page_lease_t *lease);
void sample_stream_diag_boundary_candidate(uint8_t lease_slot,
    sample_audio_key_t key, uint32_t page, uint32_t state, uint32_t event);
