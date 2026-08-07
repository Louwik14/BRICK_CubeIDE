#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_audio_format.h"
#include "Sampler/sample_stream_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed registry dimensions for the two DSP voice pools. */
#define SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY (16U)
#define SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY   SAMPLE_STREAM_TARGET_MAX_VOICES
#define SAMPLE_STREAM_SNAPSHOT_CAPACITY \
    (SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY + SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY)

typedef enum
{
    SAMPLE_STREAM_SNAPSHOT_CLASSIC = 0,
    SAMPLE_STREAM_SNAPSHOT_MULTI
} sample_stream_snapshot_source_t;

/*
 * A snapshot is the common, pointer-free projection of a DSP voice.  It is
 * not a request and does not imply page allocation or I/O ownership.
 */
typedef struct
{
    sample_audio_key_t key;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t generation;
    uint32_t current_frame;
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t loop_begin;
    uint32_t loop_end;
    uint32_t step_q16;
    uint8_t source;
    uint8_t voice_id;
    uint8_t active;
    uint8_t loop_enabled;
    int8_t direction;
    uint8_t reserved[3];
} sample_stream_snapshot_t;

void sample_stream_snapshot_init(sample_stream_snapshot_t *snapshot);
void sample_stream_snapshot_registry_reset(void);
uint8_t sample_stream_snapshot_publish(sample_stream_snapshot_source_t source,
                                       uint8_t voice_id,
                                       const sample_stream_snapshot_t *snapshot);
uint8_t sample_stream_snapshot_read(sample_stream_snapshot_source_t source,
                                    uint8_t voice_id,
                                    sample_stream_snapshot_t *out_snapshot);
void sample_stream_snapshot_clear(sample_stream_snapshot_source_t source,
                                  uint8_t voice_id);
uint8_t sample_stream_snapshot_clear_generation(sample_stream_snapshot_source_t source,
                                                uint8_t voice_id,
                                                uint32_t generation);

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_STREAM_SNAPSHOT_CAPACITY <= UINT8_MAX,
               "snapshot registry index must fit in uint8_t");
_Static_assert(sizeof(sample_stream_snapshot_t) <= 64U,
               "stream snapshot must remain bounded and pointer-free");
#endif

#ifdef __cplusplus
}
#endif
