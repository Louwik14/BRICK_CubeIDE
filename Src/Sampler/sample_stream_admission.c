#include "Sampler/sample_stream_admission.h"

#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_trace.h"

#define SAMPLE_STREAM_ADMISSION_DEFAULT_BYTES_PER_SECOND (6000000U)
#define SAMPLE_STREAM_ADMISSION_DEFAULT_FILE_OVERHEAD (32768U)
#define SAMPLE_STREAM_ADMISSION_DEFAULT_LATENCY_FRAMES (384U)
#define SAMPLE_STREAM_ADMISSION_DEFAULT_UTILIZATION_PERMILLE (750U)
#define SAMPLE_STREAM_ADMISSION_OUTPUT_RATE_HZ (48000ULL)

typedef struct
{
    sample_stream_admission_demand_t demand;
    uint64_t bytes_per_second;
    uint8_t active;
} sample_stream_admission_slot_t;

static sample_stream_admission_config_t g_sample_stream_admission_config;
static sample_stream_admission_slot_t
    g_sample_stream_admission_slots[SAMPLE_STREAM_ADMISSION_MAX_VOICES];
static sample_stream_admission_stats_t g_sample_stream_admission_stats;

static uint8_t sample_stream_admission_source_valid(uint8_t source)
{
    return (source <= (uint8_t)SAMPLE_STREAM_SNAPSHOT_MULTI) ? 1U : 0U;
}

static uint8_t sample_stream_admission_same_owner(
    const sample_stream_admission_demand_t *a,
    const sample_stream_admission_demand_t *b)
{
    return ((a->source == b->source) && (a->voice_id == b->voice_id)
            && (a->voice_generation == b->voice_generation)) ? 1U : 0U;
}

static uint64_t sample_stream_admission_rate(const sample_stream_admission_demand_t *demand)
{
    return (SAMPLE_STREAM_ADMISSION_OUTPUT_RATE_HZ * (uint64_t)demand->block_align
            * (uint64_t)demand->step_q16 + 65535ULL) / 65536ULL;
}

static uint32_t sample_stream_admission_projected_files(
    const sample_stream_admission_demand_t *demand,
    int32_t replaced_index)
{
    uint32_t files = 0U;
    uint8_t demand_file_seen = 0U;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_ADMISSION_MAX_VOICES; ++i)
    {
        if (((int32_t)i == replaced_index)
            || (g_sample_stream_admission_slots[i].active == 0U))
        {
            continue;
        }
        uint8_t first = 1U;
        for (uint32_t j = 0U; j < i; ++j)
        {
            if (((int32_t)j != replaced_index)
                && (g_sample_stream_admission_slots[j].active != 0U)
                && (sample_audio_key_equal(&g_sample_stream_admission_slots[j].demand.key,
                                           &g_sample_stream_admission_slots[i].demand.key)
                    != 0U))
            {
                first = 0U;
                break;
            }
        }
        if (first != 0U)
        {
            files++;
        }
        if (sample_audio_key_equal(&g_sample_stream_admission_slots[i].demand.key,
                                   &demand->key) != 0U)
        {
            demand_file_seen = 1U;
        }
    }
    return files + ((demand_file_seen == 0U) ? 1U : 0U);
}

static void sample_stream_admission_refresh_stats(void)
{
    uint64_t rate = 0U;
    uint8_t voices = 0U;
    uint8_t files = 0U;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_ADMISSION_MAX_VOICES; ++i)
    {
        if (g_sample_stream_admission_slots[i].active == 0U)
        {
            continue;
        }
        voices++;
        rate += g_sample_stream_admission_slots[i].bytes_per_second;
        uint8_t first = 1U;
        for (uint32_t j = 0U; j < i; ++j)
        {
            if ((g_sample_stream_admission_slots[j].active != 0U)
                && (sample_audio_key_equal(&g_sample_stream_admission_slots[j].demand.key,
                                           &g_sample_stream_admission_slots[i].demand.key) != 0U))
            {
                first = 0U;
                break;
            }
        }
        files += first;
    }
    g_sample_stream_admission_stats.active_voices = voices;
    g_sample_stream_admission_stats.distinct_files = files;
    g_sample_stream_admission_stats.admitted_bytes_per_second = rate;
    g_sample_stream_admission_stats.capacity_bytes_per_second =
        ((uint64_t)g_sample_stream_admission_config.measured_bytes_per_second
         * g_sample_stream_admission_config.utilization_permille) / 1000ULL;
}

void sample_stream_admission_init(const sample_stream_admission_config_t *config)
{
    memset(g_sample_stream_admission_slots, 0, sizeof(g_sample_stream_admission_slots));
    memset(&g_sample_stream_admission_stats, 0, sizeof(g_sample_stream_admission_stats));
    g_sample_stream_admission_config.measured_bytes_per_second =
        ((config != 0) && (config->measured_bytes_per_second != 0U))
            ? config->measured_bytes_per_second
            : SAMPLE_STREAM_ADMISSION_DEFAULT_BYTES_PER_SECOND;
    g_sample_stream_admission_config.per_distinct_file_overhead_bytes_per_second =
        (config != 0) ? config->per_distinct_file_overhead_bytes_per_second
                      : SAMPLE_STREAM_ADMISSION_DEFAULT_FILE_OVERHEAD;
    g_sample_stream_admission_config.worst_read_latency_audio_frames =
        ((config != 0) && (config->worst_read_latency_audio_frames != 0U))
            ? config->worst_read_latency_audio_frames
            : SAMPLE_STREAM_ADMISSION_DEFAULT_LATENCY_FRAMES;
    g_sample_stream_admission_config.utilization_permille =
        ((config != 0) && (config->utilization_permille != 0U)
         && (config->utilization_permille <= 1000U))
            ? config->utilization_permille
            : SAMPLE_STREAM_ADMISSION_DEFAULT_UTILIZATION_PERMILLE;
    g_sample_stream_admission_config.max_voices =
        ((config != 0) && (config->max_voices != 0U)
         && (config->max_voices <= SAMPLE_STREAM_ADMISSION_MAX_VOICES))
            ? config->max_voices
            : SAMPLE_STREAM_ADMISSION_MAX_VOICES;
    sample_stream_admission_refresh_stats();
}

sample_stream_admission_result_t sample_stream_admission_try_reserve(
    const sample_stream_admission_demand_t *demand)
{
    if ((demand == 0) || (demand->block_align == 0U) || (demand->step_q16 == 0U)
        || (sample_stream_admission_source_valid(demand->source) == 0U))
    {
        g_sample_stream_admission_stats.last_result = SAMPLE_STREAM_ADMISSION_INVALID;
        return SAMPLE_STREAM_ADMISSION_INVALID;
    }

    int32_t free_index = -1;
    int32_t owner_index = -1;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_ADMISSION_MAX_VOICES; ++i)
    {
        if ((g_sample_stream_admission_slots[i].active != 0U)
            && (sample_stream_admission_same_owner(&g_sample_stream_admission_slots[i].demand,
                                                   demand) != 0U))
        {
            owner_index = (int32_t)i;
        }
        if ((free_index < 0) && (g_sample_stream_admission_slots[i].active == 0U))
        {
            free_index = (int32_t)i;
        }
    }
    if ((owner_index < 0)
        && ((free_index < 0)
            || (g_sample_stream_admission_stats.active_voices
                >= g_sample_stream_admission_config.max_voices)))
    {
        g_sample_stream_admission_stats.rejection_count++;
        g_sample_stream_admission_stats.last_result = SAMPLE_STREAM_ADMISSION_VOICE_LIMIT;
        return SAMPLE_STREAM_ADMISSION_VOICE_LIMIT;
    }

    const uint64_t demand_rate = sample_stream_admission_rate(demand);
    const uint32_t projected_files = sample_stream_admission_projected_files(
        demand, owner_index);
    if ((demand->horizon_frames != 0U)
        && (((uint64_t)projected_files
             * g_sample_stream_admission_config.worst_read_latency_audio_frames)
            > demand->horizon_frames))
    {
        g_sample_stream_admission_stats.rejection_count++;
        g_sample_stream_admission_stats.last_result = SAMPLE_STREAM_ADMISSION_LATENCY;
        return SAMPLE_STREAM_ADMISSION_LATENCY;
    }
    const uint64_t previous_rate = (owner_index >= 0)
        ? g_sample_stream_admission_slots[(uint32_t)owner_index].bytes_per_second
        : 0U;
    const uint64_t projected = g_sample_stream_admission_stats.admitted_bytes_per_second
        - previous_rate
        + demand_rate
        + ((uint64_t)projected_files
           * g_sample_stream_admission_config.per_distinct_file_overhead_bytes_per_second);
    if (projected > g_sample_stream_admission_stats.capacity_bytes_per_second)
    {
        g_sample_stream_admission_stats.rejection_count++;
        g_sample_stream_admission_stats.last_result = SAMPLE_STREAM_ADMISSION_BANDWIDTH;
        return SAMPLE_STREAM_ADMISSION_BANDWIDTH;
    }

    sample_stream_admission_slot_t *const slot = &g_sample_stream_admission_slots[
        (uint32_t)((owner_index >= 0) ? owner_index : free_index)];
    slot->demand = *demand;
    slot->bytes_per_second = demand_rate;
    slot->active = 1U;
    g_sample_stream_admission_stats.last_result = SAMPLE_STREAM_ADMISSION_OK;
    sample_stream_admission_refresh_stats();
    return SAMPLE_STREAM_ADMISSION_OK;
}

sample_stream_admission_result_t sample_stream_admission_sync_snapshot(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    const sample_stream_snapshot_t *snapshot)
{
    if (snapshot == 0)
    {
        return SAMPLE_STREAM_ADMISSION_INVALID;
    }

    if (source > SAMPLE_STREAM_SNAPSHOT_MULTI)
    {
        return SAMPLE_STREAM_ADMISSION_INVALID;
    }
    if (snapshot->active == 0U)
    {
        sample_stream_admission_release_voice(source, voice_id);
        return SAMPLE_STREAM_ADMISSION_OK;
    }

    sample_page_stream_info_t stream_info;
    if ((snapshot->generation == 0U)
        || (snapshot->step_q16 == 0U)
        || (sample_page_cache_get_stream_info_key(snapshot->key, &stream_info) == 0U))
    {
        g_sample_stream_admission_stats.last_result = SAMPLE_STREAM_ADMISSION_INVALID;
        return SAMPLE_STREAM_ADMISSION_INVALID;
    }

    const sample_stream_admission_demand_t demand = {
        .key = snapshot->key,
        .step_q16 = snapshot->step_q16,
        .voice_generation = snapshot->generation,
        .horizon_frames = SAMPLE_AUDIO_FORMAT_STREAM_HORIZON_FRAMES,
        .block_align = stream_info.info.block_align,
        .source = (uint8_t)source,
        .voice_id = voice_id,
    };
    const sample_stream_admission_result_t result =
        sample_stream_admission_try_reserve(&demand);
    (void)sample_stream_event_trace_record(
        (result == SAMPLE_STREAM_ADMISSION_OK)
            ? SAMPLE_STREAM_EVENT_ADMISSION_ACCEPT
            : SAMPLE_STREAM_EVENT_ADMISSION_REJECT,
        snapshot->key,
        UINT32_MAX,
        (uint8_t)source,
        voice_id,
        snapshot->generation,
        0U,
        snapshot->step_q16,
        demand.block_align,
        (uint8_t)result);
    return result;
}

void sample_stream_admission_release_voice(sample_stream_snapshot_source_t source,
                                           uint8_t voice_id)
{
    if (source > SAMPLE_STREAM_SNAPSHOT_MULTI)
    {
        return;
    }

    uint8_t released = 0U;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_ADMISSION_MAX_VOICES; ++i)
    {
        sample_stream_admission_slot_t *const slot = &g_sample_stream_admission_slots[i];
        if ((slot->active != 0U) && (slot->demand.source == (uint8_t)source)
            && (slot->demand.voice_id == voice_id))
        {
            memset(slot, 0, sizeof(*slot));
            released = 1U;
        }
    }
    if (released != 0U)
    {
        sample_stream_admission_refresh_stats();
        (void)sample_stream_event_trace_record(
            SAMPLE_STREAM_EVENT_ADMISSION_RELEASE,
            (sample_audio_key_t){ 0U, 0U },
            UINT32_MAX,
            (uint8_t)source,
            voice_id,
            0U,
            0U,
            0U,
            0U,
            0U);
    }
}

void sample_stream_admission_get_stats(sample_stream_admission_stats_t *out_stats)
{
    if (out_stats != 0)
    {
        *out_stats = g_sample_stream_admission_stats;
    }
}
