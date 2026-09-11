#include "IPC/sample_classic_audio_projection_contract.h"
#include "Audio/sample_classic_audio_projection_audio.h"

#include "Platform/intercore_cache.h"

uint8_t sample_classic_audio_projection_is_ready(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY) return 0U;
    const sample_classic_audio_source_t *const src =
        &g_sample_classic_audio_source[sample_id];
    intercore_cache_consume((const void *)&src->active_snapshot,
                            sizeof(src->active_snapshot));
    const uint32_t active = src->active_snapshot;
    intercore_cache_consume(&src->snapshots[active],
                            sizeof(src->snapshots[active]));
    return src->snapshots[active].ready;
}

uint8_t sample_classic_audio_projection_resolve(uint16_t sample_id,
                                                sample_resolved_source_t *out)
{
    if (out != 0) sample_resolved_source_init(out);
    if ((out == 0) || (sample_id >= SAMPLE_CLASSIC_CAPACITY)) return 0U;
    const sample_classic_audio_source_t *const src = &g_sample_classic_audio_source[sample_id];
    intercore_cache_consume((const void *)&src->active_snapshot,
                            sizeof(src->active_snapshot));
    const uint32_t active = src->active_snapshot;
    intercore_cache_consume(&src->snapshots[active],
                            sizeof(src->snapshots[active]));
    const sample_classic_audio_snapshot_t snap = src->snapshots[active];
    if (snap.ready == 0U) return 0U;
    out->key = snap.key;
    out->total_frames = snap.total_frames;
    out->data_offset = snap.data_offset;
    out->data_size = snap.data_size;
    out->sample_rate = snap.sample_rate;
    out->registration_epoch = snap.registration_epoch;
    out->format = snap.format;
    out->channels = snap.channels;
    out->bits_per_sample = snap.bits_per_sample;
    out->block_align = snap.block_align;
    out->stride_floats = snap.stride_floats;
    out->frames_per_page = snap.frames_per_page;
    out->root_note = 60U;
    out->region_end = snap.total_frames;
    out->loop_end = snap.total_frames;
    out->rate = 1.0f;
    out->gain = 1.0f;
    out->owner_track_id = UINT8_MAX;
    out->note = 60U;
    out->velocity = 127U;
    out->instrument_id = UINT16_MAX;
    out->zone_id = UINT16_MAX;
    return sample_resolved_source_is_valid(out);
}
