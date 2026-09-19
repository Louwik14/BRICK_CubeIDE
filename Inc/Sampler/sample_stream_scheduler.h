#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_page_lease.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_SCHEDULER_SLOT_COUNT SAMPLE_PAGE_LEASE_SLOT_COUNT

/* One candidate is the first non-ready page derived for one lease. */
typedef struct
{
    sample_audio_key_t key;
    uint32_t page_index;
    uint32_t registration_epoch;
    uint8_t voice_id;
    uint8_t page_rank;
    uint8_t round_robin_slot;
    uint8_t active;
} sample_stream_scheduler_candidate_t;

typedef uint8_t (*sample_stream_scheduler_probe_fn)(
    void *context,
    uint8_t round_robin_slot,
    sample_stream_scheduler_candidate_t *out_candidate);

void sample_stream_scheduler_init(void);
void sample_stream_scheduler_begin_round(void);
uint8_t sample_stream_scheduler_round_active(void);
uint8_t sample_stream_scheduler_pick(
    sample_stream_scheduler_probe_fn probe,
    void *context,
    sample_stream_scheduler_candidate_t *out_candidate);

#ifdef __cplusplus
}
#endif
