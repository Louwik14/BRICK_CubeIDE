#include <assert.h>

#include "Sampler/sample_stream_snapshot.h"

int main(void)
{
    sample_stream_snapshot_registry_reset();

    sample_stream_snapshot_t published;
    sample_stream_snapshot_init(&published);
    published.active = 1U;
    published.generation = 17U;
    published.current_frame = 2048U;
    published.region_end = 8192U;
    published.step_q16 = 65536U;
    assert(sample_stream_snapshot_publish(SAMPLE_STREAM_SNAPSHOT_CLASSIC,
                                          3U,
                                          &published) != 0U);

    sample_stream_snapshot_t readback;
    sample_stream_snapshot_init(&readback);
    assert(sample_stream_snapshot_read(SAMPLE_STREAM_SNAPSHOT_CLASSIC,
                                       3U,
                                       &readback) != 0U);
    assert(readback.active != 0U);
    assert(readback.source == (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC);
    assert(readback.voice_id == 3U);
    assert(readback.generation == 17U);
    assert(readback.current_frame == 2048U);

    sample_stream_snapshot_clear(SAMPLE_STREAM_SNAPSHOT_CLASSIC, 3U);
    assert(sample_stream_snapshot_read(SAMPLE_STREAM_SNAPSHOT_CLASSIC,
                                       3U,
                                       &readback) != 0U);
    assert(readback.active == 0U);

    published.active = 1U;
    published.generation = 42U;
    assert(sample_stream_snapshot_publish(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                          SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY - 1U,
                                          &published) != 0U);
    assert(sample_stream_snapshot_read(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                       SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY - 1U,
                                       &readback) != 0U);
    assert(readback.source == (uint8_t)SAMPLE_STREAM_SNAPSHOT_MULTI);
    assert(readback.generation == 42U);

    assert(sample_stream_snapshot_publish(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                          SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY,
                                          &published) == 0U);
    return 0;
}
