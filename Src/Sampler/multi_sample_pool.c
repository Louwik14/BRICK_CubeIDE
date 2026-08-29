#include "Sampler/multi_sample_pool.h"

#include <string.h>

#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_needs.h"
#include "Sampler/sample_stream_manager.h"
#include "IPC/multi_sample_audio_projection.h"
#include "IPC/control_audio_publication.h"
#include "Core/live_clock.h"
#include "Storage/sd_access_gate.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    uint8_t used;
    multi_sample_instrument_t desc;
} multi_sample_instrument_slot_t;

static CTRL_STATE multi_sample_instrument_slot_t
    g_multi_instruments[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];
SDRAM_MULTI_POOL static multi_sample_desc_t g_multi_samples[MULTI_SAMPLE_POOL_MAX_SAMPLES];
SDRAM_MULTI_POOL static multi_sample_zone_t g_multi_zones[MULTI_SAMPLE_POOL_MAX_ZONES];
static CTRL_STATE uint16_t g_multi_sample_count;
static CTRL_STATE uint16_t g_multi_zone_count;
static CTRL_STATE uint32_t
    g_multi_retire_fence[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];

static uint8_t multi_sample_instrument_id_valid(uint16_t instrument_id)
{
    return (instrument_id < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS) ? 1U : 0U;
}

static uint8_t multi_sample_copy_text(char *dst, uint32_t dst_size, const char *src)
{
    if ((dst == 0) || (dst_size == 0U))
    {
        return 0U;
    }

    dst[0] = '\0';
    if (src == 0)
    {
        return 1U;
    }

    uint32_t i = 0U;
    while ((i + 1U) < dst_size)
    {
        dst[i] = src[i];
        if (src[i] == '\0')
        {
            return 1U;
        }
        i++;
    }

    dst[i] = '\0';
    return (src[i] == '\0') ? 1U : 0U;
}

static uint8_t multi_sample_range_contains(uint8_t low, uint8_t high, uint8_t value)
{
    return ((value >= low) && (value <= high)) ? 1U : 0U;
}

static uint8_t multi_sample_abs_note_delta(uint8_t a, uint8_t b)
{
    return (a >= b) ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

static uint8_t multi_sample_count_velocity_layers_for_note_root(
    const multi_sample_instrument_t *instrument,
    uint8_t note,
    uint8_t root_note)
{
    if ((instrument == 0) || (instrument->zone_count == 0U)
        || (instrument->first_zone_id >= MULTI_SAMPLE_POOL_MAX_ZONES))
    {
        return 0U;
    }

    uint16_t count = 0U;
    const uint32_t end_zone = (uint32_t)instrument->first_zone_id + instrument->zone_count;
    for (uint32_t zone_id = instrument->first_zone_id;
         (zone_id < end_zone) && (zone_id < MULTI_SAMPLE_POOL_MAX_ZONES);
         ++zone_id)
    {
        const multi_sample_zone_t *const zone = &g_multi_zones[zone_id];
        if ((zone->multi_sample_id >= g_multi_sample_count)
            || (zone->root_note != root_note)
            || (multi_sample_range_contains(zone->note_low, zone->note_high, note) == 0U))
        {
            continue;
        }

        if (count < UINT8_MAX)
        {
            count++;
        }
    }

    return (uint8_t)count;
}

static void multi_sample_instrument_recompute_note_range(multi_sample_instrument_t *instrument)
{
    if ((instrument == 0) || (instrument->zone_count == 0U)
        || (instrument->first_zone_id >= MULTI_SAMPLE_POOL_MAX_ZONES))
    {
        if (instrument != 0)
        {
            instrument->note_min = 0U;
            instrument->note_max = 0U;
        }
        return;
    }

    uint8_t note_min = 127U;
    uint8_t note_max = 0U;
    const uint32_t end_zone = (uint32_t)instrument->first_zone_id + instrument->zone_count;
    for (uint32_t zone_id = instrument->first_zone_id;
         (zone_id < end_zone) && (zone_id < MULTI_SAMPLE_POOL_MAX_ZONES);
         ++zone_id)
    {
        const multi_sample_zone_t *const zone = &g_multi_zones[zone_id];
        if (zone->note_low < note_min)
        {
            note_min = zone->note_low;
        }
        if (zone->note_high > note_max)
        {
            note_max = zone->note_high;
        }
    }

    instrument->note_min = note_min;
    instrument->note_max = note_max;
}

void multi_sample_pool_init(void)
{
    multi_sample_pool_reset();
}

void multi_sample_pool_reset(void)
{
    multi_sample_audio_projection_init();
    memset(g_multi_retire_fence, 0, sizeof(g_multi_retire_fence));
    memset(g_multi_instruments, 0, sizeof(g_multi_instruments));
    memset(g_multi_samples, 0, sizeof(g_multi_samples));
    memset(g_multi_zones, 0, sizeof(g_multi_zones));
    g_multi_sample_count = 0U;
    g_multi_zone_count = 0U;

    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
    {
        g_multi_instruments[i].desc.id = i;
        g_multi_instruments[i].desc.state = MULTI_SAMPLE_INSTRUMENT_EMPTY;
        g_multi_instruments[i].desc.first_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_multi_instruments[i].desc.first_zone_id = MULTI_SAMPLE_POOL_INVALID_ID;
    }
}

uint16_t multi_sample_pool_get_instrument_count(void)
{
    uint16_t count = 0U;
    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
    {
        if (g_multi_instruments[i].used != 0U)
        {
            count++;
        }
    }
    return count;
}

uint16_t multi_sample_pool_get_sample_capacity_used(void)
{
    return g_multi_sample_count;
}

uint16_t multi_sample_pool_get_slot_capacity_used(void)
{
    uint32_t slots = 0U;
    for (uint16_t i = 0U; i < g_multi_sample_count; ++i)
    {
        const multi_sample_desc_t *const sample = &g_multi_samples[i];
        if ((sample->instrument_id < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
            && (g_multi_instruments[sample->instrument_id].used != 0U))
        {
            slots += sample_audio_format_multi_start_slot_cost(sample->format);
        }
    }
    return (slots > UINT16_MAX) ? UINT16_MAX : (uint16_t)slots;
}

uint16_t multi_sample_pool_get_zone_capacity_used(void)
{
    return g_multi_zone_count;
}

const multi_sample_instrument_t *multi_sample_pool_get_instrument(uint16_t instrument_id)
{
    if ((multi_sample_instrument_id_valid(instrument_id) == 0U)
        || (g_multi_instruments[instrument_id].used == 0U))
    {
        return 0;
    }

    const multi_sample_instrument_t *const instrument =
        &g_multi_instruments[instrument_id].desc;
    if (instrument->state == MULTI_SAMPLE_INSTRUMENT_READY)
    {
        __DMB();
    }
    return instrument;
}

const multi_sample_desc_t *multi_sample_pool_get_sample(uint16_t multi_sample_id)
{
    if (multi_sample_id >= g_multi_sample_count)
    {
        return 0;
    }

    return &g_multi_samples[multi_sample_id];
}

multi_sample_instrument_state_t multi_sample_pool_get_state(uint16_t instrument_id)
{
    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(instrument_id);
    return (instrument != 0) ? instrument->state : MULTI_SAMPLE_INSTRUMENT_EMPTY;
}

uint8_t multi_sample_pool_set_state(uint16_t instrument_id,
                                    multi_sample_instrument_state_t state)
{
    if ((multi_sample_instrument_id_valid(instrument_id) == 0U)
        || (g_multi_instruments[instrument_id].used == 0U)
        || (state == MULTI_SAMPLE_INSTRUMENT_EMPTY))
    {
        return 0U;
    }

    g_multi_instruments[instrument_id].desc.state = state;
    if (state == MULTI_SAMPLE_INSTRUMENT_READY)
    {
        __DMB();
        if (multi_sample_audio_projection_publish(instrument_id) == 0U)
        {
            g_multi_instruments[instrument_id].desc.state = MULTI_SAMPLE_INSTRUMENT_ERROR;
            return 0U;
        }
    }
    else
    {
        multi_sample_audio_projection_withdraw(instrument_id);
    }
    return 1U;
}

uint8_t multi_sample_pool_copy_zone(uint16_t zone_id, multi_sample_zone_t *out_zone)
{
    if ((out_zone == NULL) || (zone_id >= g_multi_zone_count))
        return 0U;
    *out_zone = g_multi_zones[zone_id];
    return 1U;
}

uint8_t multi_sample_pool_set_index_path(uint16_t instrument_id, const char *path)
{
    if ((multi_sample_instrument_id_valid(instrument_id) == 0U)
        || (g_multi_instruments[instrument_id].used == 0U))
    {
        return 0U;
    }

    return multi_sample_copy_text(g_multi_instruments[instrument_id].desc.index_path,
                                  sizeof(g_multi_instruments[instrument_id].desc.index_path),
                                  path);
}

uint8_t multi_sample_pool_set_instrument_format(uint16_t instrument_id,
                                                sample_audio_format_t format)
{
    if ((multi_sample_instrument_id_valid(instrument_id) == 0U)
        || (g_multi_instruments[instrument_id].used == 0U)
        || (sample_audio_format_is_valid(format) == 0U))
    {
        return 0U;
    }

    multi_sample_instrument_t *const instrument = &g_multi_instruments[instrument_id].desc;
    if (instrument->sample_count != 0U)
    {
        const uint32_t end_sample = (uint32_t)instrument->first_sample_id
                                    + instrument->sample_count;
        for (uint32_t sample_id = instrument->first_sample_id;
             (sample_id < end_sample) && (sample_id < g_multi_sample_count);
             ++sample_id)
        {
            const multi_sample_desc_t *const sample = &g_multi_samples[sample_id];
            if ((sample->format != format)
                || (sample->stride_floats
                    != sample_audio_format_stride_floats(format))
                || (sample->frames_per_page
                    != sample_audio_format_frames_per_page(format)))
            {
                return 0U;
            }
        }
    }

    instrument->format = format;
    instrument->stride_floats =
        (uint16_t)sample_audio_format_stride_floats(format);
    instrument->frames_per_page = sample_audio_format_frames_per_page(format);
    return 1U;
}

static uint8_t multi_sample_pool_finalize_clear_instrument(uint16_t instrument_id)
{
    if ((multi_sample_instrument_id_valid(instrument_id) == 0U)
        || (g_multi_instruments[instrument_id].used == 0U))
    {
        return 0U;
    }

    multi_sample_audio_projection_withdraw(instrument_id);
    sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_MULTI, instrument_id);

    const multi_sample_instrument_t *const instrument = &g_multi_instruments[instrument_id].desc;
    const uint32_t sample_end = (instrument->first_sample_id == MULTI_SAMPLE_POOL_INVALID_ID)
        ? g_multi_sample_count
        : ((uint32_t)instrument->first_sample_id + instrument->sample_count);
    const uint32_t zone_end = (instrument->first_zone_id == MULTI_SAMPLE_POOL_INVALID_ID)
        ? g_multi_zone_count
        : ((uint32_t)instrument->first_zone_id + instrument->zone_count);

    if (instrument->first_sample_id != MULTI_SAMPLE_POOL_INVALID_ID)
    {
        for (uint32_t sample_id = instrument->first_sample_id;
             (sample_id < sample_end) && (sample_id < MULTI_SAMPLE_POOL_MAX_SAMPLES);
             ++sample_id)
        {
            const sample_audio_key_t key = sample_audio_key_multi((uint16_t)sample_id);
            sample_stream_manager_release_key(key);
            sample_page_cache_clear_key(key);
        }
    }

    if (sample_end == g_multi_sample_count)
    {
        g_multi_sample_count = (instrument->first_sample_id == MULTI_SAMPLE_POOL_INVALID_ID)
            ? g_multi_sample_count
            : instrument->first_sample_id;
    }
    if (zone_end == g_multi_zone_count)
    {
        g_multi_zone_count = (instrument->first_zone_id == MULTI_SAMPLE_POOL_INVALID_ID)
            ? g_multi_zone_count
            : instrument->first_zone_id;
    }

    memset(&g_multi_instruments[instrument_id], 0, sizeof(g_multi_instruments[instrument_id]));
    g_multi_instruments[instrument_id].desc.id = instrument_id;
    g_multi_instruments[instrument_id].desc.state = MULTI_SAMPLE_INSTRUMENT_EMPTY;
    g_multi_instruments[instrument_id].desc.first_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_multi_instruments[instrument_id].desc.first_zone_id = MULTI_SAMPLE_POOL_INVALID_ID;
    return 1U;
}

uint8_t multi_sample_pool_clear_instrument(uint16_t instrument_id)
{
    if ((multi_sample_instrument_id_valid(instrument_id) == 0U)
        || (g_multi_instruments[instrument_id].used == 0U)) return 0U;
    multi_sample_instrument_t *const instrument = &g_multi_instruments[instrument_id].desc;
    if (instrument->state == MULTI_SAMPLE_INSTRUMENT_RETIRING) return 1U;
    if (instrument->state != MULTI_SAMPLE_INSTRUMENT_READY)
        return multi_sample_pool_finalize_clear_instrument(instrument_id);

    uint64_t due_sample = 0U;
    if (!live_clock_read_audio_sample(&due_sample)) return 0U;
    if (control_audio_publish_param_fenced((uint8_t)instrument_id, 0xFFF5U,
                                           0U, 0U, due_sample,
                                           &g_multi_retire_fence[instrument_id]) == 0U)
        return 0U;
    instrument->state = MULTI_SAMPLE_INSTRUMENT_RETIRING;
    __DMB();
    return 1U;
}

void multi_sample_pool_service_retire(void)
{
    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
    {
        if ((g_multi_instruments[i].used != 0U)
            && (g_multi_instruments[i].desc.state == MULTI_SAMPLE_INSTRUMENT_RETIRING)
            && (control_audio_consumer_fence_consumed(
                    g_multi_retire_fence[i]) != 0U))
        {
            g_multi_retire_fence[i] = 0U;
            (void)multi_sample_pool_finalize_clear_instrument(i);
        }
    }
}

uint8_t multi_sample_pool_resolve(uint16_t instrument_id,
                                  uint8_t note,
                                  uint8_t velocity,
                                  multi_sample_resolve_result_t *out_result)
{
    if (out_result != 0)
    {
        memset(out_result, 0, sizeof(*out_result));
        out_result->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    }

    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(instrument_id);
    if ((instrument == 0) || (out_result == 0)
        || (instrument->state != MULTI_SAMPLE_INSTRUMENT_READY)
        || (instrument->zone_count == 0U)
        || (instrument->first_zone_id >= MULTI_SAMPLE_POOL_MAX_ZONES))
    {
        return 0U;
    }

    const multi_sample_zone_t *best_zone = 0;
    const multi_sample_zone_t *fallback_zone = 0;
    uint8_t best_delta = UINT8_MAX;
    uint8_t fallback_delta = UINT8_MAX;
    const uint32_t end_zone = (uint32_t)instrument->first_zone_id + instrument->zone_count;
    for (uint32_t zone_id = instrument->first_zone_id;
         (zone_id < end_zone) && (zone_id < MULTI_SAMPLE_POOL_MAX_ZONES);
         ++zone_id)
    {
        const multi_sample_zone_t *const zone = &g_multi_zones[zone_id];
        if ((zone->multi_sample_id >= g_multi_sample_count)
            || (multi_sample_range_contains(zone->note_low, zone->note_high, note) == 0U))
        {
            continue;
        }

        const uint8_t delta = multi_sample_abs_note_delta(zone->root_note, note);
        if ((fallback_zone == 0) || (delta < fallback_delta))
        {
            fallback_zone = zone;
            fallback_delta = delta;
        }

        if (multi_sample_range_contains(zone->vel_low, zone->vel_high, velocity) == 0U)
        {
            continue;
        }

        if ((best_zone == 0) || (delta < best_delta))
        {
            best_zone = zone;
            best_delta = delta;
        }
    }

    if (best_zone == 0)
    {
        if (fallback_zone == 0)
        {
            return 0U;
        }

        const uint8_t fallback_layer_count =
            multi_sample_count_velocity_layers_for_note_root(instrument,
                                                             note,
                                                             fallback_zone->root_note);
        if (fallback_layer_count != 1U)
        {
            return 0U;
        }

        best_zone = fallback_zone;
    }

    const uint8_t velocity_layer_count =
        multi_sample_count_velocity_layers_for_note_root(instrument,
                                                         note,
                                                         best_zone->root_note);

    out_result->multi_sample_id = best_zone->multi_sample_id;
    out_result->zone_id = (uint16_t)(best_zone - &g_multi_zones[0]);
    out_result->root_note = best_zone->root_note;
    out_result->pitch_semitones = (int8_t)((int16_t)note - (int16_t)best_zone->root_note);
    out_result->vel_low = best_zone->vel_low;
    out_result->vel_high = best_zone->vel_high;
    out_result->velocity_layer_count_for_note = velocity_layer_count;
    out_result->zone_is_single_velocity_layer = (velocity_layer_count == 1U) ? 1U : 0U;
    return 1U;
}

uint8_t multi_sample_pool_debug_define_instrument(uint16_t instrument_id,
                                                  const char *name,
                                                  multi_sample_instrument_state_t state)
{
    if ((multi_sample_instrument_id_valid(instrument_id) == 0U)
        || (state == MULTI_SAMPLE_INSTRUMENT_EMPTY))
    {
        return 0U;
    }

    char copied_name[MULTI_SAMPLE_POOL_NAME_MAX];
    if (multi_sample_copy_text(copied_name, sizeof(copied_name), name) == 0U)
    {
        return 0U;
    }

    multi_sample_instrument_slot_t *const slot = &g_multi_instruments[instrument_id];
    if ((slot->used != 0U)
        && ((slot->desc.sample_count != 0U) || (slot->desc.zone_count != 0U)))
    {
        (void)multi_sample_pool_clear_instrument(instrument_id);
    }
    memset(slot, 0, sizeof(*slot));
    slot->used = 1U;
    slot->desc.id = instrument_id;
    slot->desc.state = state;
    slot->desc.first_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    slot->desc.first_zone_id = MULTI_SAMPLE_POOL_INVALID_ID;
    memcpy(slot->desc.name, copied_name, sizeof(slot->desc.name));
    return 1U;
}

uint8_t multi_sample_pool_debug_add_sample(uint16_t instrument_id,
                                           const char *path,
                                           uint32_t total_frames,
                                           uint8_t root_note,
                                           uint8_t vel_low,
                                           uint8_t vel_high,
                                           uint16_t flags,
                                           uint16_t *out_multi_sample_id)
{
    if (out_multi_sample_id != 0)
    {
        *out_multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    }

    if ((multi_sample_instrument_id_valid(instrument_id) == 0U)
        || (g_multi_instruments[instrument_id].used == 0U)
        || (g_multi_sample_count >= MULTI_SAMPLE_POOL_MAX_SAMPLES)
        || (total_frames == 0U)
        || (root_note > 127U)
        || (vel_low > vel_high)
        || (vel_high > 127U))
    {
        return 0U;
    }

    multi_sample_instrument_t *const instrument = &g_multi_instruments[instrument_id].desc;
    if ((instrument->sample_count != 0U)
        && (((uint32_t)instrument->first_sample_id + instrument->sample_count)
            != g_multi_sample_count))
    {
        return 0U;
    }

    const uint16_t sample_id = g_multi_sample_count++;
    multi_sample_desc_t *const sample = &g_multi_samples[sample_id];
    memset(sample, 0, sizeof(*sample));
    sample->multi_sample_id = sample_id;
    sample->instrument_id = instrument_id;
    sample->total_frames = total_frames;
    sample->root_note = root_note;
    sample->vel_low = vel_low;
    sample->vel_high = vel_high;
    sample->flags = flags;
    if (multi_sample_copy_text(sample->path, sizeof(sample->path), path) == 0U)
    {
        g_multi_sample_count--;
        memset(sample, 0, sizeof(*sample));
        return 0U;
    }

    if (instrument->sample_count == 0U)
    {
        instrument->first_sample_id = sample_id;
    }
    instrument->sample_count++;

    if (out_multi_sample_id != 0)
    {
        *out_multi_sample_id = sample_id;
    }
    return 1U;
}

uint8_t multi_sample_pool_set_sample_format(uint16_t multi_sample_id,
                                            uint32_t data_offset,
                                            uint32_t data_size,
                                            uint32_t sample_rate,
                                            uint16_t channels,
                                            uint16_t bits_per_sample)
{
    if ((multi_sample_id >= g_multi_sample_count)
        || (sample_rate == 0U)
        || (channels == 0U)
        || (bits_per_sample == 0U))
    {
        return 0U;
    }

    multi_sample_desc_t *const sample = &g_multi_samples[multi_sample_id];
    const sample_audio_format_t format = sample_audio_format_from_channels(channels);
    if ((sample_audio_format_is_valid(format) == 0U)
        || (g_multi_instruments[sample->instrument_id].used == 0U))
    {
        return 0U;
    }
    multi_sample_instrument_t *const instrument =
        &g_multi_instruments[sample->instrument_id].desc;
#if !BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST
    if ((sample_audio_format_is_valid(instrument->format) != 0U)
        && (instrument->format != format))
    {
        return 0U;
    }
#endif
    sample->data_offset = data_offset;
    sample->data_size = data_size;
    sample->sample_rate = sample_rate;
    sample->channels = channels;
    sample->bits_per_sample = bits_per_sample;
    sample->format = format;
    sample->stride_floats = (uint16_t)sample_audio_format_stride_floats(format);
    sample->frames_per_page = sample_audio_format_frames_per_page(format);
    sample->block_align = (uint16_t)((channels * bits_per_sample) / 8U);
#if BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST
    if ((sample_audio_format_is_valid(instrument->format) == 0U)
        && (instrument->sample_count == 1U))
    {
        (void)multi_sample_pool_set_instrument_format(sample->instrument_id, format);
    }
    else if ((sample_audio_format_is_valid(instrument->format) != 0U)
             && (instrument->format != format))
    {
        instrument->format = SAMPLE_AUDIO_FORMAT_INVALID;
        instrument->stride_floats = 0U;
        instrument->frames_per_page = 0U;
    }
#else
    if (sample_audio_format_is_valid(instrument->format) == 0U)
    {
        (void)multi_sample_pool_set_instrument_format(sample->instrument_id, format);
    }
#endif
    return (sample->block_align != 0U) ? 1U : 0U;
}

uint8_t multi_sample_pool_set_sample_loop(uint16_t multi_sample_id,
                                          uint8_t has_loop,
                                          uint32_t loop_begin,
                                          uint32_t loop_end)
{
    if (multi_sample_id >= g_multi_sample_count)
    {
        return 0U;
    }

    multi_sample_desc_t *const sample = &g_multi_samples[multi_sample_id];
    sample->has_loop = 0U;
    sample->loop_begin = 0U;
    sample->loop_end = sample->total_frames;

    if (has_loop == 0U)
    {
        return 1U;
    }
    if ((loop_end <= loop_begin) || (loop_end > sample->total_frames))
    {
        return 0U;
    }

    sample->has_loop = 1U;
    sample->loop_begin = loop_begin;
    sample->loop_end = loop_end;
    return 1U;
}

uint8_t multi_sample_pool_resolve_source_from_result(
    uint16_t instrument_id,
    uint8_t note,
    uint8_t velocity,
    const multi_sample_resolve_result_t *resolved,
    sample_resolved_source_t *out_source)
{
    if (out_source != 0)
    {
        sample_resolved_source_init(out_source);
    }
    if ((out_source == 0) || (resolved == 0)
        || (note > 127U) || (velocity > 127U))
    {
        return 0U;
    }

    const multi_sample_desc_t *const sample =
        multi_sample_pool_get_sample(resolved->multi_sample_id);
    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(instrument_id);
    if ((sample == 0) || (instrument == 0) || (sample->instrument_id != instrument_id)
        || (sample->total_frames == 0U)
        || (sample_audio_format_is_valid(sample->format) == 0U)
        || (sample->stride_floats != sample_audio_format_stride_floats(sample->format))
        || (sample->frames_per_page != sample_audio_format_frames_per_page(sample->format))
#if !BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST
        || (sample->format != instrument->format)
        || (sample->stride_floats != instrument->stride_floats)
        || (sample->frames_per_page != instrument->frames_per_page)
#endif
        )
    {
        return 0U;
    }

    out_source->key.domain = SAMPLE_AUDIO_DOMAIN_MULTI;
    out_source->key.object_id = resolved->multi_sample_id;
    out_source->path = sample->path;
    out_source->total_frames = sample->total_frames;
    out_source->data_offset = sample->data_offset;
    out_source->data_size = sample->data_size;
    out_source->sample_rate = sample->sample_rate;
    out_source->channels = sample->channels;
    out_source->bits_per_sample = sample->bits_per_sample;
    out_source->block_align = sample->block_align;
    out_source->format = sample->format;
    out_source->stride_floats = sample->stride_floats;
    out_source->frames_per_page = sample->frames_per_page;
    out_source->registration_epoch = 0U;
    out_source->root_note = resolved->root_note;
    out_source->fine_tune_cents = 0;
    out_source->region_begin = 0U;
    out_source->region_end = sample->total_frames;
    out_source->loop_begin = (sample->has_loop != 0U) ? sample->loop_begin : 0U;
    out_source->loop_end = (sample->has_loop != 0U) ? sample->loop_end : sample->total_frames;
    out_source->loop_mode = SAMPLE_PLAY_LOOP_NONE;
    out_source->reverse = 0U;
    out_source->rate = 1.0f;
    out_source->gain = 1.0f;
    out_source->owner_track_id = UINT8_MAX;
    out_source->note = note;
    out_source->velocity = velocity;
    out_source->source_kind = 0U;
    out_source->instrument_id = instrument_id;
    out_source->zone_id = resolved->zone_id;
    return sample_resolved_source_is_valid(out_source);
}

uint8_t multi_sample_pool_resolve_source(uint16_t instrument_id,
                                         uint8_t note,
                                         uint8_t velocity,
                                         sample_resolved_source_t *out_source)
{
    multi_sample_resolve_result_t resolved;
    if (multi_sample_pool_resolve(instrument_id, note, velocity, &resolved) == 0U)
    {
        if (out_source != 0)
        {
            sample_resolved_source_init(out_source);
        }
        return 0U;
    }
    return multi_sample_pool_resolve_source_from_result(instrument_id,
                                                        note,
                                                        velocity,
                                                        &resolved,
                                                        out_source);
}

uint8_t multi_sample_pool_debug_add_zone(uint16_t instrument_id,
                                         const multi_sample_zone_t *zone)
{
    if ((multi_sample_instrument_id_valid(instrument_id) == 0U)
        || (g_multi_instruments[instrument_id].used == 0U)
        || (zone == 0)
        || (g_multi_zone_count >= MULTI_SAMPLE_POOL_MAX_ZONES)
        || (zone->note_low > zone->note_high)
        || (zone->note_high > 127U)
        || (zone->vel_low > zone->vel_high)
        || (zone->vel_high > 127U)
        || (zone->root_note > 127U)
        || (zone->multi_sample_id >= g_multi_sample_count))
    {
        return 0U;
    }

    multi_sample_instrument_t *const instrument = &g_multi_instruments[instrument_id].desc;
    if ((instrument->zone_count != 0U)
        && (((uint32_t)instrument->first_zone_id + instrument->zone_count) != g_multi_zone_count))
    {
        return 0U;
    }

    const multi_sample_desc_t *const sample = &g_multi_samples[zone->multi_sample_id];
    if ((sample->instrument_id != instrument_id)
        || (sample->vel_low > zone->vel_low) || (sample->vel_high < zone->vel_high)
        || (sample_audio_format_is_valid(sample->format) == 0U)
        || (sample->stride_floats != sample_audio_format_stride_floats(sample->format))
        || (sample->frames_per_page != sample_audio_format_frames_per_page(sample->format))
#if !BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST
        || (sample->format != instrument->format)
        || (sample->stride_floats != instrument->stride_floats)
        || (sample->frames_per_page != instrument->frames_per_page)
#endif
        )
    {
        return 0U;
    }

    const uint16_t zone_id = g_multi_zone_count++;
    g_multi_zones[zone_id] = *zone;
    if (instrument->zone_count == 0U)
    {
        instrument->first_zone_id = zone_id;
    }
    instrument->zone_count++;
    multi_sample_instrument_recompute_note_range(instrument);
    return 1U;
}
