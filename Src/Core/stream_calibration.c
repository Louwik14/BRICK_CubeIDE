#include "Core/stream_calibration.h"

#if BRICK6_STREAM_CALIBRATION

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "Core/cpu_load.h"
#include "Sampler/sample_stream_benchmark.h"
#include "Sampler/sample_stream_needs.h"
#include "Sampler/sample_stream_scheduler.h"
#include "Sampler/sample_stream_time.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include "stm32h7xx.h"

#define CAL_SAMPLE_RATE    (48000U)
#define CAL_GRID_SIGNATURE (0x03102060UL)
#define CAL_CASE_COUNT     BRICK6_STREAM_CALIBRATION_CASES_PER_BUILD

typedef struct
{
    uint8_t passes;
    uint8_t advance_pages;
} cal_case_config_t;

#if BRICK6_STREAM_CALIBRATION_PAGE_KIB == 16
static const cal_case_config_t k_cases[CAL_CASE_COUNT] = {
    {2U, 2U},
    {2U, 4U},
    {2U, 6U},
};
#elif BRICK6_STREAM_CALIBRATION_PAGE_KIB == 32
static const cal_case_config_t k_cases[CAL_CASE_COUNT] = {
    {1U, 1U},
    {1U, 2U},
    {1U, 3U},
};
#else
#error "Manual stream calibration supports only 16 or 32 KiB pages"
#endif

STORAGE_STATE_SDRAM static brick6_stream_calibration_file_t g_file;
STORAGE_STATE_SDRAM static brick6_stream_calibration_file_t g_export_file;
static uint16_t g_case_index;
static uint16_t g_saved_mask;
static uint8_t g_measurement_active;
static uint8_t g_initialized;
static uint8_t g_export_pending;
static sample_stream_audio_frame_t g_case_start_frame;
static uint32_t g_last_select_frame[8];
static uint8_t g_last_select_valid[8];
static uint32_t g_round_begin_cycle;
static uint64_t g_round_cycles_total;
static uint32_t g_round_cycles_max;
static uint32_t g_round_count;
static uint8_t g_round_had_select;
static uint32_t g_contiguous_reads;
static uint32_t g_fatfs_reads;

static uint8_t cal_passes_for(uint16_t case_index)
{
    return k_cases[case_index].passes;
}

static uint8_t cal_advance_for(uint16_t case_index)
{
    return k_cases[case_index].advance_pages;
}

static brick6_stream_calibration_result_t *cal_current(void)
{
    return &g_file.results[g_case_index];
}

static uint32_t cal_crc32(const void *data, uint32_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0U; i < size; ++i)
    {
        crc ^= bytes[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
        }
    }
    return ~crc;
}

static uint32_t cal_histogram_p99_upper(const uint32_t histogram[32], uint32_t samples)
{
    if (samples == 0U)
    {
        return 0U;
    }
    const uint32_t target = (samples * 99U + 99U) / 100U;
    uint32_t accumulated = 0U;
    for (uint8_t bucket = 0U; bucket < 32U; ++bucket)
    {
        accumulated += histogram[bucket];
        if (accumulated >= target)
        {
            return (bucket >= 31U) ? UINT32_MAX : (1UL << bucket);
        }
    }
    return UINT32_MAX;
}

static void cal_init_result(uint16_t case_index)
{
    brick6_stream_calibration_result_t *const result = &g_file.results[case_index];
    memset(result, 0, sizeof(*result));
    result->page_kib = BRICK6_STREAM_CALIBRATION_PAGE_KIB;
    result->passes = cal_passes_for(case_index);
    result->advance_pages = cal_advance_for(case_index);
    result->first_fault_voice = UINT8_MAX;
    result->first_fault_page = UINT32_MAX;
    result->minimum_margin_frames = UINT32_MAX;
    for (uint8_t voice = 0U; voice < 8U; ++voice)
    {
        result->minimum_margin_per_voice[voice] = UINT32_MAX;
    }
}

static void cal_reset_measurement(void)
{
    cal_init_result(g_case_index);
    memset(g_last_select_valid, 0, sizeof(g_last_select_valid));
    g_round_cycles_total = 0U;
    g_round_cycles_max = 0U;
    g_round_count = 0U;
    g_round_had_select = 0U;
    g_contiguous_reads = 0U;
    g_fatfs_reads = 0U;
    sample_stream_scheduler_calibration_set_passes(cal_passes_for(g_case_index));
    sample_stream_needs_calibration_set_depth(cal_advance_for(g_case_index));
    sample_stream_benchmark_reset();
    cpu_load_reset_measurement();
    g_case_start_frame = sample_stream_time_now();
    g_measurement_active = 1U;
}

static void cal_capture_current(void)
{
    if (g_measurement_active == 0U)
    {
        return;
    }
    brick6_stream_calibration_result_t *const result = cal_current();
    cpu_load_metrics_t cpu;
    cpu_load_get_metrics(&cpu);
    result->elapsed_audio_frames =
        (uint32_t)(sample_stream_time_now() - g_case_start_frame);
    result->pages_loaded = (uint32_t)g_sample_stream_benchmark.ready_pages;
    result->source_bytes = g_sample_stream_benchmark.source_bytes;
    result->read_bytes = g_sample_stream_benchmark.read_bytes;
    result->read_cycles_total = g_sample_stream_benchmark.read_cycles_total;
    result->service_cycles_total = g_sample_stream_benchmark.service_cycles_total;
    result->physical_reads = g_sample_stream_benchmark.physical_reads;
    result->fatfs_ops = g_sample_stream_benchmark.fatfs_ops;
    result->seeks = g_sample_stream_benchmark.seeks;
    result->file_opens = g_sample_stream_benchmark.file_opens;
    result->io_errors = g_sample_stream_benchmark.io_errors;
    result->contiguous_reads = g_contiguous_reads;
    result->fatfs_reads = g_fatfs_reads;
    result->round_cycles_average = (g_round_count != 0U)
        ? (uint32_t)(g_round_cycles_total / g_round_count) : 0U;
    result->round_cycles_max = g_round_cycles_max;
    result->sd_read_cycles_average = (g_sample_stream_benchmark.selected_pages != 0U)
        ? (uint32_t)(result->read_cycles_total / g_sample_stream_benchmark.selected_pages)
        : 0U;
    result->sd_read_cycles_p99_upper = cal_histogram_p99_upper(
        (const uint32_t *)g_sample_stream_benchmark.read_cycles_histogram,
        (uint32_t)g_sample_stream_benchmark.selected_pages);
    result->sd_read_cycles_max = g_sample_stream_benchmark.read_cycles_max;
    result->pages_per_second_q16 = (result->elapsed_audio_frames != 0U)
        ? (uint32_t)(((uint64_t)result->pages_loaded * CAL_SAMPLE_RATE << 16U)
                     / result->elapsed_audio_frames) : 0U;
    result->sd_bytes_per_second = (result->elapsed_audio_frames != 0U)
        ? (uint32_t)((result->read_bytes * CAL_SAMPLE_RATE)
                     / result->elapsed_audio_frames) : 0U;
    result->audio_irq_overruns = cpu.over_100_count;
    if (result->minimum_margin_frames == UINT32_MAX)
    {
        result->minimum_margin_frames = 0U;
    }
    for (uint8_t voice = 0U; voice < 8U; ++voice)
    {
        if (result->minimum_margin_per_voice[voice] == UINT32_MAX)
        {
            result->minimum_margin_per_voice[voice] = 0U;
        }
    }
    result->passed = ((result->underruns == 0U)
                      && (result->io_errors == 0U)
                      && (result->audio_irq_overruns == 0U)
                      && (result->pages_loaded != 0U)) ? 1U : 0U;
    g_saved_mask |= (uint16_t)(1U << g_case_index);
}

static uint8_t cal_write_binary(void)
{
    brick6_stream_calibration_file_t merged;
    memset(&merged, 0, sizeof(merged));
    FIL file;
    UINT transferred = 0U;
    if (f_open(&file, "0:/stream_calibration.bin", FA_READ) == FR_OK)
    {
        (void)f_read(&file, &merged, sizeof(merged), &transferred);
        (void)f_close(&file);
    }
    uint8_t prior_valid = ((transferred >= (UINT)offsetof(brick6_stream_calibration_file_t, results))
                           && (merged.magic == BRICK6_STREAM_CALIBRATION_MAGIC)
                           && (merged.abi_version == BRICK6_STREAM_CALIBRATION_ABI_VERSION)
                           && (merged.record_size == sizeof(brick6_stream_calibration_result_t))
                           && (merged.header_size == offsetof(brick6_stream_calibration_file_t, results))
                           && (merged.result_count <= BRICK6_STREAM_CALIBRATION_MAX_RESULTS)
                           && (merged.grid_signature == CAL_GRID_SIGNATURE)) ? 1U : 0U;
    if (prior_valid != 0U)
    {
        const uint32_t payload_bytes =
            (uint32_t)merged.result_count * sizeof(brick6_stream_calibration_result_t);
        prior_valid = ((transferred == (UINT)(merged.header_size + payload_bytes))
                       && (merged.payload_crc32 == cal_crc32(merged.results, payload_bytes)))
            ? 1U : 0U;
    }
    if (prior_valid == 0U)
    {
        memset(&merged, 0, sizeof(merged));
    }

    uint16_t keep = 0U;
    for (uint16_t i = 0U; (i < merged.result_count) && (keep < CAL_CASE_COUNT); ++i)
    {
        if (merged.results[i].page_kib != BRICK6_STREAM_CALIBRATION_PAGE_KIB)
        {
            merged.results[keep++] = merged.results[i];
        }
    }
    for (uint16_t i = 0U; i < CAL_CASE_COUNT; ++i)
    {
        merged.results[keep++] = g_file.results[i];
    }
    merged.magic = BRICK6_STREAM_CALIBRATION_MAGIC;
    merged.abi_version = BRICK6_STREAM_CALIBRATION_ABI_VERSION;
    merged.header_size = (uint16_t)offsetof(brick6_stream_calibration_file_t, results);
    merged.record_size = (uint16_t)sizeof(brick6_stream_calibration_result_t);
    merged.result_count = keep;
    merged.firmware_page_kib = BRICK6_STREAM_CALIBRATION_PAGE_KIB;
    merged.sample_rate = CAL_SAMPLE_RATE;
    merged.case_duration_frames = 0U;
    merged.grid_signature = CAL_GRID_SIGNATURE;
    merged.payload_crc32 = cal_crc32(
        merged.results, keep * (uint32_t)sizeof(brick6_stream_calibration_result_t));
    if (f_open(&file, "0:/stream_calibration.bin", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        return 0U;
    }
    const uint32_t bytes = merged.header_size + keep * merged.record_size;
    const FRESULT fr = f_write(&file, &merged, bytes, &transferred);
    (void)f_sync(&file);
    (void)f_close(&file);
    if ((fr != FR_OK) || (transferred != bytes))
    {
        return 0U;
    }
    g_export_file = merged;
    return 1U;
}

static uint8_t cal_write_csv(void)
{
    FIL file;
    if (f_open(&file, "0:/stream_calibration.csv", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        return 0U;
    }
    static const char header[] =
        "case,page_kib,N,advance,served_kib_per_voice_round,ahead_kib_per_voice,volume_product_kib2,"
        "saved,pass,underruns,first_voice,first_page,min_margin_frames,"
        "min_margin_voice_1,min_margin_voice_2,min_margin_voice_3,min_margin_voice_4,"
        "min_margin_voice_5,min_margin_voice_6,min_margin_voice_7,min_margin_voice_8,"
        "max_voice_gap_frames,round_avg_cycles,round_max_cycles,sd_avg_cycles,"
        "sd_p99_cycles,sd_max_cycles,sd_bytes_s,pages_s_q16,pages,physical_reads,"
        "contiguous_reads,fatfs_reads,seeks,io_errors,storage_cycles,irq_overruns\r\n";
    UINT written = 0U;
    uint8_t ok = (f_write(&file, header, sizeof(header) - 1U, &written) == FR_OK) ? 1U : 0U;
    char line[512];
    for (uint16_t i = 0U; (i < g_export_file.result_count) && (ok != 0U); ++i)
    {
        const brick6_stream_calibration_result_t *const r = &g_export_file.results[i];
        const uint16_t scenario = (uint16_t)(i % CAL_CASE_COUNT);
        const uint8_t saved = (r->page_kib == BRICK6_STREAM_CALIBRATION_PAGE_KIB)
            ? (uint8_t)((g_saved_mask >> scenario) & 1U) : 1U;
        const int length = snprintf(line, sizeof(line),
            "%u,%u,%u,%u,%u,%u,%lu,%u,%u,%lu,%u,%lu,%lu,"
            "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,"
            "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%llu,%lu\r\n",
            (unsigned)(scenario + 1U), r->page_kib, r->passes, r->advance_pages,
            (unsigned)(r->page_kib * r->passes),
            (unsigned)(r->page_kib * r->advance_pages),
            (unsigned long)r->page_kib * r->passes * r->page_kib * r->advance_pages,
            saved, r->passed,
            (unsigned long)r->underruns, r->first_fault_voice,
            (unsigned long)r->first_fault_page,
            (unsigned long)r->minimum_margin_frames,
            (unsigned long)r->minimum_margin_per_voice[0],
            (unsigned long)r->minimum_margin_per_voice[1],
            (unsigned long)r->minimum_margin_per_voice[2],
            (unsigned long)r->minimum_margin_per_voice[3],
            (unsigned long)r->minimum_margin_per_voice[4],
            (unsigned long)r->minimum_margin_per_voice[5],
            (unsigned long)r->minimum_margin_per_voice[6],
            (unsigned long)r->minimum_margin_per_voice[7],
            (unsigned long)r->max_voice_service_gap_frames,
            (unsigned long)r->round_cycles_average,
            (unsigned long)r->round_cycles_max,
            (unsigned long)r->sd_read_cycles_average,
            (unsigned long)r->sd_read_cycles_p99_upper,
            (unsigned long)r->sd_read_cycles_max,
            (unsigned long)r->sd_bytes_per_second,
            (unsigned long)r->pages_per_second_q16,
            (unsigned long)r->pages_loaded,
            (unsigned long)r->physical_reads,
            (unsigned long)r->contiguous_reads,
            (unsigned long)r->fatfs_reads,
            (unsigned long)r->seeks,
            (unsigned long)r->io_errors,
            (unsigned long long)r->service_cycles_total,
            (unsigned long)r->audio_irq_overruns);
        if ((length <= 0) || ((uint32_t)length >= sizeof(line))
            || (f_write(&file, line, (UINT)length, &written) != FR_OK))
        {
            ok = 0U;
        }
    }
    (void)f_sync(&file);
    (void)f_close(&file);
    return ok;
}

void brick6_stream_calibration_init(void)
{
    memset(&g_file, 0, sizeof(g_file));
    g_file.magic = BRICK6_STREAM_CALIBRATION_MAGIC;
    g_file.abi_version = BRICK6_STREAM_CALIBRATION_ABI_VERSION;
    g_file.header_size = (uint16_t)offsetof(brick6_stream_calibration_file_t, results);
    g_file.record_size = (uint16_t)sizeof(brick6_stream_calibration_result_t);
    g_file.result_count = CAL_CASE_COUNT;
    g_file.firmware_page_kib = BRICK6_STREAM_CALIBRATION_PAGE_KIB;
    g_file.sample_rate = CAL_SAMPLE_RATE;
    g_file.case_duration_frames = 0U;
    g_file.grid_signature = CAL_GRID_SIGNATURE;
    for (uint16_t i = 0U; i < CAL_CASE_COUNT; ++i)
    {
        cal_init_result(i);
    }
    g_case_index = 0U;
    g_saved_mask = 0U;
    g_export_pending = 0U;
    g_initialized = 1U;
    cal_reset_measurement();
}

void brick6_stream_calibration_select_case(uint8_t case_index)
{
    if ((g_initialized == 0U) || (case_index >= CAL_CASE_COUNT)
        || (case_index == g_case_index))
    {
        return;
    }
    cal_capture_current();
    g_export_pending = 1U;
    g_case_index = case_index;
    cal_reset_measurement();
}

void brick6_stream_calibration_process(void)
{
    if ((g_export_pending == 0U)
        || (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U))
    {
        return;
    }
    if ((cal_write_binary() != 0U) && (cal_write_csv() != 0U))
    {
        g_export_pending = 0U;
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
}

void brick6_stream_calibration_note_select(
    const sample_stream_scheduler_candidate_t *candidate)
{
    if ((g_measurement_active == 0U) || (candidate == NULL) || (candidate->voice_id >= 8U))
    {
        return;
    }
    brick6_stream_calibration_result_t *const result = cal_current();
    const uint32_t now = (uint32_t)sample_stream_time_now();
    const uint32_t deadline = (uint32_t)candidate->diagnostic_deadline_audio_frame;
    const uint32_t margin = (deadline > now) ? (deadline - now) : 0U;
    const uint8_t voice = candidate->voice_id;
    g_round_had_select = 1U;
    if (margin < result->minimum_margin_frames)
    {
        result->minimum_margin_frames = margin;
    }
    if (margin < result->minimum_margin_per_voice[voice])
    {
        result->minimum_margin_per_voice[voice] = margin;
    }
    if (g_last_select_valid[voice] != 0U)
    {
        const uint32_t gap = now - g_last_select_frame[voice];
        if (gap > result->max_voice_service_gap_frames)
        {
            result->max_voice_service_gap_frames = gap;
        }
    }
    g_last_select_frame[voice] = now;
    g_last_select_valid[voice] = 1U;
}

void brick6_stream_calibration_note_io(const sample_stream_io_result_t *result,
                                       uint32_t read_cycles)
{
    (void)read_cycles;
    if ((g_measurement_active == 0U) || (result == NULL))
    {
        return;
    }
    if (result->backend == 1U)
    {
        g_contiguous_reads += result->physical_reads;
    }
    else
    {
        g_fatfs_reads += result->physical_reads;
    }
}

void brick6_stream_calibration_note_underrun(sample_audio_key_t key,
                                             uint32_t page_index)
{
    if (g_measurement_active == 0U)
    {
        return;
    }
    brick6_stream_calibration_result_t *const result = cal_current();
    ++result->underruns;
    result->minimum_margin_frames = 0U;
    if (result->first_fault_voice == UINT8_MAX)
    {
        result->first_fault_voice = (key.object_id < 8U) ? (uint8_t)key.object_id : UINT8_MAX;
        result->first_fault_page = page_index;
    }
}

void brick6_stream_calibration_note_round_begin(void)
{
    if (g_measurement_active != 0U)
    {
        g_round_begin_cycle = DWT->CYCCNT;
        g_round_had_select = 0U;
    }
}

void brick6_stream_calibration_note_round_end(void)
{
    if ((g_measurement_active != 0U) && (g_round_had_select != 0U))
    {
        const uint32_t cycles = DWT->CYCCNT - g_round_begin_cycle;
        g_round_cycles_total += cycles;
        ++g_round_count;
        if (cycles > g_round_cycles_max)
        {
            g_round_cycles_max = cycles;
        }
    }
}

uint16_t brick6_stream_calibration_case_index(void) { return g_case_index; }
uint16_t brick6_stream_calibration_case_count(void) { return CAL_CASE_COUNT; }
uint8_t brick6_stream_calibration_current_passes(void) { return cal_passes_for(g_case_index); }
uint8_t brick6_stream_calibration_current_advance(void) { return cal_advance_for(g_case_index); }
uint16_t brick6_stream_calibration_current_served_kib(void)
{
    return (uint16_t)(BRICK6_STREAM_CALIBRATION_PAGE_KIB * cal_passes_for(g_case_index));
}
uint16_t brick6_stream_calibration_current_ahead_kib(void)
{
    return (uint16_t)(BRICK6_STREAM_CALIBRATION_PAGE_KIB * cal_advance_for(g_case_index));
}

#endif
