#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_stream_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The replacement design has three independent registries:
 *
 *   voice needs  : what a voice will need next;
 *   page records  : which physical pages exist in the cache;
 *   I/O records   : which transfer currently owns a destination.
 *
 * The voice-need registry is the sole logical demand authority. Page records
 * and I/O records remain physical execution state; neither can create a need
 * or acquire a scheduler-owned resource.
 */

typedef struct
{
    sample_audio_key_t key;
    uint32_t registration_epoch;
    uint32_t owner_token;
    uint32_t current_page;
    uint32_t loop_first_page;
    uint32_t loop_last_page;
    uint8_t active;
    uint8_t voice_index;
    uint8_t mobile_page_count;
    uint8_t loop_preload_count;
    uint8_t loop_enabled;
    uint8_t reserved[3];
} sample_stream_target_voice_registry_entry_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_STREAM_TARGET_MAX_NEEDS
                   == (SAMPLE_STREAM_TARGET_MAX_VOICES
                       * SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE),
               "stream need capacity must cover every target voice");
#endif

#ifdef __cplusplus
}
#endif
