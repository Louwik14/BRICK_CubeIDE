#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_stream_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_END_TO_END_BENCH_MAGIC   UINT32_C(0x45324536)
#define STREAM_END_TO_END_BENCH_VERSION (5U)
#define STREAM_END_TO_END_BENCH_MAX_BATCH (8U)
#define STREAM_END_TO_END_BENCH_OUTLIERS  (16U)

typedef struct
{
    uint32_t count;
    uint64_t sum_cycles;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint64_t sum_us;
    uint32_t average_us;
    uint32_t min_us;
    uint32_t max_us;
    uint32_t p50_us;
    uint32_t p90_us;
    uint32_t p95_us;
    uint32_t p99_us;
    uint32_t p999_us;
} stream_end_to_end_metric_t;

typedef enum
{
    STREAM_END_TO_END_BENCH_CREATING_FILE = 1,
    STREAM_END_TO_END_BENCH_WARMING_CACHE,
    STREAM_END_TO_END_BENCH_RUNNING,
    STREAM_END_TO_END_BENCH_FINALIZING,
    STREAM_END_TO_END_BENCH_DONE,
    STREAM_END_TO_END_BENCH_ERROR
} stream_end_to_end_bench_state_t;

typedef struct
{
    uint32_t target_page;
    uint8_t batch_size;
    uint8_t cache_hit;
    uint8_t used_free_slot;
    uint8_t used_recycle;
    uint32_t trigger_to_storage_us;
    uint32_t manager_cache_us;
    uint32_t trigger_to_backend_submit_us;
    uint32_t physical_transaction_us;
    uint32_t trigger_to_page_ready_us;
    uint32_t page_ready_to_audio_seen_us;
    uint32_t trigger_to_first_render_us;
} stream_end_to_end_outlier_t;

typedef struct
{
    volatile uint32_t valid;
    uint32_t error;
    uint32_t fail_step;
    uint32_t page_index;
    uint32_t sample_generation;
    uint32_t page_generation;
    uint32_t registration_epoch;
    uint32_t page_state;
    uint32_t available_frame_end;
    uint32_t progressive_chunk_index;
    uint32_t published_chunk_count;
    uint32_t published_chunk_mask;
    uint32_t sta;
    uint32_t mask;
    uint32_t dcount;
    uint32_t dlen;
    uint32_t idmactrl;
    uint32_t idmabase0;
    uint32_t idmabase1;
    uint32_t idmabsize;
    uint32_t cmd18_count;
    uint32_t idmabtc_count;
    uint32_t dataend_count;
    uint32_t cmd12_count;
    uint32_t block_state;
    uint32_t block_result;
    uint32_t block_pending;
    uint32_t block_progressive_chunks;
    uint32_t backend_active_jobs;
    uint32_t backend_result;
    int32_t fresult;
    uint32_t scheduler_owner;
    uint32_t scheduler_class;
    uint32_t gate_owner;
    uint32_t last_successful_stage;
    uint32_t first_failed_stage;
    uint16_t sample_id;
    uint8_t sample_domain;
    uint8_t block_owner;
} stream_end_to_end_error_snapshot_t;

typedef struct
{
    uint32_t voice_index;
    uint32_t target_page;
    uint32_t request_publish_us;
    uint32_t backend_submit_us;
    uint32_t physical_start_us;
    uint32_t first_chunk_available_us;
    uint32_t presocle_exhausted_us;
    int32_t continuation_margin_us;
    uint32_t starvation_us;
    uint8_t continuation_available_before_exhaust;
    uint8_t starved;
    uint16_t reserved;
} stream_end_to_end_voice_queue_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    volatile uint32_t state;
    volatile uint32_t done;
    volatile uint32_t error;
    uint32_t fail_step;
    int32_t last_fresult;
    uint32_t last_block_result;
    uint32_t cpu_hz;
    uint32_t page_size;
    uint32_t chunk_size;
    uint32_t sd_clock_hz;
    uint32_t sd_clock_divider;
    uint32_t num_requests;
    uint32_t simultaneous_cold_starts;
    uint32_t presocle_bytes;
    uint32_t presocle_frames_source;
    uint32_t playback_rate_x;
    uint32_t presocle_ram_per_sample_64_slices_bytes;
    uint32_t audio_block_frames;
    uint32_t audio_block_us;
    uint32_t render_deadline_us;
    uint32_t file_size;
    uint32_t page_count;
    volatile uint32_t progress;
    uint32_t progress_total;
    uint32_t warmup_target_pages;
    uint32_t warmup_pages_ready;
    uint32_t batches_completed;
    uint32_t voices_completed;
    uint32_t pages_requested;
    uint32_t pages_ready;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t cold_with_free_slot;
    uint32_t cold_with_recycle;
    uint32_t cache_alloc;
    uint32_t cache_recycle;
    uint32_t cache_alloc_fail;
    uint32_t audio_page_missing;
    uint32_t manager_calls;
    uint32_t reserve_calls;
    uint32_t recycle_calls;
    uint32_t render_within_1_block_32;
    uint32_t render_within_2_blocks_32;
    uint32_t render_missed_2_blocks_32;
    uint32_t batch_all_rendered_within_1_block_32;
    uint32_t batch_all_rendered_within_2_blocks_32;
    uint32_t batch_missed_2_blocks_32;
    uint32_t audio_irq_count;
    uint64_t audio_irq_cycles_sum;
    uint32_t audio_irq_cycles_max;
    uint32_t audio_irq_load_percent;
    uint32_t audio_wall_latency_max;
    uint32_t audio_preempted_by_sd_count;
    uint32_t sd_rearm_cycles_average;
    uint32_t sd_rearm_cycles_max;
    uint32_t sd_idmabtc_handler_cycles_average;
    uint32_t sd_idmabtc_handler_cycles_max;
    uint32_t sd_idmabtc_interval_cycles_min;
    uint32_t sd_idmabtc_interval_cycles_average;
    uint32_t sd_idmabtc_interval_cycles_max;
    uint32_t rearm_deadline_miss_count;
    uint32_t rejected_non_cold_batches;
    uint32_t timestamp_order_errors;
    volatile uint32_t chunk_publish_count;
    volatile uint32_t chunk0_publish_cycles;
    volatile uint32_t chunk1_publish_cycles;
    volatile uint32_t chunk2_publish_cycles;
    volatile uint32_t chunk3_publish_cycles;
    volatile uint32_t duplicate_chunk_publish_count;
    volatile uint32_t out_of_order_chunk_publish_count;
    volatile uint32_t generation_mismatch_count;
    volatile uint32_t starvation_count;
    volatile uint32_t first_render_not_before_ready_count;
    volatile uint32_t chunk_bit_perfect_count;
    volatile uint32_t chunk_data_mismatch_count;
    volatile uint32_t minimum_lead_frames;
    volatile uint32_t minimum_lead_bytes;
    volatile uint32_t presocle_transition_count;
    volatile uint32_t presocle_transition_mismatch_count;
    volatile uint32_t presocle_bit_perfect_frame_count;
    volatile uint32_t presocle_data_mismatch_count;
    volatile uint32_t continuation_bit_perfect_frame_count;
    volatile uint32_t continuation_data_mismatch_count;
    volatile uint32_t voices_presocle_exhausted;
    volatile uint32_t voices_continuation_ready_before_exhaust;
    volatile uint32_t voices_starved;
    volatile uint32_t starvation_duration_max_us;
    volatile uint64_t starvation_duration_sum_us;
    volatile uint32_t minimum_remaining_presocle_frames;
    volatile uint32_t minimum_remaining_presocle_bytes;
    uint32_t batch_zero_starvation_count;
    uint32_t batch_with_starvation_count;
    int32_t minimum_continuation_margin_us;
    int32_t continuation_margin_p50_us;
    int32_t continuation_margin_p90_us;
    int32_t continuation_margin_p99_us;
    int32_t continuation_margin_p999_us;
    uint32_t instrumentation_ram_bytes;
    stream_end_to_end_error_snapshot_t error_snapshot;

    stream_end_to_end_metric_t trigger_to_need_publish;
    stream_end_to_end_metric_t need_publish_to_storage_seen;
    stream_end_to_end_metric_t storage_seen_to_manager_pick;
    stream_end_to_end_metric_t manager_pick;
    stream_end_to_end_metric_t cache_lookup;
    stream_end_to_end_metric_t cache_reserve;
    stream_end_to_end_metric_t cache_recycle_time;
    stream_end_to_end_metric_t trigger_to_backend_submit;
    stream_end_to_end_metric_t backend_submit_to_physical_start;
    stream_end_to_end_metric_t physical_transaction;
    stream_end_to_end_metric_t data_transfer;
    stream_end_to_end_metric_t physical_complete_to_page_ready;
    stream_end_to_end_metric_t trigger_to_page_ready;
    stream_end_to_end_metric_t trigger_to_full_page_ready;
    stream_end_to_end_metric_t trigger_to_first_chunk_available;
    stream_end_to_end_metric_t first_chunk_available_to_audio_seen;
    stream_end_to_end_metric_t page_ready_to_audio_seen;
    stream_end_to_end_metric_t audio_seen_to_reader_resolve;
    stream_end_to_end_metric_t reader_resolve_to_first_render;
    stream_end_to_end_metric_t trigger_to_first_render;
    stream_end_to_end_metric_t batch_all_ready;
    stream_end_to_end_metric_t batch_all_rendered;

    /* Existing physical decomposition, retained verbatim in meaning. */
    stream_end_to_end_metric_t request_to_backend_accept;
    stream_end_to_end_metric_t backend_accept_to_map_start;
    stream_end_to_end_metric_t physical_mapping;
    stream_end_to_end_metric_t map_to_storage_submit;
    stream_end_to_end_metric_t storage_submit;
    stream_end_to_end_metric_t storage_accept_to_launch;
    stream_end_to_end_metric_t pre_cache_maintenance;
    stream_end_to_end_metric_t command_response;
    stream_end_to_end_metric_t stop_command;
    stream_end_to_end_metric_t physical_complete_to_cache;
    stream_end_to_end_metric_t cache_maintenance;
    stream_end_to_end_metric_t block_publish;
    stream_end_to_end_metric_t backend_complete;

    uint32_t first_voice_ready_us;
    uint32_t last_voice_ready_us;
    uint32_t first_voice_rendered_us;
    uint32_t last_voice_rendered_us;
    stream_end_to_end_voice_queue_t queue[STREAM_END_TO_END_BENCH_MAX_BATCH];
    stream_end_to_end_outlier_t outliers[STREAM_END_TO_END_BENCH_OUTLIERS];
    uint32_t outlier_count;
} stream_end_to_end_bench_result_t;

extern volatile stream_end_to_end_bench_result_t g_stream_end_to_end_bench;

void stream_end_to_end_bench_init(void);
void stream_end_to_end_bench_service(void);
void stream_end_to_end_bench_audio_boundary(uint32_t frames);
uint8_t stream_end_to_end_bench_probe_key(sample_audio_key_t key);

void stream_end_to_end_bench_probe_need(sample_audio_key_t key,
                                        uint32_t page_index,
                                        uint32_t begin_cycles,
                                        uint32_t publish_cycles);
void stream_end_to_end_bench_probe_resolve(sample_audio_key_t key,
                                           uint32_t page_index,
                                           uint32_t begin_cycles,
                                           uint32_t end_cycles,
                                           uint8_t acquired);
void stream_end_to_end_bench_probe_storage_seen(sample_audio_key_t key,
                                                uint32_t page_index,
                                                uint32_t cycles,
                                                uint32_t lookup_cycles,
                                                uint8_t cache_hit);
void stream_end_to_end_bench_probe_manager_pick(sample_audio_key_t key,
                                                uint32_t page_index,
                                                uint32_t begin_cycles,
                                                uint32_t end_cycles);
void stream_end_to_end_bench_probe_reserve(sample_audio_key_t key,
                                           uint32_t page_index,
                                           uint32_t begin_cycles,
                                           uint32_t end_cycles,
                                           uint8_t success);
void stream_end_to_end_bench_probe_allocation(sample_audio_key_t key,
                                              uint32_t page_index,
                                              uint8_t recycled,
                                              uint32_t recycle_cycles);
void stream_end_to_end_bench_probe_io_complete(
    const sample_stream_io_result_t *result,
    uint32_t page_ready_cycles,
    uint8_t page_ready_valid);
void stream_end_to_end_bench_probe_chunk_publish(
    sample_audio_key_t key, uint32_t page_index,
    uint32_t available_frame_end, uint32_t publish_cycles,
    uint8_t accepted, uint8_t duplicate);

#ifdef __cplusplus
}
#endif
