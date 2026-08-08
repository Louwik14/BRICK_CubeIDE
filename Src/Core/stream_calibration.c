#include "Core/stream_calibration.h"

#if BRICK6_STREAM_CALIBRATION

#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "Core/brick6_sampler_runtime.h"
#include "Core/cpu_load.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_pool.h"
#include "Sampler/sample_stream_benchmark.h"
#include "Sampler/sample_stream_needs.h"
#include "Sampler/sample_stream_scheduler.h"
#include "Sampler/sample_stream_time.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include "stm32h7xx.h"

#define CAL_SAMPLE_RATE          (48000U)
#define CAL_CASE_FRAMES          (2U * CAL_SAMPLE_RATE)
#define CAL_SETTLE_FRAMES        (CAL_SAMPLE_RATE / 10U)
#define CAL_MAX_KEYBOARD_NOTE    (127U)
#define CAL_MAX_PITCH_SOURCE_FRAMES (CAL_CASE_FRAMES * 48U)
#define CAL_GRID_SIGNATURE       (0x01020306UL)

typedef enum
{
    CAL_STATE_RESET = 0,
    CAL_STATE_PREPARE,
    CAL_STATE_START,
    CAL_STATE_RUN,
    CAL_STATE_SETTLE,
    CAL_STATE_WRITE,
    CAL_STATE_COMPLETE,
    CAL_STATE_FATAL
} cal_state_t;

static const uint8_t k_passes[] = {1U, 2U, 3U};
static const uint8_t k_advance[] = {2U, 3U, 4U, 5U, 6U};
static const char *const k_paths[8] = {
    "0:/voix1.wav", "0:/voix2.wav", "0:/voix3.wav", "0:/voix4.wav",
    "0:/voix5.wav", "0:/voix6.wav", "0:/voix7.wav", "0:/voix8.wav"
};

STORAGE_STATE_SDRAM static brick6_stream_calibration_file_t g_file;
static cal_state_t g_state;
static uint16_t g_case_index;
static uint8_t g_prepare_index;
static uint8_t g_case_running;
static sample_stream_audio_frame_t g_case_start_frame;
static sample_stream_audio_frame_t g_state_start_frame;
static uint32_t g_last_select_frame[8];
static uint8_t g_last_select_valid[8];
static uint32_t g_round_begin_cycle;
static uint64_t g_round_cycles_total;
static uint32_t g_round_cycles_max;
static uint32_t g_round_count;
static uint8_t g_round_had_select;
static uint32_t g_contiguous_reads;
static uint32_t g_fatfs_reads;
static uint8_t g_started_voices;

static brick6_stream_calibration_result_t *cal_current(void)
{
    return &g_file.results[g_case_index];
}

static uint8_t cal_passes(void)
{
    return k_passes[g_case_index / (uint16_t)(sizeof(k_advance))];
}

static uint8_t cal_advance(void)
{
    return k_advance[g_case_index % (uint16_t)(sizeof(k_advance))];
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

static uint32_t cal_histogram_p99_upper(const uint32_t histogram[32],
                                        uint32_t samples)
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

static void cal_stop_all(void)
{
    for (uint8_t track = 0U; track < 8U; ++track)
    {
        brick6_sampler_runtime_stop(track);
    }
}

static void cal_configure_tracks(void)
{
    for (uint8_t track = 0U; track < 8U; ++track)
    {
        (void)track_state_set_track_family(track, UI_TRACK_FAMILY_SAMPLER);
        (void)track_state_set_track_type(track, UI_TRACK_TYPE_STREAM);
        track_runtime_refresh_track(track);
        brick6_sampler_runtime_reset_track(track);
        brick6_sampler_runtime_set_sample(track, track);
        brick6_sampler_runtime_set_gain(track, 0.125f);
        brick6_sampler_runtime_set_start(track, 0.0f);
        brick6_sampler_runtime_set_end(track, 1.0f);
        brick6_sampler_runtime_set_mode(track, 0U);
        brick6_sampler_runtime_set_tune(track, 0.0f);
    }
}

static void cal_begin_case(void)
{
    brick6_stream_calibration_result_t *result = cal_current();
    memset(result, 0, sizeof(*result));
    result->page_kib = BRICK6_STREAM_CALIBRATION_PAGE_KIB;
    result->passes = cal_passes();
    result->advance_pages = cal_advance();
    result->first_fault_voice = UINT8_MAX;
    result->first_fault_page = UINT32_MAX;
    result->minimum_margin_frames = UINT32_MAX;
    for (uint8_t voice = 0U; voice < 8U; ++voice)
    {
        result->minimum_margin_per_voice[voice] = UINT32_MAX;
    }
    memset(g_last_select_valid, 0, sizeof(g_last_select_valid));
    g_round_cycles_total = 0U;
    g_round_cycles_max = 0U;
    g_round_count = 0U;
    g_contiguous_reads = 0U;
    g_fatfs_reads = 0U;
    sample_stream_scheduler_calibration_set_passes(result->passes);
    sample_stream_needs_calibration_set_depth(result->advance_pages);
    sample_stream_benchmark_reset();
    brick6_sampler_runtime_diag_reset();
    cpu_load_reset_measurement();
    g_case_start_frame = sample_stream_time_now();
    g_case_running = 1U;
    for (uint8_t track = 0U; track < 8U; ++track)
    {
        brick6_sampler_runtime_trigger_note_velocity(track,
                                                     CAL_MAX_KEYBOARD_NOTE,
                                                     127U);
    }
    g_started_voices = 0U;
    for (uint8_t track = 0U; track < 8U; ++track)
    {
        g_started_voices +=
            brick6_sampler_runtime_calibration_track_stream_active(track);
    }
}

static void cal_finish_case(void)
{
    brick6_stream_calibration_result_t *result = cal_current();
    cpu_load_metrics_t cpu;
    g_case_running = 0U;
    cal_stop_all();
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
                                       ? (uint32_t)(g_round_cycles_total / g_round_count)
                                       : 0U;
    result->round_cycles_max = g_round_cycles_max;
    result->sd_read_cycles_average = (g_sample_stream_benchmark.selected_pages != 0U)
                                         ? (uint32_t)(result->read_cycles_total
                                                      / g_sample_stream_benchmark.selected_pages)
                                         : 0U;
    result->sd_read_cycles_p99_upper = cal_histogram_p99_upper(
        (const uint32_t *)g_sample_stream_benchmark.read_cycles_histogram,
        (uint32_t)g_sample_stream_benchmark.selected_pages);
    result->sd_read_cycles_max = g_sample_stream_benchmark.read_cycles_max;
    result->pages_per_second_q16 = (result->elapsed_audio_frames != 0U)
        ? (uint32_t)(((uint64_t)result->pages_loaded * CAL_SAMPLE_RATE << 16U)
                     / result->elapsed_audio_frames)
        : 0U;
    result->sd_bytes_per_second = (result->elapsed_audio_frames != 0U)
        ? (uint32_t)((result->read_bytes * CAL_SAMPLE_RATE)
                     / result->elapsed_audio_frames)
        : 0U;
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
                      && (g_started_voices == 8U)
                      && (result->pages_loaded != 0U)) ? 1U : 0U;
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
        const uint32_t prior_bytes = merged.header_size
            + (uint32_t)merged.result_count * merged.record_size;
        const uint32_t prior_crc = cal_crc32(
            merged.results,
            (uint32_t)merged.result_count * sizeof(brick6_stream_calibration_result_t));
        prior_valid = ((transferred == prior_bytes)
                       && (merged.payload_crc32 == prior_crc)) ? 1U : 0U;
    }
    if (prior_valid == 0U)
    {
        memset(&merged, 0, sizeof(merged));
    }
    uint16_t keep = 0U;
    for (uint16_t i = 0U; (i < merged.result_count) && (keep < 15U); ++i)
    {
        if (merged.results[i].page_kib != BRICK6_STREAM_CALIBRATION_PAGE_KIB)
        {
            merged.results[keep++] = merged.results[i];
        }
    }
    for (uint16_t i = 0U; i < BRICK6_STREAM_CALIBRATION_CASES_PER_BUILD; ++i)
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
    merged.case_duration_frames = CAL_CASE_FRAMES;
    merged.grid_signature = CAL_GRID_SIGNATURE;
    merged.payload_crc32 = cal_crc32(merged.results,
        keep * (uint32_t)sizeof(brick6_stream_calibration_result_t));
    if (f_open(&file, "0:/stream_calibration.bin", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        return 0U;
    }
    const uint32_t bytes = merged.header_size + keep * merged.record_size;
    const FRESULT fr = f_write(&file, &merged, bytes, &transferred);
    (void)f_sync(&file);
    (void)f_close(&file);
    g_file = merged;
    return ((fr == FR_OK) && (transferred == bytes)) ? 1U : 0U;
}

static void cal_write_csv(void)
{
    FIL file;
    if (f_open(&file, "0:/stream_calibration.csv", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        return;
    }
    static const char header[] =
        "page_kib,N,advance,pass,underruns,first_voice,first_page,min_margin_frames,"
        "max_voice_gap_frames,round_avg_cycles,round_max_cycles,sd_avg_cycles,"
        "sd_p99_cycles,sd_max_cycles,sd_bytes_s,pages_s_q16,pages,physical_reads,"
        "contiguous_reads,fatfs_reads,seeks,io_errors,storage_cycles,irq_overruns\r\n";
    UINT written;
    (void)f_write(&file, header, sizeof(header) - 1U, &written);
    char line[384];
    for (uint16_t i = 0U; i < g_file.result_count; ++i)
    {
        const brick6_stream_calibration_result_t *r = &g_file.results[i];
        const int length = snprintf(line, sizeof(line),
            "%u,%u,%u,%u,%lu,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%llu,%lu\r\n",
            r->page_kib, r->passes, r->advance_pages, r->passed,
            (unsigned long)r->underruns, r->first_fault_voice,
            (unsigned long)r->first_fault_page,
            (unsigned long)r->minimum_margin_frames,
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
        if (length > 0)
        {
            (void)f_write(&file, line, (UINT)length, &written);
        }
    }
    (void)f_sync(&file);
    (void)f_close(&file);
}

void brick6_stream_calibration_init(void)
{
    memset(&g_file, 0, sizeof(g_file));
    g_file.magic = BRICK6_STREAM_CALIBRATION_MAGIC;
    g_file.abi_version = BRICK6_STREAM_CALIBRATION_ABI_VERSION;
    g_file.header_size = (uint16_t)offsetof(brick6_stream_calibration_file_t, results);
    g_file.record_size = (uint16_t)sizeof(brick6_stream_calibration_result_t);
    g_file.firmware_page_kib = BRICK6_STREAM_CALIBRATION_PAGE_KIB;
    g_file.sample_rate = CAL_SAMPLE_RATE;
    g_file.case_duration_frames = CAL_CASE_FRAMES;
    g_file.grid_signature = CAL_GRID_SIGNATURE;
    g_state = CAL_STATE_RESET;
    g_case_index = 0U;
    g_prepare_index = 0U;
    g_case_running = 0U;
    cal_configure_tracks();
}

void brick6_stream_calibration_process(void)
{
    switch (g_state)
    {
        case CAL_STATE_PREPARE:
            if (g_prepare_index < 8U)
            {
                sample_pool_clear(g_prepare_index);
                if (!sample_pool_load(g_prepare_index, k_paths[g_prepare_index]))
                {
                    g_state = CAL_STATE_FATAL;
                    break;
                }
                const sample_desc_t *desc = sample_pool_get(g_prepare_index);
                if ((desc == NULL) || (desc->valid == 0U) || (desc->channels != 2U)
                    || (desc->sample_rate != CAL_SAMPLE_RATE)
                    || (desc->length_frames < CAL_MAX_PITCH_SOURCE_FRAMES))
                {
                    g_state = CAL_STATE_FATAL;
                    break;
                }
                ++g_prepare_index;
                g_state = CAL_STATE_PREPARE;
                break;
            }
            g_state = CAL_STATE_START;
            break;

        case CAL_STATE_RESET:
            cal_stop_all();
            sample_cache_init();
            g_prepare_index = 0U;
            g_state = CAL_STATE_PREPARE;
            break;

        case CAL_STATE_START:
            cal_configure_tracks();
            cal_begin_case();
            g_state = CAL_STATE_RUN;
            break;

        case CAL_STATE_RUN:
            if ((sample_stream_time_now() - g_case_start_frame) >= CAL_CASE_FRAMES)
            {
                cal_finish_case();
                g_state_start_frame = sample_stream_time_now();
                g_state = CAL_STATE_SETTLE;
            }
            break;

        case CAL_STATE_SETTLE:
            if ((sample_stream_time_now() - g_state_start_frame) >= CAL_SETTLE_FRAMES)
            {
                ++g_case_index;
                if (g_case_index >= BRICK6_STREAM_CALIBRATION_CASES_PER_BUILD)
                {
                    g_file.result_count = BRICK6_STREAM_CALIBRATION_CASES_PER_BUILD;
                    g_state = CAL_STATE_WRITE;
                }
                else
                {
                    g_state = CAL_STATE_RESET;
                }
            }
            break;

        case CAL_STATE_WRITE:
            if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
            {
                break;
            }
            if (cal_write_binary() != 0U)
            {
                cal_write_csv();
                g_state = CAL_STATE_COMPLETE;
            }
            else
            {
                g_state = CAL_STATE_FATAL;
            }
            sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
            break;

        case CAL_STATE_COMPLETE:
        case CAL_STATE_FATAL:
        default:
            break;
    }
}

void brick6_stream_calibration_note_select(
    const sample_stream_scheduler_candidate_t *candidate)
{
    if ((g_case_running == 0U) || (candidate == NULL) || (candidate->voice_id >= 8U))
    {
        return;
    }
    brick6_stream_calibration_result_t *result = cal_current();
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
    if ((g_case_running == 0U) || (result == NULL))
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
    if (g_case_running == 0U)
    {
        return;
    }
    brick6_stream_calibration_result_t *result = cal_current();
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
    if (g_case_running != 0U)
    {
        g_round_begin_cycle = DWT->CYCCNT;
        g_round_had_select = 0U;
    }
}

void brick6_stream_calibration_note_round_end(void)
{
    if ((g_case_running != 0U) && (g_round_had_select != 0U))
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

uint8_t brick6_stream_calibration_complete(void)
{
    return (g_state == CAL_STATE_COMPLETE) ? 1U : 0U;
}

uint8_t brick6_stream_calibration_error(void)
{
    return (g_state == CAL_STATE_FATAL) ? 1U : 0U;
}

uint16_t brick6_stream_calibration_case_index(void) { return g_case_index; }
uint16_t brick6_stream_calibration_case_count(void) { return BRICK6_STREAM_CALIBRATION_CASES_PER_BUILD; }
uint8_t brick6_stream_calibration_current_passes(void) { return cal_passes(); }
uint8_t brick6_stream_calibration_current_advance(void) { return cal_advance(); }

uint16_t brick6_stream_calibration_pass_count(void)
{
    uint16_t count = 0U;
    const uint16_t end = (g_state == CAL_STATE_COMPLETE) ? g_file.result_count : g_case_index;
    for (uint16_t i = 0U; i < end; ++i) count += g_file.results[i].passed;
    return count;
}

uint16_t brick6_stream_calibration_fail_count(void)
{
    const uint16_t end = (g_state == CAL_STATE_COMPLETE) ? g_file.result_count : g_case_index;
    return (uint16_t)(end - brick6_stream_calibration_pass_count());
}

#endif
