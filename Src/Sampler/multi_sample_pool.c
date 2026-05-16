#include "Sampler/multi_sample_pool.h"

#include <string.h>

#include "Storage/memory_layout.h"

typedef struct
{
    uint8_t used;
    multi_sample_instrument_t desc;
} multi_sample_instrument_slot_t;

static CTRL_STATE multi_sample_instrument_slot_t
    g_multi_instruments[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];
SDRAM_SAMPLES static multi_sample_desc_t g_multi_samples[MULTI_SAMPLE_POOL_MAX_SAMPLES];
SDRAM_SAMPLES static multi_sample_zone_t g_multi_zones[MULTI_SAMPLE_POOL_MAX_ZONES];
static CTRL_STATE uint16_t g_multi_sample_count;
static CTRL_STATE uint16_t g_multi_zone_count;

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

    return &g_multi_instruments[instrument_id].desc;
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

uint8_t multi_sample_pool_clear_instrument(uint16_t instrument_id)
{
    if ((multi_sample_instrument_id_valid(instrument_id) == 0U)
        || (g_multi_instruments[instrument_id].used == 0U))
    {
        return 0U;
    }

    const multi_sample_instrument_t *const instrument = &g_multi_instruments[instrument_id].desc;
    const uint32_t sample_end = (instrument->first_sample_id == MULTI_SAMPLE_POOL_INVALID_ID)
        ? g_multi_sample_count
        : ((uint32_t)instrument->first_sample_id + instrument->sample_count);
    const uint32_t zone_end = (instrument->first_zone_id == MULTI_SAMPLE_POOL_INVALID_ID)
        ? g_multi_zone_count
        : ((uint32_t)instrument->first_zone_id + instrument->zone_count);

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
    if ((sample->vel_low > zone->vel_low) || (sample->vel_high < zone->vel_high))
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
