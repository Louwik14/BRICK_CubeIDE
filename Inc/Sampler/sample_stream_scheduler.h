#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_page_lease.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_SCHEDULER_SLOT_COUNT SAMPLE_PAGE_LEASE_SLOT_COUNT
#define SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES SAMPLE_STREAM_SCHEDULER_SLOT_COUNT

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

typedef struct
{
    uint8_t candidate_index;
    uint8_t round_robin_slot;
    uint8_t reserved[2];
} sample_stream_scheduler_decision_t;

void sample_stream_scheduler_init(void);
void sample_stream_scheduler_begin_round(void);
uint8_t sample_stream_scheduler_round_active(void);
uint8_t sample_stream_scheduler_pick(
    const sample_stream_scheduler_candidate_t *candidates,
    uint32_t candidate_count,
    sample_stream_scheduler_decision_t *out_decision);

#ifdef __cplusplus
}
#endif
