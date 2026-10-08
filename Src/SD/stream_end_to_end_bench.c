#include "SD/stream_end_to_end_bench.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Sampler/multi_sample_config.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_cache_config.h"
#include "Sampler/sample_page_cache_port.h"
#include "Sampler/sample_stream_fatfs_map.h"
#include "Sampler/sample_voice_reader.h"
#include "SD/sd_diskio.h"
#include "SD/sd_block_device.h"
#include "Storage/audio_recorder_wav.h"
#include "Storage/brick6_stream_service_task.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_parser.h"
#include "ff.h"
#include "sdmmc.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

/* One firmware image measures one explicit configuration. */
#ifndef STREAM_E2E_BENCH_NUM_REQUESTS
#define STREAM_E2E_BENCH_NUM_REQUESTS (10000U)
#endif
#ifndef STREAM_E2E_BENCH_SIMULTANEOUS_COLD_STARTS
#define STREAM_E2E_BENCH_SIMULTANEOUS_COLD_STARTS (1U)
#endif
#ifndef STREAM_E2E_BENCH_SD_CLOCK_DIVIDER
#define STREAM_E2E_BENCH_SD_CLOCK_DIVIDER (4U)
#endif
#ifndef STREAM_E2E_BENCH_WARMUP_PAGES
#define STREAM_E2E_BENCH_WARMUP_PAGES SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT
#endif

#define STREAM_E2E_BENCH_PATH "0:/BRICK/TEST/STREAM_E2E.B6T"
#define STREAM_E2E_BENCH_FILE_512_MIB (UINT32_C(512) * 1024U * 1024U)
#define STREAM_E2E_BENCH_FILE_256_MIB (UINT32_C(256) * 1024U * 1024U)
#define STREAM_E2E_BENCH_PAGE_BYTES SAMPLE_PAGE_BYTES
#define STREAM_E2E_BENCH_FILE_WRITE_BYTES (16U * 1024U)
#define STREAM_E2E_BENCH_MAX_PAGES \
    (STREAM_E2E_BENCH_FILE_512_MIB / STREAM_E2E_BENCH_PAGE_BYTES)
#define STREAM_E2E_BENCH_HIST_BIN_US (2U)
#define STREAM_E2E_BENCH_HIST_BINS (4096U)
#define STREAM_E2E_BENCH_HIST_COUNT (6U)
#define STREAM_E2E_BENCH_KEY_ID (MULTI_SAMPLE_MAX_SAMPLES - 1U)
#define STREAM_E2E_AUDIO_BLOCK_FRAMES (64U)
#define STREAM_E2E_SAMPLE_RATE_HZ (48000U)

#if ((STREAM_E2E_BENCH_SIMULTANEOUS_COLD_STARTS != 1U) \
     && (STREAM_E2E_BENCH_SIMULTANEOUS_COLD_STARTS != 2U) \
     && (STREAM_E2E_BENCH_SIMULTANEOUS_COLD_STARTS != 4U) \
     && (STREAM_E2E_BENCH_SIMULTANEOUS_COLD_STARTS != 8U))
#error "simultaneous cold starts must be 1, 2, 4 or 8"
#endif

_Static_assert(STREAM_E2E_BENCH_PAGE_BYTES == SAMPLE_PAGE_BYTES,
               "truth benchmark page size must equal the real Page Cache page");
_Static_assert((STREAM_E2E_BENCH_PAGE_BYTES
                % STREAM_E2E_BENCH_FILE_WRITE_BYTES) == 0U,
               "file preparation chunks must divide the Page Cache page");
_Static_assert(STREAM_E2E_BENCH_MAX_PAGES <= UINT16_MAX,
               "page permutation uses 16-bit indices");

enum
{
    STREAM_E2E_ERROR_NONE = 0,
    STREAM_E2E_ERROR_GATE,
    STREAM_E2E_ERROR_MOUNT,
    STREAM_E2E_ERROR_FILE,
    STREAM_E2E_ERROR_MAP,
    STREAM_E2E_ERROR_REGISTER,
    STREAM_E2E_ERROR_AUDIO_BIND,
    STREAM_E2E_ERROR_IO
};

enum
{
    STREAM_E2E_HIST_PAGE_READY = 0,
    STREAM_E2E_HIST_FIRST_RENDER,
    STREAM_E2E_HIST_BATCH_READY,
    STREAM_E2E_HIST_BATCH_RENDER,
    STREAM_E2E_HIST_PHYSICAL,
    STREAM_E2E_HIST_DATA
};

typedef struct
{
    uint32_t page;
    uint32_t trigger;
    uint32_t need_publish;
    uint32_t storage_seen;
    uint32_t manager_pick_begin;
    uint32_t manager_pick_end;
    uint32_t lookup_cycles;
    uint32_t reserve_begin;
    uint32_t reserve_end;
    uint32_t backend_submit;
    uint32_t page_ready;
    uint32_t audio_seen;
    uint32_t resolve_begin;
    uint32_t resolve_end;
    uint32_t first_render;
    uint32_t recycle_cycles;
    sample_stream_io_timing_trace_t physical;
    uint8_t allocated;
    uint8_t recycled;
    uint8_t rendered;
    uint8_t reserved;
} stream_e2e_voice_runtime_t;

typedef struct
{
    persistent_fatfs_file_t file;
    sample_stream_safe_metadata_t metadata;
    sample_audio_key_t key;
    sample_voice_reader_t reader[STREAM_END_TO_END_BENCH_MAX_BATCH];
    stream_e2e_voice_runtime_t voice[STREAM_END_TO_END_BENCH_MAX_BATCH];
    uint32_t create_offset;
    uint32_t prng;
    uint32_t registration_epoch;
    uint32_t permutation_cursor;
    uint32_t warmup_submitted;
    uint16_t page_order[STREAM_E2E_BENCH_MAX_PAGES];
    uint16_t page_count;
    volatile uint8_t trigger_pending;
    volatile uint8_t batch_active;
    volatile uint8_t batch_complete;
    uint8_t batch_size;
    uint8_t batch_is_warmup;
    uint8_t gate_held;
    uint8_t file_open;
    volatile float render_sink;
} stream_e2e_runtime_t;

SDRAM_STREAM_SERVICE volatile stream_end_to_end_bench_result_t
    g_stream_end_to_end_bench;
SDRAM_STREAM_SERVICE static stream_e2e_runtime_t g_stream_e2e_runtime;
SDRAM_STREAM_SCRATCH static uint8_t
    g_stream_e2e_file_buffer[STREAM_E2E_BENCH_PAGE_BYTES];
SDRAM_STREAM_SCRATCH static uint32_t
    g_stream_e2e_hist[STREAM_E2E_BENCH_HIST_COUNT]
                      [STREAM_E2E_BENCH_HIST_BINS];

static uint32_t stream_e2e_now(void)
{
    return DWT->CYCCNT;
}

static uint32_t stream_e2e_cycles_to_us(uint32_t cycles)
{
    const uint32_t hz = (g_stream_end_to_end_bench.cpu_hz != 0U)
        ? g_stream_end_to_end_bench.cpu_hz : 1U;
    return (uint32_t)(((uint64_t)cycles * UINT64_C(1000000)
                       + (hz / 2U)) / hz);
}

static void stream_e2e_metric_init(volatile stream_end_to_end_metric_t *metric)
{
    memset((void *)metric, 0, sizeof(*metric));
    metric->min_cycles = UINT32_MAX;
    metric->min_us = UINT32_MAX;
}

static void stream_e2e_metric_add(
    volatile stream_end_to_end_metric_t *metric, uint32_t cycles)
{
    const uint32_t us = stream_e2e_cycles_to_us(cycles);
    ++metric->count;
    metric->sum_cycles += cycles;
    if (cycles < metric->min_cycles) metric->min_cycles = cycles;
    if (cycles > metric->max_cycles) metric->max_cycles = cycles;
    metric->sum_us += us;
    if (us < metric->min_us) metric->min_us = us;
    if (us > metric->max_us) metric->max_us = us;
}

static void stream_e2e_hist_add(uint32_t which, uint32_t cycles)
{
    uint32_t bin = stream_e2e_cycles_to_us(cycles)
        / STREAM_E2E_BENCH_HIST_BIN_US;
    if (bin >= STREAM_E2E_BENCH_HIST_BINS)
        bin = STREAM_E2E_BENCH_HIST_BINS - 1U;
    ++g_stream_e2e_hist[which][bin];
}

static uint32_t stream_e2e_percentile(const uint32_t *histogram,
                                      uint32_t count, uint32_t permille)
{
    uint32_t rank = (uint32_t)(((uint64_t)count * permille + 999U) / 1000U);
    if (rank == 0U) rank = 1U;
    uint32_t cumulative = 0U;
    for (uint32_t bin = 0U; bin < STREAM_E2E_BENCH_HIST_BINS; ++bin)
    {
        cumulative += histogram[bin];
        if (cumulative >= rank)
            return bin * STREAM_E2E_BENCH_HIST_BIN_US;
    }
    return (STREAM_E2E_BENCH_HIST_BINS - 1U)
        * STREAM_E2E_BENCH_HIST_BIN_US;
}

static void stream_e2e_metric_finalize(
    volatile stream_end_to_end_metric_t *metric, const uint32_t *histogram)
{
    if (metric->count == 0U)
    {
        metric->min_cycles = 0U;
        metric->min_us = 0U;
        return;
    }
    metric->average_us = (uint32_t)(metric->sum_us / metric->count);
    if (histogram == NULL) return;
    metric->p50_us = stream_e2e_percentile(histogram, metric->count, 500U);
    metric->p90_us = stream_e2e_percentile(histogram, metric->count, 900U);
    metric->p95_us = stream_e2e_percentile(histogram, metric->count, 950U);
    metric->p99_us = stream_e2e_percentile(histogram, metric->count, 990U);
    metric->p999_us = stream_e2e_percentile(histogram, metric->count, 999U);
}

static uint8_t stream_e2e_key_matches(sample_audio_key_t key)
{
    return sample_audio_key_equal(&key, &g_stream_e2e_runtime.key);
}

static stream_e2e_voice_runtime_t *stream_e2e_find_voice(
    sample_audio_key_t key, uint32_t page)
{
    if ((g_stream_e2e_runtime.batch_active == 0U)
        || (stream_e2e_key_matches(key) == 0U)) return NULL;
    for (uint32_t i = 0U; i < g_stream_e2e_runtime.batch_size; ++i)
    {
        if (g_stream_e2e_runtime.voice[i].page == page)
            return &g_stream_e2e_runtime.voice[i];
    }
    return NULL;
}

static void stream_e2e_release_gate(void)
{
    if (g_stream_e2e_runtime.gate_held != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        g_stream_e2e_runtime.gate_held = 0U;
    }
}

static void stream_e2e_fail(uint32_t error, uint32_t step, FRESULT fr,
                            sd_block_device_result_t block_result)
{
    if (g_stream_e2e_runtime.file_open != 0U)
    {
        persistent_fatfs_close(&g_stream_e2e_runtime.file);
        g_stream_e2e_runtime.file_open = 0U;
    }
    stream_e2e_release_gate();
    g_stream_end_to_end_bench.error = error;
    g_stream_end_to_end_bench.fail_step = step;
    g_stream_end_to_end_bench.last_fresult = (int32_t)fr;
    g_stream_end_to_end_bench.last_block_result = (uint32_t)block_result;
    g_stream_end_to_end_bench.state = STREAM_END_TO_END_BENCH_ERROR;
    g_stream_end_to_end_bench.done = 1U;
}

static uint8_t stream_e2e_configure_sd_clock(void)
{
    const uint32_t kernel_hz =
        HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
    if (kernel_hz == 0U) return 0U;
    hsd1.Init.ClockDiv = STREAM_E2E_BENCH_SD_CLOCK_DIVIDER;
    MODIFY_REG(hsd1.Instance->CLKCR, SDMMC_CLKCR_CLKDIV,
               STREAM_E2E_BENCH_SD_CLOCK_DIVIDER);
    __DSB();
    g_stream_end_to_end_bench.sd_clock_divider =
        (hsd1.Instance->CLKCR & SDMMC_CLKCR_CLKDIV_Msk)
            >> SDMMC_CLKCR_CLKDIV_Pos;
    if (g_stream_end_to_end_bench.sd_clock_divider == 0U) return 0U;
    g_stream_end_to_end_bench.sd_clock_hz = kernel_hz
        / (2U * g_stream_end_to_end_bench.sd_clock_divider);
    return 1U;
}

static uint32_t stream_e2e_prng(void)
{
    uint32_t x = g_stream_e2e_runtime.prng;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    g_stream_e2e_runtime.prng = x;
    return x;
}

static void stream_e2e_shuffle(void)
{
    const uint16_t count = g_stream_e2e_runtime.page_count;
    for (uint16_t i = 0U; i < count; ++i)
        g_stream_e2e_runtime.page_order[i] = i;
    for (uint32_t i = count; i > 1U; --i)
    {
        const uint32_t j = stream_e2e_prng() % i;
        const uint16_t tmp = g_stream_e2e_runtime.page_order[i - 1U];
        g_stream_e2e_runtime.page_order[i - 1U] =
            g_stream_e2e_runtime.page_order[j];
        g_stream_e2e_runtime.page_order[j] = tmp;
    }
    g_stream_e2e_runtime.permutation_cursor = 0U;
}

static FRESULT stream_e2e_make_dirs(void)
{
    FRESULT fr = f_mkdir("0:/BRICK");
    if ((fr != FR_OK) && (fr != FR_EXIST)) return fr;
    fr = f_mkdir("0:/BRICK/TEST");
    return (fr == FR_EXIST) ? FR_OK : fr;
}

static void stream_e2e_fill_file_pattern(void)
{
    uint32_t x = UINT32_C(0x6B8B4567);
    uint32_t *const words = (uint32_t *)(void *)g_stream_e2e_file_buffer;
    for (uint32_t i = 0U;
         i < (STREAM_E2E_BENCH_PAGE_BYTES / sizeof(uint32_t)); ++i)
    {
        x ^= x << 13U;
        x ^= x >> 17U;
        x ^= x << 5U;
        words[i] = x ^ i;
    }
}

static uint8_t stream_e2e_file_is_canonical(void)
{
    wav_info_t info;
    if (persistent_fatfs_open_read(&g_stream_e2e_runtime.file,
                                   STREAM_E2E_BENCH_PATH) == 0U)
        return 0U;
    g_stream_e2e_runtime.file_open = 1U;
    const uint8_t valid = (uint8_t)(
        wav_parser_parse_info(&g_stream_e2e_runtime.file.file, &info)
        && (wav_parser_is_canonical_brick_float(&info) != 0U)
        && (info.data_size == (g_stream_end_to_end_bench.file_size
                              - AUDIO_RECORDER_WAV_HEADER_BYTES)));
    const FRESULT close_fr = persistent_fatfs_close_result(
        &g_stream_e2e_runtime.file);
    g_stream_e2e_runtime.file_open = 0U;
    return (uint8_t)((valid != 0U) && (close_fr == FR_OK));
}

static uint8_t stream_e2e_open_map_register(void)
{
    if (persistent_fatfs_open_read(&g_stream_e2e_runtime.file,
                                   STREAM_E2E_BENCH_PATH) == 0U)
        return 0U;
    g_stream_e2e_runtime.file_open = 1U;
    wav_info_t info;
    const uint8_t parsed = (uint8_t)(
        wav_parser_parse_info(&g_stream_e2e_runtime.file.file, &info)
        && (wav_parser_is_canonical_brick_float(&info) != 0U));
    const uint32_t total_frames = parsed != 0U
        ? (info.data_size / info.block_align) : 0U;
    const uint8_t registered = (uint8_t)(
        (parsed != 0U)
        && (sample_page_cache_port_register_file(
                g_stream_e2e_runtime.key, STREAM_E2E_BENCH_PATH, &info,
                total_frames, info.data_offset,
                &g_stream_e2e_runtime.file.file) != 0U));
    sample_page_stream_load_info_t load_info;
    const uint8_t loaded = (uint8_t)(
        (registered != 0U)
        && (sample_page_cache_get_stream_load_info_key(
                g_stream_e2e_runtime.key, &load_info) != 0U));
    const FRESULT close_fr = persistent_fatfs_close_result(
        &g_stream_e2e_runtime.file);
    g_stream_e2e_runtime.file_open = 0U;
    if ((loaded == 0U) || (close_fr != FR_OK)) return 0U;
    g_stream_e2e_runtime.metadata = load_info.stream_safe;
    g_stream_e2e_runtime.registration_epoch = load_info.registration_epoch;
    g_stream_e2e_runtime.page_count = (uint16_t)(
        info.data_size / STREAM_E2E_BENCH_PAGE_BYTES);
    g_stream_end_to_end_bench.page_count = g_stream_e2e_runtime.page_count;
    stream_e2e_release_gate();
    g_stream_e2e_runtime.prng = UINT32_C(0x9E3779B9);
    stream_e2e_shuffle();
    g_stream_end_to_end_bench.state =
        (STREAM_E2E_BENCH_WARMUP_PAGES != 0U)
            ? STREAM_END_TO_END_BENCH_WARMING_CACHE
            : STREAM_END_TO_END_BENCH_RUNNING;
    g_stream_end_to_end_bench.progress = 0U;
    g_stream_end_to_end_bench.progress_total =
        STREAM_E2E_BENCH_NUM_REQUESTS;
    return 1U;
}

static void stream_e2e_prepare_file(void)
{
    if (g_stream_e2e_runtime.gate_held == 0U)
    {
        if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
            return;
        g_stream_e2e_runtime.gate_held = 1U;
        if (sd_access_storage_status() != SD_STORAGE_STATUS_READY)
        {
            stream_e2e_fail(STREAM_E2E_ERROR_MOUNT, 1U, FR_NOT_READY,
                            SD_BLOCK_DEVICE_OK);
            return;
        }
        const FRESULT mkdir_fr = stream_e2e_make_dirs();
        if (mkdir_fr != FR_OK)
        {
            stream_e2e_fail(STREAM_E2E_ERROR_FILE, 2U, mkdir_fr,
                            SD_BLOCK_DEVICE_OK);
            return;
        }
        FILINFO info;
        const FRESULT stat_fr = f_stat(STREAM_E2E_BENCH_PATH, &info);
        if ((stat_fr == FR_OK)
            && ((info.fsize == STREAM_E2E_BENCH_FILE_512_MIB)
                || (info.fsize == STREAM_E2E_BENCH_FILE_256_MIB)))
        {
            g_stream_end_to_end_bench.file_size = (uint32_t)info.fsize;
            if (stream_e2e_file_is_canonical() != 0U)
            {
                if (stream_e2e_open_map_register() == 0U)
                    stream_e2e_fail(STREAM_E2E_ERROR_REGISTER, 3U,
                                    FR_INT_ERR, SD_BLOCK_DEVICE_OK);
                return;
            }
        }
        if ((stat_fr != FR_OK) && (stat_fr != FR_NO_FILE))
        {
            stream_e2e_fail(STREAM_E2E_ERROR_FILE, 4U, stat_fr,
                            SD_BLOCK_DEVICE_OK);
            return;
        }
        if (stat_fr == FR_OK) (void)f_unlink(STREAM_E2E_BENCH_PATH);
        DWORD free_clusters = 0U;
        FATFS *free_fs = NULL;
        const FRESULT free_fr = f_getfree("0:", &free_clusters, &free_fs);
        if ((free_fr != FR_OK) || (free_fs == NULL))
        {
            stream_e2e_fail(STREAM_E2E_ERROR_FILE, 5U, free_fr,
                            SD_BLOCK_DEVICE_OK);
            return;
        }
        const uint64_t free_bytes = (uint64_t)free_clusters
            * free_fs->csize * 512U;
        if (free_bytes >= STREAM_E2E_BENCH_FILE_512_MIB)
            g_stream_end_to_end_bench.file_size =
                STREAM_E2E_BENCH_FILE_512_MIB;
        else if (free_bytes >= STREAM_E2E_BENCH_FILE_256_MIB)
            g_stream_end_to_end_bench.file_size =
                STREAM_E2E_BENCH_FILE_256_MIB;
        else
        {
            stream_e2e_fail(STREAM_E2E_ERROR_FILE, 6U, FR_DENIED,
                            SD_BLOCK_DEVICE_OK);
            return;
        }
        const FRESULT open_fr = persistent_fatfs_open_write_result(
            &g_stream_e2e_runtime.file, STREAM_E2E_BENCH_PATH);
        if (open_fr != FR_OK)
        {
            stream_e2e_fail(STREAM_E2E_ERROR_FILE, 7U, open_fr,
                            SD_BLOCK_DEVICE_OK);
            return;
        }
        g_stream_e2e_runtime.file_open = 1U;
        stream_e2e_fill_file_pattern();
        (void)audio_recorder_wav_build_header(
            g_stream_e2e_file_buffer,
            g_stream_end_to_end_bench.file_size
                - AUDIO_RECORDER_WAV_HEADER_BYTES,
            STREAM_E2E_SAMPLE_RATE_HZ, 2U);
        g_stream_end_to_end_bench.progress_total =
            g_stream_end_to_end_bench.file_size;
    }

    if (g_stream_e2e_runtime.create_offset
        < g_stream_end_to_end_bench.file_size)
    {
        const persist_codec_sink_t sink = persistent_fatfs_sink(
            &g_stream_e2e_runtime.file);
        const uint8_t wrote = sink.write(
            sink.context, g_stream_e2e_file_buffer,
            STREAM_E2E_BENCH_FILE_WRITE_BYTES);
        const FRESULT fr = g_stream_e2e_runtime.file.last_result;
        if ((wrote == 0U)
            || (g_stream_e2e_runtime.file.transferred
                != STREAM_E2E_BENCH_FILE_WRITE_BYTES))
        {
            stream_e2e_fail(STREAM_E2E_ERROR_FILE, 8U, fr,
                            SD_BLOCK_DEVICE_OK);
            return;
        }
        g_stream_e2e_runtime.create_offset +=
            g_stream_e2e_runtime.file.transferred;
        if (g_stream_e2e_runtime.create_offset
            == STREAM_E2E_BENCH_FILE_WRITE_BYTES)
            stream_e2e_fill_file_pattern();
        g_stream_end_to_end_bench.progress =
            g_stream_e2e_runtime.create_offset;
        return;
    }

    FRESULT fr = f_sync(&g_stream_e2e_runtime.file.file);
    if (fr == FR_OK)
        fr = persistent_fatfs_close_result(&g_stream_e2e_runtime.file);
    g_stream_e2e_runtime.file_open = 0U;
    if (fr != FR_OK)
    {
        stream_e2e_fail(STREAM_E2E_ERROR_FILE, 9U, fr,
                        SD_BLOCK_DEVICE_OK);
        return;
    }
    if (stream_e2e_open_map_register() == 0U)
        stream_e2e_fail(STREAM_E2E_ERROR_REGISTER, 10U, FR_INT_ERR,
                        SD_BLOCK_DEVICE_OK);
}

static uint32_t stream_e2e_next_page(void)
{
    if (g_stream_e2e_runtime.permutation_cursor
        >= g_stream_e2e_runtime.page_count)
        stream_e2e_shuffle();
    return g_stream_e2e_runtime.page_order[
        g_stream_e2e_runtime.permutation_cursor++];
}

static uint8_t stream_e2e_page_is_one_physical_transaction(uint32_t page)
{
    sample_stream_physical_cursor_t cursor;
    sample_stream_physical_span_t span;
    memset(&cursor, 0, sizeof(cursor));
    return (uint8_t)((sample_stream_physical_map_resolve(
        &g_stream_e2e_runtime.metadata.physical_map,
        (uint64_t)g_stream_e2e_runtime.metadata.data_offset_bytes
            + (uint64_t)page * STREAM_E2E_BENCH_PAGE_BYTES,
        STREAM_E2E_BENCH_PAGE_BYTES, &cursor, &span) != 0U)
        && (span.first_sector_skip == 0U)
        && (span.logical_bytes == STREAM_E2E_BENCH_PAGE_BYTES)
        && (span.sector_count
            == (STREAM_E2E_BENCH_PAGE_BYTES / 512U)));
}

static uint8_t stream_e2e_prepare_batch(uint8_t warmup)
{
    uint32_t wanted = STREAM_E2E_BENCH_SIMULTANEOUS_COLD_STARTS;
    if (warmup != 0U)
    {
        const uint32_t remaining = STREAM_E2E_BENCH_WARMUP_PAGES
            - g_stream_e2e_runtime.warmup_submitted;
        if (wanted > remaining) wanted = remaining;
    }
    else
    {
        const uint32_t remaining = STREAM_E2E_BENCH_NUM_REQUESTS
            - g_stream_end_to_end_bench.voices_completed;
        if (wanted > remaining) wanted = remaining;
    }
    if (wanted == 0U) return 0U;

    memset(g_stream_e2e_runtime.voice, 0,
           sizeof(g_stream_e2e_runtime.voice));
    uint32_t selected = 0U;
    uint32_t attempts = 0U;
    while ((selected < wanted)
           && (attempts < g_stream_e2e_runtime.page_count))
    {
        ++attempts;
        const uint32_t page = stream_e2e_next_page();
        if (stream_e2e_page_is_one_physical_transaction(page) == 0U)
            continue;
        if (sample_page_cache_get_page_state_key(
                g_stream_e2e_runtime.key, page) != SAMPLE_PAGE_FREE)
        {
            ++g_stream_end_to_end_bench.cache_hits;
            continue;
        }
        uint8_t duplicate = 0U;
        for (uint32_t i = 0U; i < selected; ++i)
            if (g_stream_e2e_runtime.voice[i].page == page) duplicate = 1U;
        if (duplicate != 0U) continue;
        g_stream_e2e_runtime.voice[selected++].page = page;
    }
    if (selected != wanted)
    {
        stream_e2e_fail(STREAM_E2E_ERROR_MAP, 14U, FR_INT_ERR,
                        SD_BLOCK_DEVICE_INVALID_ARG);
        return 0U;
    }
    g_stream_e2e_runtime.batch_size = (uint8_t)wanted;
    g_stream_e2e_runtime.batch_is_warmup = warmup;
    g_stream_e2e_runtime.trigger_pending = 1U;
    __DMB();
    return 1U;
}

static void stream_e2e_insert_outlier(
    const stream_e2e_voice_runtime_t *voice, uint8_t batch_size)
{
    stream_end_to_end_outlier_t item;
    memset(&item, 0, sizeof(item));
    item.target_page = voice->page;
    item.batch_size = batch_size;
    item.used_free_slot = (uint8_t)(voice->allocated && !voice->recycled);
    item.used_recycle = voice->recycled;
    item.trigger_to_storage_us = stream_e2e_cycles_to_us(
        voice->storage_seen - voice->trigger);
    item.manager_cache_us = stream_e2e_cycles_to_us(
        (voice->manager_pick_end - voice->manager_pick_begin)
        + voice->lookup_cycles + (voice->reserve_end - voice->reserve_begin));
    item.trigger_to_backend_submit_us = stream_e2e_cycles_to_us(
        voice->backend_submit - voice->trigger);
    item.physical_transaction_us = stream_e2e_cycles_to_us(
        voice->physical.physical_complete_cycles
        - voice->physical.command_cycles);
    item.trigger_to_page_ready_us = stream_e2e_cycles_to_us(
        voice->page_ready - voice->trigger);
    item.page_ready_to_audio_seen_us = stream_e2e_cycles_to_us(
        voice->audio_seen - voice->page_ready);
    item.trigger_to_first_render_us = stream_e2e_cycles_to_us(
        voice->first_render - voice->trigger);

    uint32_t count = g_stream_end_to_end_bench.outlier_count;
    if (count < STREAM_END_TO_END_BENCH_OUTLIERS)
        ++g_stream_end_to_end_bench.outlier_count;
    else if (item.trigger_to_first_render_us
             <= g_stream_end_to_end_bench.outliers[count - 1U]
                    .trigger_to_first_render_us)
        return;
    if (count >= STREAM_END_TO_END_BENCH_OUTLIERS)
        count = STREAM_END_TO_END_BENCH_OUTLIERS - 1U;
    while ((count > 0U)
           && (item.trigger_to_first_render_us
               > g_stream_end_to_end_bench.outliers[count - 1U]
                     .trigger_to_first_render_us))
    {
        if (count < STREAM_END_TO_END_BENCH_OUTLIERS)
            g_stream_end_to_end_bench.outliers[count] =
                g_stream_end_to_end_bench.outliers[count - 1U];
        --count;
    }
    g_stream_end_to_end_bench.outliers[count] = item;
}

#define STREAM_E2E_ADD(metric_, end_, begin_) \
    do { if ((end_) != 0U && (begin_) != 0U) \
        stream_e2e_metric_add(&(metric_), (end_) - (begin_)); } while (0)

static void stream_e2e_record_physical(
    const stream_e2e_voice_runtime_t *voice)
{
    const sample_stream_io_timing_trace_t *const t = &voice->physical;
    STREAM_E2E_ADD(g_stream_end_to_end_bench.request_to_backend_accept,
                   t->backend_accept_cycles, voice->backend_submit);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.backend_accept_to_map_start,
                   t->map_start_cycles, t->backend_accept_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.physical_mapping,
                   t->map_end_cycles, t->map_start_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.map_to_storage_submit,
                   t->storage_submit_enter_cycles, t->map_end_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.storage_submit,
                   t->storage_accept_cycles, t->storage_submit_enter_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.storage_accept_to_launch,
                   t->launch_enter_cycles, t->storage_accept_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.pre_cache_maintenance,
                   t->pre_cache_end_cycles, t->pre_cache_start_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.command_response,
                   t->data_start_cycles, t->command_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.data_transfer,
                   t->data_end_cycles, t->data_start_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.stop_command,
                   t->physical_complete_cycles, t->data_end_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.physical_complete_to_cache,
                   t->cache_start_cycles, t->physical_complete_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.cache_maintenance,
                   t->cache_end_cycles, t->cache_start_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.block_publish,
                   t->block_publish_cycles, t->cache_end_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.backend_complete,
                   t->backend_complete_cycles, t->block_publish_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.backend_submit_to_physical_start,
                   t->command_cycles, voice->backend_submit);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.physical_transaction,
                   t->physical_complete_cycles, t->command_cycles);
    STREAM_E2E_ADD(g_stream_end_to_end_bench.physical_complete_to_page_ready,
                   voice->page_ready, t->physical_complete_cycles);
    if ((t->physical_complete_cycles != 0U) && (t->command_cycles != 0U))
        stream_e2e_hist_add(STREAM_E2E_HIST_PHYSICAL,
                            t->physical_complete_cycles - t->command_cycles);
    if ((t->data_end_cycles != 0U) && (t->data_start_cycles != 0U))
        stream_e2e_hist_add(STREAM_E2E_HIST_DATA,
                            t->data_end_cycles - t->data_start_cycles);
}

static void stream_e2e_record_batch(void)
{
    uint32_t first_ready_delta = UINT32_MAX;
    uint32_t last_ready_delta = 0U;
    uint32_t first_render_delta = UINT32_MAX;
    uint32_t last_render_delta = 0U;
    const uint8_t warmup = g_stream_e2e_runtime.batch_is_warmup;
    for (uint32_t i = 0U; i < g_stream_e2e_runtime.batch_size; ++i)
    {
        const stream_e2e_voice_runtime_t *const v =
            &g_stream_e2e_runtime.voice[i];
        if ((v->page_ready == 0U) || (v->first_render == 0U))
        {
            stream_e2e_fail(STREAM_E2E_ERROR_IO, 11U, FR_INT_ERR,
                            SD_BLOCK_DEVICE_READ_FAIL);
            return;
        }
        const uint32_t ready_delta = v->page_ready - v->trigger;
        const uint32_t render_delta = v->first_render - v->trigger;
        if (ready_delta < first_ready_delta) first_ready_delta = ready_delta;
        if (ready_delta > last_ready_delta) last_ready_delta = ready_delta;
        if (render_delta < first_render_delta) first_render_delta = render_delta;
        if (render_delta > last_render_delta) last_render_delta = render_delta;
        if (warmup == 0U)
        {
            STREAM_E2E_ADD(g_stream_end_to_end_bench.trigger_to_need_publish,
                           v->need_publish, v->trigger);
            STREAM_E2E_ADD(g_stream_end_to_end_bench.need_publish_to_storage_seen,
                           v->storage_seen, v->need_publish);
            STREAM_E2E_ADD(g_stream_end_to_end_bench.storage_seen_to_manager_pick,
                           v->manager_pick_end, v->storage_seen);
            STREAM_E2E_ADD(g_stream_end_to_end_bench.manager_pick,
                           v->manager_pick_end, v->manager_pick_begin);
            stream_e2e_metric_add(&g_stream_end_to_end_bench.cache_lookup,
                                  v->lookup_cycles);
            STREAM_E2E_ADD(g_stream_end_to_end_bench.cache_reserve,
                           v->reserve_end, v->reserve_begin);
            if (v->recycle_cycles != 0U)
                stream_e2e_metric_add(
                    &g_stream_end_to_end_bench.cache_recycle_time,
                    v->recycle_cycles);
            STREAM_E2E_ADD(g_stream_end_to_end_bench.trigger_to_backend_submit,
                           v->backend_submit, v->trigger);
            STREAM_E2E_ADD(g_stream_end_to_end_bench.trigger_to_page_ready,
                           v->page_ready, v->trigger);
            STREAM_E2E_ADD(g_stream_end_to_end_bench.page_ready_to_audio_seen,
                           v->audio_seen, v->page_ready);
            STREAM_E2E_ADD(g_stream_end_to_end_bench.audio_seen_to_reader_resolve,
                           v->resolve_begin, v->audio_seen);
            STREAM_E2E_ADD(g_stream_end_to_end_bench.reader_resolve_to_first_render,
                           v->first_render, v->resolve_end);
            STREAM_E2E_ADD(g_stream_end_to_end_bench.trigger_to_first_render,
                           v->first_render, v->trigger);
            stream_e2e_hist_add(STREAM_E2E_HIST_PAGE_READY,
                                v->page_ready - v->trigger);
            stream_e2e_hist_add(STREAM_E2E_HIST_FIRST_RENDER,
                                v->first_render - v->trigger);
            stream_e2e_record_physical(v);
            stream_e2e_insert_outlier(v, g_stream_e2e_runtime.batch_size);
            const uint32_t render_us = stream_e2e_cycles_to_us(
                v->first_render - v->trigger);
            if (render_us <= g_stream_end_to_end_bench.audio_block_us)
                ++g_stream_end_to_end_bench.render_within_1_block;
            else
                ++g_stream_end_to_end_bench.render_missed_1_block;
        }
    }

    if (warmup != 0U)
    {
        g_stream_e2e_runtime.warmup_submitted +=
            g_stream_e2e_runtime.batch_size;
        g_stream_end_to_end_bench.warmup_pages_ready =
            g_stream_e2e_runtime.warmup_submitted;
    }
    else
    {
        stream_e2e_metric_add(&g_stream_end_to_end_bench.batch_all_ready,
                              last_ready_delta);
        stream_e2e_metric_add(&g_stream_end_to_end_bench.batch_all_rendered,
                              last_render_delta);
        stream_e2e_hist_add(STREAM_E2E_HIST_BATCH_READY,
                            last_ready_delta);
        stream_e2e_hist_add(STREAM_E2E_HIST_BATCH_RENDER,
                            last_render_delta);
        g_stream_end_to_end_bench.first_voice_ready_us =
            stream_e2e_cycles_to_us(first_ready_delta);
        g_stream_end_to_end_bench.last_voice_ready_us =
            stream_e2e_cycles_to_us(last_ready_delta);
        g_stream_end_to_end_bench.first_voice_rendered_us =
            stream_e2e_cycles_to_us(first_render_delta);
        g_stream_end_to_end_bench.last_voice_rendered_us =
            stream_e2e_cycles_to_us(last_render_delta);
        ++g_stream_end_to_end_bench.batches_completed;
        g_stream_end_to_end_bench.voices_completed +=
            g_stream_e2e_runtime.batch_size;
        g_stream_end_to_end_bench.progress =
            g_stream_end_to_end_bench.voices_completed;
        const uint32_t batch_us = stream_e2e_cycles_to_us(last_render_delta);
        if (batch_us <= g_stream_end_to_end_bench.audio_block_us)
            ++g_stream_end_to_end_bench.batch_all_rendered_within_1_block;
        else
            ++g_stream_end_to_end_bench.batch_missed_1_block;
    }
    g_stream_e2e_runtime.batch_complete = 0U;
    g_stream_e2e_runtime.batch_active = 0U;
    __DMB();
}

static void stream_e2e_finalize(void)
{
    stream_end_to_end_metric_t *const first =
        (stream_end_to_end_metric_t *)(void *)
            &g_stream_end_to_end_bench.trigger_to_need_publish;
    stream_end_to_end_metric_t *const last =
        (stream_end_to_end_metric_t *)(void *)
            &g_stream_end_to_end_bench.backend_complete;
    for (stream_end_to_end_metric_t *metric = first;
         metric <= last; ++metric)
        stream_e2e_metric_finalize(metric, NULL);
    stream_e2e_metric_finalize(
        &g_stream_end_to_end_bench.trigger_to_page_ready,
        g_stream_e2e_hist[STREAM_E2E_HIST_PAGE_READY]);
    stream_e2e_metric_finalize(
        &g_stream_end_to_end_bench.trigger_to_first_render,
        g_stream_e2e_hist[STREAM_E2E_HIST_FIRST_RENDER]);
    stream_e2e_metric_finalize(
        &g_stream_end_to_end_bench.batch_all_ready,
        g_stream_e2e_hist[STREAM_E2E_HIST_BATCH_READY]);
    stream_e2e_metric_finalize(
        &g_stream_end_to_end_bench.batch_all_rendered,
        g_stream_e2e_hist[STREAM_E2E_HIST_BATCH_RENDER]);
    stream_e2e_metric_finalize(
        &g_stream_end_to_end_bench.physical_transaction,
        g_stream_e2e_hist[STREAM_E2E_HIST_PHYSICAL]);
    stream_e2e_metric_finalize(
        &g_stream_end_to_end_bench.data_transfer,
        g_stream_e2e_hist[STREAM_E2E_HIST_DATA]);
    g_stream_end_to_end_bench.state = STREAM_END_TO_END_BENCH_DONE;
    g_stream_end_to_end_bench.done = 1U;
}

void stream_end_to_end_bench_init(void)
{
    memset((void *)&g_stream_end_to_end_bench, 0,
           sizeof(g_stream_end_to_end_bench));
    memset(&g_stream_e2e_runtime, 0, sizeof(g_stream_e2e_runtime));
    memset(g_stream_e2e_hist, 0, sizeof(g_stream_e2e_hist));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_stream_e2e_runtime.key = sample_audio_key_multi(
        STREAM_E2E_BENCH_KEY_ID);
    g_stream_end_to_end_bench.magic = STREAM_END_TO_END_BENCH_MAGIC;
    g_stream_end_to_end_bench.version = STREAM_END_TO_END_BENCH_VERSION;
    g_stream_end_to_end_bench.size = sizeof(g_stream_end_to_end_bench);
    g_stream_end_to_end_bench.cpu_hz = SystemCoreClock;
    g_stream_end_to_end_bench.page_size = STREAM_E2E_BENCH_PAGE_BYTES;
    g_stream_end_to_end_bench.num_requests =
        STREAM_E2E_BENCH_NUM_REQUESTS;
    g_stream_end_to_end_bench.simultaneous_cold_starts =
        STREAM_E2E_BENCH_SIMULTANEOUS_COLD_STARTS;
    g_stream_end_to_end_bench.audio_block_frames =
        STREAM_E2E_AUDIO_BLOCK_FRAMES;
    g_stream_end_to_end_bench.audio_block_us =
        (STREAM_E2E_AUDIO_BLOCK_FRAMES * 1000000U)
            / STREAM_E2E_SAMPLE_RATE_HZ;
    g_stream_end_to_end_bench.warmup_target_pages =
        STREAM_E2E_BENCH_WARMUP_PAGES;
    g_stream_end_to_end_bench.instrumentation_ram_bytes =
        sizeof(g_stream_end_to_end_bench) + sizeof(g_stream_e2e_runtime)
        + sizeof(g_stream_e2e_file_buffer) + sizeof(g_stream_e2e_hist)
        + (sizeof(sample_stream_io_timing_trace_t)
           * (SAMPLE_STREAM_IO_JOB_CAPACITY + 1U));

    stream_end_to_end_metric_t *const first =
        (stream_end_to_end_metric_t *)(void *)
            &g_stream_end_to_end_bench.trigger_to_need_publish;
    stream_end_to_end_metric_t *const last =
        (stream_end_to_end_metric_t *)(void *)
            &g_stream_end_to_end_bench.backend_complete;
    for (stream_end_to_end_metric_t *metric = first;
         metric <= last; ++metric)
        stream_e2e_metric_init(metric);
    if (stream_e2e_configure_sd_clock() == 0U)
    {
        stream_e2e_fail(STREAM_E2E_ERROR_IO, 0U, FR_INT_ERR,
                        SD_BLOCK_DEVICE_READ_FAIL);
        return;
    }
    g_stream_end_to_end_bench.state =
        STREAM_END_TO_END_BENCH_CREATING_FILE;
}

void stream_end_to_end_bench_service(void)
{
    switch ((stream_end_to_end_bench_state_t)
            g_stream_end_to_end_bench.state)
    {
        case STREAM_END_TO_END_BENCH_CREATING_FILE:
            stream_e2e_prepare_file();
            break;
        case STREAM_END_TO_END_BENCH_WARMING_CACHE:
        case STREAM_END_TO_END_BENCH_RUNNING:
            if (g_stream_e2e_runtime.batch_complete != 0U)
                stream_e2e_record_batch();
            if (g_stream_end_to_end_bench.state
                    == STREAM_END_TO_END_BENCH_WARMING_CACHE
                && g_stream_e2e_runtime.warmup_submitted
                    >= STREAM_E2E_BENCH_WARMUP_PAGES)
                g_stream_end_to_end_bench.state =
                    STREAM_END_TO_END_BENCH_RUNNING;
            if ((g_stream_end_to_end_bench.state
                    == STREAM_END_TO_END_BENCH_RUNNING)
                && (g_stream_end_to_end_bench.voices_completed
                    >= STREAM_E2E_BENCH_NUM_REQUESTS))
            {
                g_stream_end_to_end_bench.state =
                    STREAM_END_TO_END_BENCH_FINALIZING;
                break;
            }
            if ((g_stream_e2e_runtime.batch_active == 0U)
                && (g_stream_e2e_runtime.trigger_pending == 0U))
                (void)stream_e2e_prepare_batch(
                    (uint8_t)(g_stream_end_to_end_bench.state
                        == STREAM_END_TO_END_BENCH_WARMING_CACHE));
            if ((g_stream_e2e_runtime.batch_active != 0U)
                || (g_stream_e2e_runtime.trigger_pending != 0U))
            {
                ++g_stream_end_to_end_bench.manager_calls;
                brick6_stream_service_task_poll();
            }
            break;
        case STREAM_END_TO_END_BENCH_FINALIZING:
            stream_e2e_finalize();
            break;
        case STREAM_END_TO_END_BENCH_DONE:
        case STREAM_END_TO_END_BENCH_ERROR:
        default:
            __WFI();
            break;
    }
}

void stream_end_to_end_bench_audio_boundary(uint32_t frames)
{
    const uint32_t state = g_stream_end_to_end_bench.state;
    if ((state != STREAM_END_TO_END_BENCH_WARMING_CACHE)
        && (state != STREAM_END_TO_END_BENCH_RUNNING)) return;

    if (g_stream_e2e_runtime.trigger_pending != 0U)
    {
        for (uint32_t i = 0U; i < g_stream_e2e_runtime.batch_size; ++i)
        {
            if (sample_page_cache_audio_get_page_state_key(
                    g_stream_e2e_runtime.key,
                    g_stream_e2e_runtime.voice[i].page) != SAMPLE_PAGE_FREE)
            {
                ++g_stream_end_to_end_bench.cache_hits;
                ++g_stream_end_to_end_bench.rejected_non_cold_batches;
                g_stream_e2e_runtime.trigger_pending = 0U;
                return;
            }
        }
        const uint32_t trigger = stream_e2e_now();
        g_stream_e2e_runtime.batch_active = 1U;
        for (uint32_t i = 0U; i < g_stream_e2e_runtime.batch_size; ++i)
        {
            stream_e2e_voice_runtime_t *const voice =
                &g_stream_e2e_runtime.voice[i];
            sample_play_plan_t plan;
            sample_play_plan_init(&plan);
            const uint32_t start = voice->page * SAMPLE_PAGE_FRAMES;
            plan.key = g_stream_e2e_runtime.key;
            plan.sample_id = STREAM_E2E_BENCH_KEY_ID;
            plan.format = SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
            plan.stride_floats = 2U;
            plan.frames_per_page = SAMPLE_PAGE_FRAMES;
            plan.registration_epoch = g_stream_e2e_runtime.registration_epoch;
            plan.start_frame = start;
            plan.region_begin = start;
            plan.region_end = start + 1U;
            plan.loop_begin = start;
            plan.loop_end = start + 1U;
            plan.step_q16 = 65536U;
            plan.loop_mode = SAMPLE_PLAY_LOOP_NONE;
            plan.kernel_type = SAMPLE_KERNEL_FWD_1X;
            voice->trigger = trigger;
            if (sample_voice_reader_bind_play_plan_deferred(
                    &g_stream_e2e_runtime.reader[i], &plan,
                    (uint8_t)i) == 0U)
            {
                stream_e2e_fail(STREAM_E2E_ERROR_AUDIO_BIND, 12U,
                                FR_INT_ERR, SD_BLOCK_DEVICE_OK);
                return;
            }
            ++g_stream_end_to_end_bench.cache_misses;
        }
        g_stream_e2e_runtime.trigger_pending = 0U;
        __DMB();
    }

    if (g_stream_e2e_runtime.batch_active == 0U) return;
    uint32_t complete = 0U;
    for (uint32_t i = 0U; i < g_stream_e2e_runtime.batch_size; ++i)
    {
        stream_e2e_voice_runtime_t *const voice =
            &g_stream_e2e_runtime.voice[i];
        if (voice->rendered != 0U)
        {
            ++complete;
            continue;
        }
        if ((voice->audio_seen == 0U)
            && (sample_page_cache_audio_get_page_state_key(
                    g_stream_e2e_runtime.key,
                    voice->page) == SAMPLE_PAGE_READY))
            voice->audio_seen = stream_e2e_now();
        float out_l = 0.0f;
        float out_r = 0.0f;
        float last_l = 0.0f;
        float last_r = 0.0f;
        uint32_t rendered = 0U;
        (void)sample_voice_reader_render_fwd_1x_ready_simple(
            &g_stream_e2e_runtime.reader[i], 1.0f,
            &out_l, &out_r, 1U, 0U, &rendered, &last_l, &last_r);
        if (rendered != 0U)
        {
            g_stream_e2e_runtime.render_sink += out_l + out_r;
            voice->first_render = stream_e2e_now();
            voice->rendered = 1U;
            sample_voice_reader_stop(&g_stream_e2e_runtime.reader[i]);
            ++complete;
        }
        else if (voice->page_ready == 0U)
        {
            ++g_stream_end_to_end_bench.audio_page_missing;
        }
    }
    (void)frames;
    if (complete == g_stream_e2e_runtime.batch_size)
    {
        g_stream_e2e_runtime.batch_complete = 1U;
        __DMB();
    }
}

uint8_t stream_end_to_end_bench_probe_key(sample_audio_key_t key)
{
    const uint32_t state = g_stream_end_to_end_bench.state;
    return (uint8_t)(((state == STREAM_END_TO_END_BENCH_WARMING_CACHE)
        || (state == STREAM_END_TO_END_BENCH_RUNNING))
        && (g_stream_e2e_runtime.batch_active != 0U)
        && (stream_e2e_key_matches(key) != 0U));
}

void stream_end_to_end_bench_probe_need(sample_audio_key_t key,
                                        uint32_t page_index,
                                        uint32_t begin_cycles,
                                        uint32_t publish_cycles)
{
    (void)begin_cycles;
    stream_e2e_voice_runtime_t *const voice =
        stream_e2e_find_voice(key, page_index);
    if ((voice != NULL) && (voice->need_publish == 0U))
        voice->need_publish = publish_cycles;
}

void stream_end_to_end_bench_probe_resolve(sample_audio_key_t key,
                                           uint32_t page_index,
                                           uint32_t begin_cycles,
                                           uint32_t end_cycles,
                                           uint8_t acquired)
{
    stream_e2e_voice_runtime_t *const voice =
        stream_e2e_find_voice(key, page_index);
    if ((voice != NULL) && (acquired != 0U)
        && (voice->resolve_begin == 0U))
    {
        voice->resolve_begin = begin_cycles;
        voice->resolve_end = end_cycles;
    }
}

void stream_end_to_end_bench_probe_storage_seen(sample_audio_key_t key,
                                                uint32_t page_index,
                                                uint32_t cycles,
                                                uint32_t lookup_cycles,
                                                uint8_t cache_hit)
{
    (void)cache_hit;
    stream_e2e_voice_runtime_t *const voice =
        stream_e2e_find_voice(key, page_index);
    if ((voice != NULL) && (voice->storage_seen == 0U))
    {
        voice->storage_seen = cycles;
        voice->lookup_cycles = lookup_cycles;
    }
}

void stream_end_to_end_bench_probe_manager_pick(sample_audio_key_t key,
                                                uint32_t page_index,
                                                uint32_t begin_cycles,
                                                uint32_t end_cycles)
{
    stream_e2e_voice_runtime_t *const voice =
        stream_e2e_find_voice(key, page_index);
    if ((voice != NULL) && (voice->manager_pick_begin == 0U))
    {
        voice->manager_pick_begin = begin_cycles;
        voice->manager_pick_end = end_cycles;
    }
}

void stream_end_to_end_bench_probe_reserve(sample_audio_key_t key,
                                           uint32_t page_index,
                                           uint32_t begin_cycles,
                                           uint32_t end_cycles,
                                           uint8_t success)
{
    stream_e2e_voice_runtime_t *const voice =
        stream_e2e_find_voice(key, page_index);
    if (voice == NULL) return;
    ++g_stream_end_to_end_bench.reserve_calls;
    if (voice->reserve_begin == 0U)
    {
        voice->reserve_begin = begin_cycles;
        voice->reserve_end = end_cycles;
    }
    if (success == 0U) ++g_stream_end_to_end_bench.cache_alloc_fail;
    else ++g_stream_end_to_end_bench.pages_requested;
}

void stream_end_to_end_bench_probe_allocation(sample_audio_key_t key,
                                              uint32_t page_index,
                                              uint8_t recycled,
                                              uint32_t recycle_cycles)
{
    stream_e2e_voice_runtime_t *const voice =
        stream_e2e_find_voice(key, page_index);
    if (voice == NULL) return;
    voice->allocated = 1U;
    voice->recycled = recycled;
    voice->recycle_cycles = recycle_cycles;
    ++g_stream_end_to_end_bench.cache_alloc;
    if (recycled != 0U)
    {
        ++g_stream_end_to_end_bench.cache_recycle;
        ++g_stream_end_to_end_bench.recycle_calls;
        ++g_stream_end_to_end_bench.cold_with_recycle;
    }
    else
        ++g_stream_end_to_end_bench.cold_with_free_slot;
}

void stream_end_to_end_bench_probe_io_complete(
    const sample_stream_io_result_t *result,
    uint32_t page_ready_cycles)
{
    if (result == NULL) return;
    stream_e2e_voice_runtime_t *const voice = stream_e2e_find_voice(
        result->token.key, result->token.page_index);
    if (voice == NULL) return;
    if ((result->load_result != SAMPLE_PAGE_LOAD_OK)
        || (page_ready_cycles == 0U))
    {
        stream_e2e_fail(STREAM_E2E_ERROR_IO, 13U, FR_DISK_ERR,
                        SD_BLOCK_DEVICE_READ_FAIL);
        return;
    }
    voice->backend_submit = result->request_cycles;
    voice->physical = result->timing;
    voice->page_ready = page_ready_cycles;
    ++g_stream_end_to_end_bench.pages_ready;
}
