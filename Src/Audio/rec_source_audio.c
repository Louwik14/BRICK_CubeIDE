#include "Audio/rec_source_audio.h"

#include "IPC/rec_source_contract.h"
#include "Sampler/sample_audio_format.h"
#include "stm32h7xx.h"

uint8_t rec_source_audio_resolve(sample_resolved_source_t *out_source)
{
    if (out_source != NULL) sample_resolved_source_init(out_source);
    if (out_source == NULL) return 0U;
    if (g_rec_source_projection.ready == 0U) return 0U;
    __DMB();
    const rec_source_snapshot_t snapshot = g_rec_source_projection;
    __DMB();
    if ((snapshot.ready == 0U) || (g_rec_source_projection.ready == 0U)
        || (snapshot.frame_count == 0U)) return 0U;
    out_source->key = snapshot.key;
    out_source->total_frames = snapshot.frame_count;
    out_source->registration_epoch = snapshot.registration_epoch;
    out_source->format = SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
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
