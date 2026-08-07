#include "Sampler/sample_stream_admission.h"

sample_stream_admission_result_t sample_stream_admission_sync_snapshot(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    const sample_stream_snapshot_t *snapshot)
{
    (void)source;
    (void)voice_id;
    (void)snapshot;
    return SAMPLE_STREAM_ADMISSION_OK;
}

void sample_stream_admission_release_voice(sample_stream_snapshot_source_t source,
                                           uint8_t voice_id)
{
    (void)source;
    (void)voice_id;
}
