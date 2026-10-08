#include "SD/sd_random_bench.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_backend_physical.h"
#include "Sampler/sample_stream_fatfs_map.h"
#include "SD/sd_scheduler_runtime.h"
#include "SD/sd_diskio.h"
#include "Storage/sd_access_gate.h"
#include "Storage/persistent_fatfs_io.h"
#include "ff.h"
#include "sdmmc.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

#define SD_BENCH_PATH              "0:/BRICK/TEST/SD_RANDOM.B6T"
#define SD_BENCH_FILE_512_MIB      (UINT32_C(512) * 1024U * 1024U)
#define SD_BENCH_FILE_256_MIB      (UINT32_C(256) * 1024U * 1024U)
#define SD_BENCH_PAGE_BYTES        (64U * 1024U)
#define SD_BENCH_PAGE_SECTORS      (SD_BENCH_PAGE_BYTES / 512U)
#define SD_BENCH_READ_COUNT        (100000U)
#define SD_BENCH_MAX_PAGES         (SD_BENCH_FILE_512_MIB / SD_BENCH_PAGE_BYTES)
#define SD_BENCH_HISTOGRAM_BIN_US  (10U)
#define SD_BENCH_HISTOGRAM_BINS    (2048U)
#define SD_BENCH_SD_CLOCK_DIVIDER  (4U)

enum
{
    SD_BENCH_ERROR_NONE = 0,
    SD_BENCH_ERROR_GATE,
    SD_BENCH_ERROR_MOUNT,
    SD_BENCH_ERROR_DIRECTORY,
    SD_BENCH_ERROR_FILE,
    SD_BENCH_ERROR_WRITE,
    SD_BENCH_ERROR_SYNC,
    SD_BENCH_ERROR_MAP,
    SD_BENCH_ERROR_FRAGMENT,
    SD_BENCH_ERROR_SUBMIT,
    SD_BENCH_ERROR_READ
};

typedef struct
{
    persistent_fatfs_file_t file;
    sample_stream_safe_metadata_t metadata;
    sample_stream_backend_physical_async_t io;
    sample_stream_physical_cursor_t cursor;
    sample_page_load_target_t target;
    uint32_t create_offset;
    uint32_t prng;
    uint32_t request_cycles;
    uint16_t page_order[SD_BENCH_MAX_PAGES];
    uint16_t page_count;
    uint8_t gate_held;
    uint8_t file_open;
    uint8_t io_active;
} sd_random_bench_runtime_t;

volatile sd_random_bench_result_t g_sd_random_bench;
SDRAM_STREAM_SERVICE static sd_random_bench_runtime_t g_sd_bench_runtime;
SDRAM_STREAM_SCRATCH static uint8_t g_sd_bench_buffer[SD_BENCH_PAGE_BYTES];
SDRAM_STREAM_SCRATCH static uint32_t
    g_sd_bench_latency_histogram[SD_BENCH_HISTOGRAM_BINS];
SDRAM_STREAM_SCRATCH static uint32_t
    g_sd_bench_transaction_histogram[SD_BENCH_HISTOGRAM_BINS];
SDRAM_STREAM_SCRATCH static uint32_t
    g_sd_bench_before_histogram[SD_BENCH_HISTOGRAM_BINS];
SDRAM_STREAM_SCRATCH static uint32_t
    g_sd_bench_after_histogram[SD_BENCH_HISTOGRAM_BINS];

static uint32_t sd_bench_now(void)
{
    return DWT->CYCCNT;
}

static uint32_t sd_bench_cycles_to_us(uint32_t cycles)
{
    const uint32_t hz = (g_sd_random_bench.cpu_hz != 0U)
        ? g_sd_random_bench.cpu_hz : 1U;
    return (uint32_t)(((uint64_t)cycles * UINT64_C(1000000) + (hz / 2U)) / hz);
}

static uint8_t sd_bench_configure_sd_clock(void)
{
    const uint32_t kernel_hz =
        HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
    if (kernel_hz == 0U) return 0U;
    hsd1.Init.ClockDiv = SD_BENCH_SD_CLOCK_DIVIDER;
    MODIFY_REG(hsd1.Instance->CLKCR, SDMMC_CLKCR_CLKDIV,
               SD_BENCH_SD_CLOCK_DIVIDER);
    __DSB();
    g_sd_random_bench.sd_clock_divider =
        (hsd1.Instance->CLKCR & SDMMC_CLKCR_CLKDIV_Msk)
            >> SDMMC_CLKCR_CLKDIV_Pos;
    if (g_sd_random_bench.sd_clock_divider == 0U) return 0U;
    g_sd_random_bench.sd_clock_hz = kernel_hz
        / (2U * g_sd_random_bench.sd_clock_divider);
    return 1U;
}

static void sd_bench_metric_init(volatile sd_random_bench_metric_t *metric)
{
    memset((void *)metric, 0, sizeof(*metric));
    metric->min_us = UINT32_MAX;
}

static void sd_bench_metric_add(volatile sd_random_bench_metric_t *metric,
                                uint32_t cycles)
{
    const uint32_t us = sd_bench_cycles_to_us(cycles);
    metric->count++;
    metric->sum_us += us;
    if (us < metric->min_us) metric->min_us = us;
    if (us > metric->max_us) metric->max_us = us;
}

static void sd_bench_histogram_add(uint32_t *histogram, uint32_t us)
{
    uint32_t bin = us / SD_BENCH_HISTOGRAM_BIN_US;
    if (bin >= SD_BENCH_HISTOGRAM_BINS)
        bin = SD_BENCH_HISTOGRAM_BINS - 1U;
    histogram[bin]++;
}

static uint8_t sd_bench_timestamps_valid(const uint32_t *timestamps,
                                         uint32_t count)
{
    enum
    {
        SD_BENCH_TIMESTAMP_COMMAND = 9U,
        SD_BENCH_TIMESTAMP_DATA_START = 11U
    };
    for (uint32_t i = 1U; i < count; ++i)
    {
        /* CMDREND may preempt SDMMC_SendCommand/start_read before their CPU
         * call chain returns.  dma_launch_return is diagnostic only, so it
         * has no strict ordering relationship with data_start. */
        if (i == SD_BENCH_TIMESTAMP_DATA_START) continue;
        /* Unsigned subtraction preserves a normal DWT wrap.  A delta larger
         * than half the 32-bit counter range denotes an impossible reversed
         * boundary for this request (the SD timeout is shorter). */
        if ((timestamps[i] - timestamps[i - 1U]) > INT32_MAX)
        {
            g_sd_random_bench.timestamp_order_errors++;
            g_sd_random_bench.last_timestamp_order_error_edge = i;
            return 0U;
        }
    }
    if ((timestamps[SD_BENCH_TIMESTAMP_DATA_START]
            - timestamps[SD_BENCH_TIMESTAMP_COMMAND]) > INT32_MAX)
    {
        g_sd_random_bench.timestamp_order_errors++;
        g_sd_random_bench.last_timestamp_order_error_edge =
            SD_BENCH_TIMESTAMP_DATA_START;
        return 0U;
    }
    return 1U;
}

static void sd_bench_snapshot_last_timestamps(
    const sd_block_device_async_request_t *request, uint32_t ready_cycles)
{
    g_sd_random_bench.last_request_cycles = g_sd_bench_runtime.request_cycles;
    g_sd_random_bench.last_backend_accept_cycles =
        g_sd_bench_runtime.io.perf_accept_cycles;
    g_sd_random_bench.last_map_start_cycles =
        g_sd_bench_runtime.io.perf_map_start_cycles;
    g_sd_random_bench.last_map_end_cycles =
        g_sd_bench_runtime.io.perf_map_end_cycles;
    g_sd_random_bench.last_storage_submit_enter_cycles =
        request->perf_submit_enter_cycles;
    g_sd_random_bench.last_storage_accept_cycles = request->perf_submit_cycles;
    g_sd_random_bench.last_launch_enter_cycles =
        request->perf_launch_enter_cycles;
    g_sd_random_bench.last_pre_cache_start_cycles =
        request->perf_pre_cache_start_cycles;
    g_sd_random_bench.last_pre_cache_end_cycles =
        request->perf_pre_cache_end_cycles;
    g_sd_random_bench.last_command_cycles = request->perf_command_cycles;
    g_sd_random_bench.last_dma_launch_return_cycles = request->perf_dma_cycles;
    g_sd_random_bench.last_data_start_cycles = request->perf_data_start_cycles;
    g_sd_random_bench.last_data_end_cycles = request->perf_data_end_cycles;
    g_sd_random_bench.last_transaction_start_cycles =
        request->perf_command_cycles;
    g_sd_random_bench.last_transaction_end_cycles =
        request->perf_complete_cycles;
    g_sd_random_bench.last_cache_start_cycles = request->perf_cache_start_cycles;
    g_sd_random_bench.last_cache_end_cycles = request->perf_cache_end_cycles;
    g_sd_random_bench.last_block_publish_cycles = request->perf_publish_cycles;
    g_sd_random_bench.last_backend_complete_cycles =
        g_sd_bench_runtime.io.perf_complete_cycles;
    g_sd_random_bench.last_ready_cycles = ready_cycles;
}

static uint32_t sd_bench_prng(void)
{
    uint32_t x = g_sd_bench_runtime.prng;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    g_sd_bench_runtime.prng = x;
    return x;
}

static void sd_bench_release_gate(void)
{
    if (g_sd_bench_runtime.gate_held != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        g_sd_bench_runtime.gate_held = 0U;
    }
}

static void sd_bench_snapshot_media(void)
{
    g_sd_random_bench.disk_status = sd_diskio_debug_status();
    g_sd_random_bench.card_state = sd_diskio_debug_card_state();
}

static void sd_bench_fail(uint32_t error, uint32_t fail_step, FRESULT fr,
                          sd_block_device_result_t block_result)
{
    if (g_sd_bench_runtime.file_open != 0U)
    {
        persistent_fatfs_close(&g_sd_bench_runtime.file);
        g_sd_bench_runtime.file_open = 0U;
    }
    sd_bench_release_gate();
    g_sd_random_bench.error = error;
    g_sd_random_bench.fail_step = fail_step;
    g_sd_random_bench.last_fresult = (int32_t)fr;
    g_sd_random_bench.last_block_result = (uint32_t)block_result;
    sd_bench_snapshot_media();
    g_sd_random_bench.errors++;
    if (block_result == SD_BLOCK_DEVICE_TIMEOUT)
    {
        g_sd_random_bench.timeouts++;
    }
    g_sd_random_bench.state = SD_RANDOM_BENCH_ERROR;
    g_sd_random_bench.done = 1U;
}

static FRESULT sd_bench_make_dirs(void)
{
    FRESULT fr = f_mkdir("0:/BRICK");
    if ((fr != FR_OK) && (fr != FR_EXIST)) return fr;
    fr = f_mkdir("0:/BRICK/TEST");
    return (fr == FR_EXIST) ? FR_OK : fr;
}

static void sd_bench_fill_pattern(void)
{
    uint32_t x = UINT32_C(0x6B8B4567);
    uint32_t *const words = (uint32_t *)(void *)g_sd_bench_buffer;
    for (uint32_t i = 0U; i < (SD_BENCH_PAGE_BYTES / sizeof(uint32_t)); ++i)
    {
        x ^= x << 13U;
        x ^= x >> 17U;
        x ^= x << 5U;
        words[i] = x ^ i;
    }
}

static uint8_t sd_bench_open_and_map(void)
{
    if (persistent_fatfs_open_read(
            &g_sd_bench_runtime.file, SD_BENCH_PATH) == 0U)
    {
        sd_bench_fail(SD_BENCH_ERROR_FILE, SD_RANDOM_BENCH_FAIL_REOPEN,
                      g_sd_bench_runtime.file.last_result,
                      SD_BLOCK_DEVICE_OK);
        return 0U;
    }
    g_sd_bench_runtime.file_open = 1U;
    memset(&g_sd_bench_runtime.metadata, 0, sizeof(g_sd_bench_runtime.metadata));
    if (sample_stream_fatfs_map_build_from_file(
            &g_sd_bench_runtime.file.file,
            &g_sd_bench_runtime.metadata) == 0U)
    {
        sd_bench_fail(SD_BENCH_ERROR_MAP, SD_RANDOM_BENCH_FAIL_MAP,
                      FR_INT_ERR, SD_BLOCK_DEVICE_OK);
        return 0U;
    }
    const FRESULT fr = persistent_fatfs_close_result(
        &g_sd_bench_runtime.file);
    g_sd_bench_runtime.file_open = 0U;
    if (fr != FR_OK)
    {
        sd_bench_fail(SD_BENCH_ERROR_FILE, SD_RANDOM_BENCH_FAIL_CLOSE,
                      fr, SD_BLOCK_DEVICE_OK);
        return 0U;
    }
    g_sd_bench_runtime.metadata.block_align = 1U;
    g_sd_bench_runtime.metadata.data_offset_bytes = 0U;
    g_sd_bench_runtime.metadata.file_size = g_sd_random_bench.file_size;
    g_sd_bench_runtime.page_count = (uint16_t)(
        g_sd_random_bench.file_size / SD_BENCH_PAGE_BYTES);
    return 1U;
}

static void sd_bench_shuffle_pages(void)
{
    const uint16_t count = g_sd_bench_runtime.page_count;
    for (uint16_t i = 0U; i < count; ++i)
    {
        g_sd_bench_runtime.page_order[i] = i;
    }
    for (uint32_t i = count; i > 1U; --i)
    {
        const uint32_t j = sd_bench_prng() % i;
        const uint16_t tmp = g_sd_bench_runtime.page_order[i - 1U];
        g_sd_bench_runtime.page_order[i - 1U] = g_sd_bench_runtime.page_order[j];
        g_sd_bench_runtime.page_order[j] = tmp;
    }
    if ((g_sd_random_bench.completed_reads != 0U) && (count > 1U)
        && (g_sd_bench_runtime.page_order[0] == g_sd_random_bench.last_page))
    {
        const uint16_t tmp = g_sd_bench_runtime.page_order[0];
        g_sd_bench_runtime.page_order[0] = g_sd_bench_runtime.page_order[1];
        g_sd_bench_runtime.page_order[1] = tmp;
    }
}

static void sd_bench_start_running(void)
{
    sd_bench_release_gate();
    g_sd_bench_runtime.prng = UINT32_C(0x9E3779B9);
    sd_bench_shuffle_pages();
    g_sd_random_bench.progress = 0U;
    g_sd_random_bench.progress_total = SD_BENCH_READ_COUNT;
    g_sd_random_bench.state = SD_RANDOM_BENCH_RUNNING;
}

static void sd_bench_prepare_file(void)
{
    if (g_sd_bench_runtime.gate_held == 0U)
    {
        if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
        {
            sd_bench_fail(SD_BENCH_ERROR_GATE, SD_RANDOM_BENCH_FAIL_MOUNT,
                          FR_LOCKED,
                          SD_BLOCK_DEVICE_GATE_NOT_HELD);
            return;
        }
        g_sd_bench_runtime.gate_held = 1U;
        if (sd_access_storage_status() != SD_STORAGE_STATUS_READY)
        {
            sd_bench_fail(SD_BENCH_ERROR_MOUNT, SD_RANDOM_BENCH_FAIL_MOUNT,
                          FR_NOT_READY,
                          SD_BLOCK_DEVICE_OK);
            return;
        }
        const FRESULT mkdir_fr = sd_bench_make_dirs();
        if (mkdir_fr != FR_OK)
        {
            sd_bench_fail(SD_BENCH_ERROR_DIRECTORY,
                          SD_RANDOM_BENCH_FAIL_MKDIR, mkdir_fr,
                          SD_BLOCK_DEVICE_OK);
            return;
        }

        FILINFO info;
        const FRESULT stat_fr = f_stat(SD_BENCH_PATH, &info);
        if ((stat_fr == FR_OK)
            && ((info.fsize == SD_BENCH_FILE_512_MIB)
                || (info.fsize == SD_BENCH_FILE_256_MIB)))
        {
            g_sd_random_bench.file_size = (uint32_t)info.fsize;
            if (sd_bench_open_and_map() != 0U) sd_bench_start_running();
            return;
        }
        if ((stat_fr != FR_OK) && (stat_fr != FR_NO_FILE))
        {
            sd_bench_fail(SD_BENCH_ERROR_FILE, SD_RANDOM_BENCH_FAIL_OPEN,
                          stat_fr, SD_BLOCK_DEVICE_OK);
            return;
        }
        if (stat_fr == FR_OK) (void)f_unlink(SD_BENCH_PATH);

        DWORD free_clusters = 0U;
        FATFS *free_fs = 0;
        FRESULT fr = f_getfree("0:", &free_clusters, &free_fs);
        if ((fr != FR_OK) || (free_fs == 0))
        {
            sd_bench_fail(SD_BENCH_ERROR_FILE, SD_RANDOM_BENCH_FAIL_OPEN,
                          fr, SD_BLOCK_DEVICE_OK);
            return;
        }
        const uint64_t free_bytes = (uint64_t)free_clusters
            * (uint64_t)free_fs->csize * 512U;
        if (free_bytes >= SD_BENCH_FILE_512_MIB)
            g_sd_random_bench.file_size = SD_BENCH_FILE_512_MIB;
        else if (free_bytes >= SD_BENCH_FILE_256_MIB)
            g_sd_random_bench.file_size = SD_BENCH_FILE_256_MIB;
        else
        {
            sd_bench_fail(SD_BENCH_ERROR_FILE, SD_RANDOM_BENCH_FAIL_OPEN,
                          FR_DENIED, SD_BLOCK_DEVICE_OK);
            return;
        }
        fr = persistent_fatfs_open_write_result(
            &g_sd_bench_runtime.file, SD_BENCH_PATH);
        if (fr != FR_OK)
        {
            sd_bench_fail(SD_BENCH_ERROR_FILE, SD_RANDOM_BENCH_FAIL_OPEN,
                          fr, SD_BLOCK_DEVICE_OK);
            return;
        }
        g_sd_bench_runtime.file_open = 1U;
        sd_bench_fill_pattern();
        g_sd_random_bench.progress_total = g_sd_random_bench.file_size;
    }

    if (g_sd_bench_runtime.create_offset < g_sd_random_bench.file_size)
    {
        const persist_codec_sink_t sink = persistent_fatfs_sink(
            &g_sd_bench_runtime.file);
        const uint8_t write_ok = sink.write(
            sink.context, g_sd_bench_buffer, SD_BENCH_PAGE_BYTES);
        const FRESULT fr = g_sd_bench_runtime.file.last_result;
        g_sd_random_bench.bytes_written =
            g_sd_bench_runtime.file.transferred;
        g_sd_bench_runtime.create_offset +=
            g_sd_bench_runtime.file.transferred;
        g_sd_random_bench.write_offset = g_sd_bench_runtime.create_offset;
        sd_bench_snapshot_media();
        if (write_ok == 0U)
        {
            sd_bench_fail(SD_BENCH_ERROR_WRITE, SD_RANDOM_BENCH_FAIL_WRITE,
                          fr, SD_BLOCK_DEVICE_OK);
            return;
        }
        g_sd_random_bench.progress = g_sd_bench_runtime.create_offset;
        return;
    }

    FRESULT fr = f_sync(&g_sd_bench_runtime.file.file);
    if (fr != FR_OK)
    {
        sd_bench_fail(SD_BENCH_ERROR_SYNC, SD_RANDOM_BENCH_FAIL_SYNC,
                      fr, SD_BLOCK_DEVICE_OK);
        return;
    }
    fr = persistent_fatfs_close_result(&g_sd_bench_runtime.file);
    g_sd_bench_runtime.file_open = 0U;
    if (fr != FR_OK)
    {
        sd_bench_fail(SD_BENCH_ERROR_FILE, SD_RANDOM_BENCH_FAIL_CLOSE,
                      fr, SD_BLOCK_DEVICE_OK);
        return;
    }
    if (sd_bench_open_and_map() != 0U) sd_bench_start_running();
}

static uint8_t sd_bench_begin_read(void)
{
    if ((g_sd_random_bench.completed_reads != 0U)
        && ((g_sd_random_bench.completed_reads
             % g_sd_bench_runtime.page_count) == 0U))
    {
        sd_bench_shuffle_pages();
    }
    const uint32_t ordinal = g_sd_random_bench.completed_reads
        % g_sd_bench_runtime.page_count;
    const uint32_t page = g_sd_bench_runtime.page_order[ordinal];
    sample_stream_physical_span_t span;
    memset(&g_sd_bench_runtime.cursor, 0, sizeof(g_sd_bench_runtime.cursor));
    if ((sample_stream_physical_map_resolve(
            &g_sd_bench_runtime.metadata.physical_map,
            (uint64_t)page * SD_BENCH_PAGE_BYTES, SD_BENCH_PAGE_BYTES,
            &g_sd_bench_runtime.cursor, &span) == 0U)
        || (span.first_sector_skip != 0U)
        || (span.logical_bytes != SD_BENCH_PAGE_BYTES)
        || (span.sector_count != SD_BENCH_PAGE_SECTORS))
    {
        sd_bench_fail(SD_BENCH_ERROR_FRAGMENT,
                      SD_RANDOM_BENCH_FAIL_RANDOM_READ, FR_INT_ERR,
                      SD_BLOCK_DEVICE_INVALID_ARG);
        return 0U;
    }

    memset(&g_sd_bench_runtime.target, 0, sizeof(g_sd_bench_runtime.target));
    g_sd_bench_runtime.target.start_frame = page * SD_BENCH_PAGE_BYTES;
    g_sd_bench_runtime.target.frame_count = SD_BENCH_PAGE_BYTES;
    memset(&g_sd_bench_runtime.cursor, 0, sizeof(g_sd_bench_runtime.cursor));
    g_sd_bench_runtime.request_cycles = sd_bench_now();
    if (sample_stream_backend_physical_begin(
            &g_sd_bench_runtime.io, &g_sd_bench_runtime.metadata,
            &g_sd_bench_runtime.target, &g_sd_bench_runtime.cursor,
            g_sd_bench_buffer, sizeof(g_sd_bench_buffer), 0U,
            UINT32_MAX) == 0U)
    {
        sd_bench_fail(SD_BENCH_ERROR_SUBMIT,
                      SD_RANDOM_BENCH_FAIL_RANDOM_READ, FR_INT_ERR,
                      SD_BLOCK_DEVICE_BUSY);
        return 0U;
    }
    g_sd_random_bench.last_page = page;
    g_sd_random_bench.last_request_cycles = g_sd_bench_runtime.request_cycles;
    g_sd_bench_runtime.io_active = 1U;
    return 1U;
}

static void sd_bench_record_read(void)
{
    sample_page_load_result_t result;
    const uint8_t *source = 0;
    uint32_t source_bytes = 0U;
    uint8_t physical_reads = 0U;
    if (sample_stream_backend_physical_poll(
            &g_sd_bench_runtime.io, &result, &source,
            &source_bytes, &physical_reads) == 0U)
    {
        return;
    }
    g_sd_bench_runtime.io_active = 0U;
    const sd_block_device_async_request_t *const request =
        &g_sd_bench_runtime.io.request;
    const uint32_t ready_cycles = sd_bench_now();
    if ((result != SAMPLE_PAGE_LOAD_OK) || (source != g_sd_bench_buffer)
        || (source_bytes != SD_BENCH_PAGE_BYTES) || (physical_reads != 1U)
        || (request->result != SD_BLOCK_DEVICE_OK))
    {
        sd_bench_fail(SD_BENCH_ERROR_READ,
                      SD_RANDOM_BENCH_FAIL_RANDOM_READ,
                      FR_DISK_ERR, request->result);
        return;
    }
    sd_bench_snapshot_last_timestamps(request, ready_cycles);
    const uint32_t timestamps[] = {
        g_sd_bench_runtime.request_cycles,
        g_sd_bench_runtime.io.perf_accept_cycles,
        g_sd_bench_runtime.io.perf_map_start_cycles,
        g_sd_bench_runtime.io.perf_map_end_cycles,
        request->perf_submit_enter_cycles,
        request->perf_submit_cycles,
        request->perf_launch_enter_cycles,
        request->perf_pre_cache_start_cycles,
        request->perf_pre_cache_end_cycles,
        request->perf_command_cycles,
        request->perf_dma_cycles,
        request->perf_data_start_cycles,
        request->perf_data_end_cycles,
        request->perf_complete_cycles,
        request->perf_cache_start_cycles,
        request->perf_cache_end_cycles,
        request->perf_publish_cycles,
        g_sd_bench_runtime.io.perf_complete_cycles,
        ready_cycles,
    };
    if (sd_bench_timestamps_valid(
            timestamps, sizeof(timestamps) / sizeof(timestamps[0])) == 0U)
    {
        sd_bench_fail(SD_BENCH_ERROR_READ,
                      SD_RANDOM_BENCH_FAIL_RANDOM_READ,
                      FR_INT_ERR, SD_BLOCK_DEVICE_INVALID_ARG);
        return;
    }

    const uint32_t index = g_sd_random_bench.completed_reads;
    const uint32_t latency_us = sd_bench_cycles_to_us(
        ready_cycles - g_sd_bench_runtime.request_cycles);
    const uint32_t transaction_us = sd_bench_cycles_to_us(
        request->perf_complete_cycles - request->perf_command_cycles);
    const uint32_t queue_us = sd_bench_cycles_to_us(
        request->perf_command_cycles - g_sd_bench_runtime.request_cycles);
    const uint32_t after_us = sd_bench_cycles_to_us(
        ready_cycles - request->perf_complete_cycles);
    sd_bench_histogram_add(g_sd_bench_latency_histogram, latency_us);
    sd_bench_histogram_add(g_sd_bench_transaction_histogram, transaction_us);
    sd_bench_histogram_add(g_sd_bench_before_histogram, queue_us);
    sd_bench_histogram_add(g_sd_bench_after_histogram, after_us);
    sd_bench_metric_add(&g_sd_random_bench.request_to_ready,
        ready_cycles - g_sd_bench_runtime.request_cycles);
    sd_bench_metric_add(&g_sd_random_bench.before_transaction,
        request->perf_command_cycles - g_sd_bench_runtime.request_cycles);
    sd_bench_metric_add(&g_sd_random_bench.physical_transaction,
        request->perf_complete_cycles - request->perf_command_cycles);
    sd_bench_metric_add(&g_sd_random_bench.after_transaction,
        ready_cycles - request->perf_complete_cycles);
    sd_bench_metric_add(&g_sd_random_bench.request_to_backend_accept,
        g_sd_bench_runtime.io.perf_accept_cycles
            - g_sd_bench_runtime.request_cycles);
    sd_bench_metric_add(&g_sd_random_bench.backend_accept_to_map_start,
        g_sd_bench_runtime.io.perf_map_start_cycles
            - g_sd_bench_runtime.io.perf_accept_cycles);
    sd_bench_metric_add(&g_sd_random_bench.physical_mapping,
        g_sd_bench_runtime.io.perf_map_end_cycles
            - g_sd_bench_runtime.io.perf_map_start_cycles);
    sd_bench_metric_add(&g_sd_random_bench.map_to_storage_submit,
        request->perf_submit_enter_cycles
            - g_sd_bench_runtime.io.perf_map_end_cycles);
    sd_bench_metric_add(&g_sd_random_bench.storage_submit,
        request->perf_submit_cycles - request->perf_submit_enter_cycles);
    sd_bench_metric_add(&g_sd_random_bench.map_to_storage_accept,
        request->perf_submit_cycles - g_sd_bench_runtime.io.perf_map_end_cycles);
    sd_bench_metric_add(&g_sd_random_bench.storage_accept_to_launch,
        request->perf_launch_enter_cycles - request->perf_submit_cycles);
    sd_bench_metric_add(&g_sd_random_bench.launch_to_command,
        request->perf_command_cycles - request->perf_launch_enter_cycles);
    sd_bench_metric_add(&g_sd_random_bench.storage_accept_to_command,
        request->perf_command_cycles - request->perf_submit_cycles);
    sd_bench_metric_add(&g_sd_random_bench.pre_cache_maintenance,
        request->perf_pre_cache_end_cycles
            - request->perf_pre_cache_start_cycles);
    sd_bench_metric_add(&g_sd_random_bench.command_response,
        request->perf_data_start_cycles - request->perf_command_cycles);
    sd_bench_metric_add(&g_sd_random_bench.data_transfer,
        request->perf_data_end_cycles - request->perf_data_start_cycles);
    sd_bench_metric_add(&g_sd_random_bench.stop_command,
        request->perf_complete_cycles - request->perf_data_end_cycles);
    sd_bench_metric_add(&g_sd_random_bench.completion_processing,
        request->perf_publish_cycles - request->perf_complete_cycles);
    sd_bench_metric_add(&g_sd_random_bench.physical_complete_to_cache,
        request->perf_cache_start_cycles - request->perf_complete_cycles);
    sd_bench_metric_add(&g_sd_random_bench.cache_maintenance,
        request->perf_cache_end_cycles - request->perf_cache_start_cycles);
    sd_bench_metric_add(&g_sd_random_bench.cache_to_block_publish,
        request->perf_publish_cycles - request->perf_cache_end_cycles);
    sd_bench_metric_add(
        &g_sd_random_bench.block_publish_to_backend_complete,
        g_sd_bench_runtime.io.perf_complete_cycles
            - request->perf_publish_cycles);
    sd_bench_metric_add(&g_sd_random_bench.backend_complete_to_ready,
        ready_cycles - g_sd_bench_runtime.io.perf_complete_cycles);
    g_sd_random_bench.latency_sum_us += latency_us;
    g_sd_random_bench.transaction_sum_us += transaction_us;
    g_sd_random_bench.queue_sum_us += queue_us;
    if (latency_us < g_sd_random_bench.latency_min_us)
        g_sd_random_bench.latency_min_us = latency_us;
    if (latency_us > g_sd_random_bench.latency_max_us)
        g_sd_random_bench.latency_max_us = latency_us;
    if (transaction_us < g_sd_random_bench.transaction_min_us)
        g_sd_random_bench.transaction_min_us = transaction_us;
    if (transaction_us > g_sd_random_bench.transaction_max_us)
        g_sd_random_bench.transaction_max_us = transaction_us;
    if (queue_us > g_sd_random_bench.queue_max_us)
        g_sd_random_bench.queue_max_us = queue_us;
    g_sd_random_bench.completed_reads = index + 1U;
    g_sd_random_bench.progress = index + 1U;
    if (g_sd_random_bench.completed_reads >= SD_BENCH_READ_COUNT)
    {
        g_sd_random_bench.state = SD_RANDOM_BENCH_SORTING;
    }
}

static uint32_t sd_bench_percentile(const uint32_t *histogram, uint32_t count,
                                    uint32_t permille)
{
    uint32_t rank = (uint32_t)(((uint64_t)count * permille + 999U) / 1000U);
    if (rank == 0U) rank = 1U;
    uint32_t cumulative = 0U;
    for (uint32_t bin = 0U; bin < SD_BENCH_HISTOGRAM_BINS; ++bin)
    {
        cumulative += histogram[bin];
        if (cumulative >= rank) return bin * SD_BENCH_HISTOGRAM_BIN_US;
    }
    return (SD_BENCH_HISTOGRAM_BINS - 1U) * SD_BENCH_HISTOGRAM_BIN_US;
}

static void sd_bench_metric_finalize(
    volatile sd_random_bench_metric_t *metric, const uint32_t *histogram)
{
    if (metric->count == 0U) return;
    metric->average_us = (uint32_t)(metric->sum_us / metric->count);
    if (histogram == 0) return;
    metric->p50_us = sd_bench_percentile(histogram, metric->count, 500U);
    metric->p90_us = sd_bench_percentile(histogram, metric->count, 900U);
    metric->p99_us = sd_bench_percentile(histogram, metric->count, 990U);
    metric->p999_us = sd_bench_percentile(histogram, metric->count, 999U);
}

static void sd_bench_finalize(void)
{
    const uint32_t count = g_sd_random_bench.completed_reads;
    g_sd_random_bench.num_reads = count;
    g_sd_random_bench.total_bytes = (uint64_t)count * SD_BENCH_PAGE_BYTES;
    g_sd_random_bench.total_time_us = g_sd_random_bench.latency_sum_us;
    g_sd_random_bench.latency_avg_us = (uint32_t)(
        g_sd_random_bench.latency_sum_us / count);
    g_sd_random_bench.latency_p50_us = sd_bench_percentile(
        g_sd_bench_latency_histogram, count, 500U);
    g_sd_random_bench.latency_p90_us = sd_bench_percentile(
        g_sd_bench_latency_histogram, count, 900U);
    g_sd_random_bench.latency_p95_us = sd_bench_percentile(
        g_sd_bench_latency_histogram, count, 950U);
    g_sd_random_bench.latency_p99_us = sd_bench_percentile(
        g_sd_bench_latency_histogram, count, 990U);
    g_sd_random_bench.latency_p999_us = sd_bench_percentile(
        g_sd_bench_latency_histogram, count, 999U);
    g_sd_random_bench.transaction_avg_us = (uint32_t)(
        g_sd_random_bench.transaction_sum_us / count);
    g_sd_random_bench.transaction_p99_us = sd_bench_percentile(
        g_sd_bench_transaction_histogram, count, 990U);
    g_sd_random_bench.queue_avg_us = (uint32_t)(
        g_sd_random_bench.queue_sum_us / count);
    sd_bench_metric_finalize(&g_sd_random_bench.request_to_ready,
        g_sd_bench_latency_histogram);
    sd_bench_metric_finalize(&g_sd_random_bench.before_transaction,
        g_sd_bench_before_histogram);
    sd_bench_metric_finalize(&g_sd_random_bench.physical_transaction,
        g_sd_bench_transaction_histogram);
    sd_bench_metric_finalize(&g_sd_random_bench.after_transaction,
        g_sd_bench_after_histogram);
    sd_bench_metric_finalize(&g_sd_random_bench.request_to_backend_accept, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.backend_accept_to_map_start, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.physical_mapping, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.map_to_storage_submit, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.storage_submit, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.map_to_storage_accept, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.storage_accept_to_launch, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.launch_to_command, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.storage_accept_to_command, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.pre_cache_maintenance, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.command_response, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.data_transfer, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.stop_command, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.completion_processing, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.physical_complete_to_cache, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.cache_maintenance, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.cache_to_block_publish, 0);
    sd_bench_metric_finalize(
        &g_sd_random_bench.block_publish_to_backend_complete, 0);
    sd_bench_metric_finalize(&g_sd_random_bench.backend_complete_to_ready, 0);
    if (g_sd_random_bench.total_time_us != 0U)
    {
        g_sd_random_bench.pages_per_second_x1000 = (uint32_t)(
            ((uint64_t)count * UINT64_C(1000000000))
            / g_sd_random_bench.total_time_us);
        g_sd_random_bench.pages_per_second =
            (g_sd_random_bench.pages_per_second_x1000 + 500U) / 1000U;
        g_sd_random_bench.bytes_per_second =
            (g_sd_random_bench.total_bytes * UINT64_C(1000000))
            / g_sd_random_bench.total_time_us;
    }
    g_sd_random_bench.state = SD_RANDOM_BENCH_DONE;
    g_sd_random_bench.done = 1U;
}

void sd_random_bench_init(void)
{
    memset((void *)&g_sd_random_bench, 0, sizeof(g_sd_random_bench));
    memset(&g_sd_bench_runtime, 0, sizeof(g_sd_bench_runtime));
    memset(g_sd_bench_latency_histogram, 0,
           sizeof(g_sd_bench_latency_histogram));
    memset(g_sd_bench_transaction_histogram, 0,
           sizeof(g_sd_bench_transaction_histogram));
    memset(g_sd_bench_before_histogram, 0,
           sizeof(g_sd_bench_before_histogram));
    memset(g_sd_bench_after_histogram, 0,
           sizeof(g_sd_bench_after_histogram));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_sd_random_bench.magic = SD_RANDOM_BENCH_MAGIC;
    g_sd_random_bench.version = SD_RANDOM_BENCH_VERSION;
    g_sd_random_bench.size = sizeof(g_sd_random_bench);
    g_sd_random_bench.cpu_hz = SystemCoreClock;
    if (sd_bench_configure_sd_clock() == 0U)
    {
        g_sd_random_bench.error = SD_BENCH_ERROR_SUBMIT;
        g_sd_random_bench.fail_step = SD_RANDOM_BENCH_FAIL_MOUNT;
        g_sd_random_bench.errors = 1U;
        g_sd_random_bench.state = SD_RANDOM_BENCH_ERROR;
        g_sd_random_bench.done = 1U;
        return;
    }
    g_sd_random_bench.page_size = SD_BENCH_PAGE_BYTES;
    g_sd_random_bench.num_reads = SD_BENCH_READ_COUNT;
    g_sd_random_bench.latency_min_us = UINT32_MAX;
    g_sd_random_bench.transaction_min_us = UINT32_MAX;
    sd_bench_metric_init(&g_sd_random_bench.request_to_ready);
    sd_bench_metric_init(&g_sd_random_bench.before_transaction);
    sd_bench_metric_init(&g_sd_random_bench.physical_transaction);
    sd_bench_metric_init(&g_sd_random_bench.after_transaction);
    sd_bench_metric_init(&g_sd_random_bench.request_to_backend_accept);
    sd_bench_metric_init(&g_sd_random_bench.backend_accept_to_map_start);
    sd_bench_metric_init(&g_sd_random_bench.physical_mapping);
    sd_bench_metric_init(&g_sd_random_bench.map_to_storage_submit);
    sd_bench_metric_init(&g_sd_random_bench.storage_submit);
    sd_bench_metric_init(&g_sd_random_bench.map_to_storage_accept);
    sd_bench_metric_init(&g_sd_random_bench.storage_accept_to_launch);
    sd_bench_metric_init(&g_sd_random_bench.launch_to_command);
    sd_bench_metric_init(&g_sd_random_bench.storage_accept_to_command);
    sd_bench_metric_init(&g_sd_random_bench.pre_cache_maintenance);
    sd_bench_metric_init(&g_sd_random_bench.command_response);
    sd_bench_metric_init(&g_sd_random_bench.data_transfer);
    sd_bench_metric_init(&g_sd_random_bench.stop_command);
    sd_bench_metric_init(&g_sd_random_bench.completion_processing);
    sd_bench_metric_init(&g_sd_random_bench.physical_complete_to_cache);
    sd_bench_metric_init(&g_sd_random_bench.cache_maintenance);
    sd_bench_metric_init(&g_sd_random_bench.cache_to_block_publish);
    sd_bench_metric_init(
        &g_sd_random_bench.block_publish_to_backend_complete);
    sd_bench_metric_init(&g_sd_random_bench.backend_complete_to_ready);
    g_sd_random_bench.state = SD_RANDOM_BENCH_CREATING_FILE;
    sd_bench_snapshot_media();
}

void sd_random_bench_service(void)
{
    switch ((sd_random_bench_state_t)g_sd_random_bench.state)
    {
        case SD_RANDOM_BENCH_CREATING_FILE:
            sd_bench_prepare_file();
            break;
        case SD_RANDOM_BENCH_RUNNING:
            if (g_sd_bench_runtime.io_active == 0U)
            {
                (void)sd_bench_begin_read();
            }
            else
            {
                sd_scheduler_runtime_service();
                sd_bench_record_read();
            }
            break;
        case SD_RANDOM_BENCH_SORTING:
            sd_bench_finalize();
            break;
        case SD_RANDOM_BENCH_DONE:
        case SD_RANDOM_BENCH_ERROR:
        default:
            __WFI();
            break;
    }
}
