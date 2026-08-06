#include "Sampler/sample_stream_benchmark.h"

#if BRICK6_STREAM_BENCH

#include <string.h>

#include "stm32h7xx.h"

#define SAMPLE_STREAM_BENCH_MAGIC (0x53424E43UL)
#define SAMPLE_STREAM_BENCH_ABI_VERSION (1U)

volatile sample_stream_benchmark_snapshot_t g_sample_stream_benchmark;
static sample_audio_key_t g_sample_stream_benchmark_last_key;
static uint8_t g_sample_stream_benchmark_last_key_valid;

static uint32_t sample_stream_benchmark_bucket(uint32_t value)
{
    uint32_t bucket = 0U;
    while ((value > 1U) && (bucket < (SAMPLE_STREAM_BENCH_HISTOGRAM_BUCKETS - 1U)))
    {
        value >>= 1U;
        bucket++;
    }
    return bucket;
}

void sample_stream_benchmark_reset(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    memset((void *)&g_sample_stream_benchmark, 0, sizeof(g_sample_stream_benchmark));
    g_sample_stream_benchmark.magic = SAMPLE_STREAM_BENCH_MAGIC;
    g_sample_stream_benchmark.abi_version = SAMPLE_STREAM_BENCH_ABI_VERSION;
    g_sample_stream_benchmark.enabled = 1U;
    g_sample_stream_benchmark.read_chunk_kib =
        (uint32_t)sample_stream_io_get_read_chunk_kib();
    g_sample_stream_benchmark.start_audio_frame = sample_stream_time_now();
    memset(&g_sample_stream_benchmark_last_key, 0,
           sizeof(g_sample_stream_benchmark_last_key));
    g_sample_stream_benchmark_last_key_valid = 0U;
}

void sample_stream_benchmark_note_io(sample_audio_key_t key,
                                     const sample_stream_io_result_t *result,
                                     uint32_t read_cycles,
                                     uint32_t wait_frames,
                                     uint32_t backlog,
                                     uint8_t deadline_missed)
{
    if ((result == 0) || (g_sample_stream_benchmark.enabled == 0U))
    {
        return;
    }
    g_sample_stream_benchmark.selected_pages++;
    g_sample_stream_benchmark.source_bytes += result->source_bytes;
    g_sample_stream_benchmark.read_bytes += result->read_bytes;
    g_sample_stream_benchmark.read_cycles_total += read_cycles;
    g_sample_stream_benchmark.wait_frames_total += wait_frames;
    g_sample_stream_benchmark.physical_reads += result->physical_reads;
    g_sample_stream_benchmark.fatfs_ops += result->fatfs_ops;
    g_sample_stream_benchmark.file_opens += result->file_opens;
    g_sample_stream_benchmark.seeks += result->seeks;
    g_sample_stream_benchmark.read_cache_hits += result->read_cache_hits;
    g_sample_stream_benchmark.deadline_misses += (deadline_missed != 0U) ? 1U : 0U;
    if (result->load_result == SAMPLE_PAGE_LOAD_OK)
    {
        g_sample_stream_benchmark.ready_pages++;
    }
    else
    {
        g_sample_stream_benchmark.io_errors++;
    }
    if (read_cycles > g_sample_stream_benchmark.read_cycles_max)
    {
        g_sample_stream_benchmark.read_cycles_max = read_cycles;
    }
    if (wait_frames > g_sample_stream_benchmark.wait_frames_max)
    {
        g_sample_stream_benchmark.wait_frames_max = wait_frames;
    }
    if (backlog > g_sample_stream_benchmark.backlog_max)
    {
        g_sample_stream_benchmark.backlog_max = backlog;
    }
    g_sample_stream_benchmark.read_cycles_histogram[
        sample_stream_benchmark_bucket(read_cycles)]++;
    g_sample_stream_benchmark.wait_frames_histogram[
        sample_stream_benchmark_bucket(wait_frames)]++;
    if ((g_sample_stream_benchmark_last_key_valid != 0U)
        && (sample_audio_key_equal(&g_sample_stream_benchmark_last_key, &key) == 0U))
    {
        g_sample_stream_benchmark.source_changes++;
    }
    g_sample_stream_benchmark_last_key = key;
    g_sample_stream_benchmark_last_key_valid = 1U;
}

void sample_stream_benchmark_note_service(uint32_t pages, uint32_t service_cycles)
{
    if (g_sample_stream_benchmark.enabled == 0U)
    {
        return;
    }
    g_sample_stream_benchmark.service_calls++;
    g_sample_stream_benchmark.service_cycles_total += service_cycles;
    if (service_cycles > g_sample_stream_benchmark.service_cycles_max)
    {
        g_sample_stream_benchmark.service_cycles_max = service_cycles;
    }
    if (pages > g_sample_stream_benchmark.pages_per_service_max)
    {
        g_sample_stream_benchmark.pages_per_service_max = pages;
    }
}

void sample_stream_benchmark_note_blocked_poll(void)
{
    if (g_sample_stream_benchmark.enabled != 0U)
    {
        g_sample_stream_benchmark.blocked_service_polls++;
    }
}

#endif
