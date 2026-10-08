#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SD_RANDOM_BENCH_MAGIC   UINT32_C(0x53444236)
#define SD_RANDOM_BENCH_VERSION (2U)

typedef struct
{
    uint32_t count;
    uint64_t sum_us;
    uint32_t average_us;
    uint32_t min_us;
    uint32_t max_us;
    uint32_t p50_us;
    uint32_t p90_us;
    uint32_t p99_us;
    uint32_t p999_us;
} sd_random_bench_metric_t;

typedef enum
{
    SD_RANDOM_BENCH_CREATING_FILE = 1,
    SD_RANDOM_BENCH_RUNNING,
    SD_RANDOM_BENCH_SORTING,
    SD_RANDOM_BENCH_DONE,
    SD_RANDOM_BENCH_ERROR
} sd_random_bench_state_t;

typedef enum
{
    SD_RANDOM_BENCH_FAIL_NONE = 0,
    SD_RANDOM_BENCH_FAIL_MOUNT = 1,
    SD_RANDOM_BENCH_FAIL_MKDIR = 2,
    SD_RANDOM_BENCH_FAIL_OPEN = 3,
    SD_RANDOM_BENCH_FAIL_EXPAND = 4,
    SD_RANDOM_BENCH_FAIL_WRITE = 5,
    SD_RANDOM_BENCH_FAIL_SYNC = 6,
    SD_RANDOM_BENCH_FAIL_CLOSE = 7,
    SD_RANDOM_BENCH_FAIL_REOPEN = 8,
    SD_RANDOM_BENCH_FAIL_MAP = 9,
    SD_RANDOM_BENCH_FAIL_RANDOM_READ = 10
} sd_random_bench_fail_step_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    volatile uint32_t state;
    volatile uint32_t progress;
    uint32_t progress_total;
    volatile uint32_t done;
    volatile uint32_t error;
    uint32_t fail_step;
    int32_t last_fresult;
    uint32_t last_block_result;
    uint32_t write_offset;
    uint32_t bytes_written;
    uint32_t disk_status;
    uint32_t card_state;
    uint32_t cpu_hz;
    uint32_t file_size;
    uint32_t page_size;
    uint32_t num_reads;
    uint32_t completed_reads;
    uint64_t total_time_us;
    uint64_t total_bytes;
    uint64_t latency_sum_us;
    uint32_t latency_min_us;
    uint32_t latency_avg_us;
    uint32_t latency_p50_us;
    uint32_t latency_p90_us;
    uint32_t latency_p95_us;
    uint32_t latency_p99_us;
    uint32_t latency_p999_us;
    uint32_t latency_max_us;
    uint64_t transaction_sum_us;
    uint32_t transaction_min_us;
    uint32_t transaction_avg_us;
    uint32_t transaction_p99_us;
    uint32_t transaction_max_us;
    uint64_t queue_sum_us;
    uint32_t queue_avg_us;
    uint32_t queue_max_us;
    uint32_t pages_per_second;
    uint32_t pages_per_second_x1000;
    uint64_t bytes_per_second;
    uint32_t errors;
    uint32_t timeouts;
    uint32_t last_page;
    uint32_t last_request_cycles;
    uint32_t last_transaction_start_cycles;
    uint32_t last_transaction_end_cycles;
    uint32_t last_ready_cycles;
    sd_random_bench_metric_t request_to_ready;
    sd_random_bench_metric_t before_transaction;
    sd_random_bench_metric_t physical_transaction;
    sd_random_bench_metric_t after_transaction;
    sd_random_bench_metric_t request_to_backend_accept;
    sd_random_bench_metric_t backend_accept_to_map_start;
    sd_random_bench_metric_t physical_mapping;
    sd_random_bench_metric_t map_to_storage_submit;
    sd_random_bench_metric_t storage_submit;
    sd_random_bench_metric_t map_to_storage_accept;
    sd_random_bench_metric_t storage_accept_to_launch;
    sd_random_bench_metric_t launch_to_command;
    sd_random_bench_metric_t storage_accept_to_command;
    sd_random_bench_metric_t pre_cache_maintenance;
    sd_random_bench_metric_t command_response;
    sd_random_bench_metric_t data_transfer;
    sd_random_bench_metric_t stop_command;
    sd_random_bench_metric_t completion_processing;
    sd_random_bench_metric_t physical_complete_to_cache;
    sd_random_bench_metric_t cache_maintenance;
    sd_random_bench_metric_t cache_to_block_publish;
    sd_random_bench_metric_t block_publish_to_backend_complete;
    sd_random_bench_metric_t backend_complete_to_ready;
    uint32_t last_backend_accept_cycles;
    uint32_t last_map_start_cycles;
    uint32_t last_map_end_cycles;
    uint32_t last_storage_submit_enter_cycles;
    uint32_t last_storage_accept_cycles;
    uint32_t last_launch_enter_cycles;
    uint32_t last_pre_cache_start_cycles;
    uint32_t last_pre_cache_end_cycles;
    uint32_t last_command_cycles;
    uint32_t last_dma_launch_return_cycles;
    uint32_t last_data_start_cycles;
    uint32_t last_data_end_cycles;
    uint32_t last_cache_start_cycles;
    uint32_t last_cache_end_cycles;
    uint32_t last_block_publish_cycles;
    uint32_t last_backend_complete_cycles;
} sd_random_bench_result_t;

extern volatile sd_random_bench_result_t g_sd_random_bench;

void sd_random_bench_init(void);
void sd_random_bench_service(void);

#ifdef __cplusplus
}
#endif
