#include <assert.h>
#include <stdint.h>

#include "Sampler/sample_stream_needs.h"

static sample_stream_snapshot_t make_snapshot(uint8_t voice_id, uint32_t generation)
{
    sample_stream_snapshot_t snapshot;
    sample_stream_snapshot_init(&snapshot);
    snapshot.key.domain = SAMPLE_AUDIO_DOMAIN_MULTI;
    snapshot.key.object_id = 4U;
    snapshot.format = SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
    snapshot.frames_per_page = sample_audio_format_frames_per_page(snapshot.format);
    snapshot.registration_epoch = 9U;
    snapshot.generation = generation;
    snapshot.current_frame = 15000U;
    snapshot.region_begin = 0U;
    snapshot.region_end = 30000U;
    snapshot.loop_begin = 2048U;
    snapshot.loop_end = 25000U;
    snapshot.loop_enabled = 1U;
    snapshot.step_q16 = 65536U;
    snapshot.direction = 1;
    snapshot.active = 1U;
    snapshot.voice_id = voice_id;
    return snapshot;
}

int main(void)
{
    sample_stream_needs_registry_reset();
    sample_stream_snapshot_t first = make_snapshot(2U, 12U);
    assert(sample_stream_needs_registry_update(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                               first.voice_id,
                                               &first,
                                               1000U) != 0U);

    sample_stream_target_voice_registry_entry_t entry;
    assert(sample_stream_needs_registry_read(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                             first.voice_id,
                                             &entry) != 0U);
    assert(entry.need_count != 0U);
    assert(entry.need_count <= SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE);
    assert(entry.needs[0].consume_deadline_audio_frame == 1000U);
    assert(sample_stream_needs_registry_contains_any(first.key,
                                                     entry.needs[0].page_index,
                                                     first.registration_epoch) != 0U);
    assert(sample_stream_needs_registry_contains_key(first.key) != 0U);

    uint8_t saw_loop_preload = 0U;
    for (uint8_t i = 0U; i < entry.need_count; ++i)
    {
        if (entry.needs[i].role == SAMPLE_STREAM_NEED_ROLE_LOOP)
        {
            saw_loop_preload = 1U;
            assert(entry.needs[i].consume_deadline_audio_frame
                   == SAMPLE_STREAM_AUDIO_FRAME_NEVER);
        }
        for (uint8_t j = (uint8_t)(i + 1U); j < entry.need_count; ++j)
        {
            assert(entry.needs[i].page_index != entry.needs[j].page_index);
        }
    }
    assert(saw_loop_preload != 0U);

    sample_stream_snapshot_t shared = make_snapshot(3U, 13U);
    assert(sample_stream_needs_registry_update(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                               shared.voice_id,
                                               &shared,
                                               2000U) != 0U);
    assert(sample_stream_needs_registry_drop_generation(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                                        first.voice_id,
                                                        first.generation + 1U) == 0U);
    assert(sample_stream_needs_registry_drop_generation(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                                        first.voice_id,
                                                        first.generation) != 0U);
    assert(sample_stream_needs_registry_contains_key(shared.key) != 0U);

    shared.direction = -1;
    assert(sample_stream_needs_build(&shared, 0U, &entry) == 0U);
    shared.direction = 1;
    shared.loop_begin = 9000U;
    shared.loop_end = 8000U;
    assert(sample_stream_needs_build(&shared, 0U, &entry) == 0U);

    sample_stream_needs_registry_drop(SAMPLE_STREAM_SNAPSHOT_MULTI, shared.voice_id);
    assert(sample_stream_needs_registry_has_active() == 0U);
    assert(sample_stream_needs_registry_contains_key(shared.key) == 0U);
    return 0;
}
