#include "Storage/looper_stream_control.h"

#include <string.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_page_cache.h"
#include "Seq/seq_types.h"
#include "Storage/audio_recorder.h"
#include "Storage/audio_recorder_wav.h"

static uint32_t g_looper_stream_generation[SEQ_TRACK_COUNT];

void looper_stream_control_init(void)
{
    memset(g_looper_stream_generation, 0, sizeof(g_looper_stream_generation));
}

void looper_stream_control_service(void)
{
    uint8_t track = UINT8_MAX;
    audio_recorder_live_stream_t live;
    if ((audio_recorder_control_looper_take_track(&track) == 0U)
        || (track >= SEQ_TRACK_COUNT)
        || (audio_recorder_get_live_stream(AUDIO_RECORDER_CLIENT_LOOPER,
                                           &live) == 0U)
        || (live.accepted_frames == 0U)
        || (live.reservation.reserved_file_bytes > UINT32_MAX)) return;

    const sample_audio_key_t key = sample_audio_key_looper(track);
    const uint32_t generation = live.reservation.generation;
    if (g_looper_stream_generation[track] != generation)
    {
        if (g_looper_stream_generation[track] != 0U)
            sample_page_cache_clear_key(key);
        if (sample_page_cache_register_live_pcm24_stereo_sample_key(
                key, live.path, live.accepted_frames, live.committed_frames,
                AUDIO_RECORDER_WAV_HEADER_BYTES,
                (uint32_t)live.reservation.reserved_file_bytes,
                live.reservation.extents, live.reservation.extent_count,
                live.reservation.media_epoch) == 0U) return;
        g_looper_stream_generation[track] = generation;
    }
    else
    {
        (void)sample_page_cache_update_live_map_key(
            key, (uint32_t)live.reservation.reserved_file_bytes,
            live.reservation.extents, live.reservation.extent_count,
            live.reservation.media_epoch);
        (void)sample_page_cache_update_readable_frames_key(
            key, live.committed_frames);
    }
    (void)sample_page_cache_update_stream_path_key(key, live.path);
}
