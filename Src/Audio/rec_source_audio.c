#include "Audio/rec_source_audio.h"

#include "IPC/rec_source_contract.h"
#include "Platform/intercore_cache.h"
#include "Sampler/sample_audio_format.h"

uint8_t rec_source_audio_resolve(sample_resolved_source_t *out_source)
{
    if (out_source != NULL) sample_resolved_source_init(out_source);
    if (out_source == NULL) return 0U;
    intercore_cache_consume((const void *)&g_rec_source_projection.active_snapshot,
                            sizeof(g_rec_source_projection.active_snapshot));
    const uint32_t active = g_rec_source_projection.active_snapshot;
    intercore_cache_consume(&g_rec_source_projection.snapshots[active],
                            sizeof(g_rec_source_projection.snapshots[active]));
    const rec_source_snapshot_t snapshot =
        g_rec_source_projection.snapshots[active];
    if ((snapshot.ready == 0U) || (snapshot.frame_count == 0U)) return 0U;
    out_source->key = snapshot.key;
    out_source->total_frames = snapshot.frame_count;
    out_source->data_size = snapshot.frame_count * 6U;
    out_source->data_offset = 512U;
    out_source->sample_rate = snapshot.sample_rate;
    out_source->registration_epoch = snapshot.registration_epoch;
    out_source->format = SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
    out_source->channels = 2U;
    out_source->bits_per_sample = 24U;
    out_source->block_align = 6U;
    out_source->stride_floats = 2U;
    out_source->frames_per_page = SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE;
    out_source->root_note = 60U;
    out_source->region_end = snapshot.frame_count;
    out_source->loop_end = snapshot.frame_count;
    out_source->rate = 1.0f;
    out_source->gain = 1.0f;
    out_source->owner_track_id = UINT8_MAX;
    out_source->note = 60U;
    out_source->velocity = 127U;
    out_source->instrument_id = UINT16_MAX;
    out_source->zone_id = UINT16_MAX;
    return sample_resolved_source_is_valid(out_source);
}
