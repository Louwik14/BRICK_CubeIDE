#ifndef BRICK6_LIVE_PARAMETER_AUDIO_QUEUE_H
#define BRICK6_LIVE_PARAMETER_AUDIO_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include "Core/live_parameter_event.h"

#define LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS 64U

/* Fixed, pointer-free control payload for clipboard/restore transactions. */
typedef struct
{
    uint16_t parameter_id;
    uint8_t scope;
    uint8_t track;
    uint8_t slot;
    uint8_t reserved;
    uint16_t flags;
    int32_t value;
} live_parameter_audio_bulk_item_t;

typedef struct
{
    uint32_t capture_tick;
    uint8_t source;
    uint8_t count;
    uint16_t reserved;
    live_parameter_audio_bulk_item_t item[LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS];
} live_parameter_audio_bulk_t;

_Static_assert(sizeof(live_parameter_audio_bulk_item_t) == 12U,
               "live_parameter_audio_bulk_item_t must remain fixed-width");
_Static_assert(sizeof(live_parameter_audio_bulk_t)
                   == (8U + (LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS * 12U)),
               "live_parameter_audio_bulk_t must remain pointer-free");

/* The converted event remains pointer-free after it crosses the control
 * boundary.  effective_sample_time is the only timestamp used by audio. */
typedef struct
{
    uint64_t effective_sample_time;
    uint32_t capture_tick;
    uint32_t ingress_serial;
    uint16_t parameter_id;
    uint8_t source;
    uint8_t scope;
    uint8_t track;
    uint8_t slot;
    uint16_t flags;
    int32_t value;
} live_parameter_audio_event_t;

_Static_assert(sizeof(live_parameter_audio_event_t) == 32U,
               "live_parameter_audio_event_t must remain a fixed 32-byte event");

#define LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY 64U
#define LIVE_PARAMETER_AUDIO_DRAIN_BUDGET LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY
#define LIVE_PARAMETER_AUDIO_STALE_THRESHOLD_SAMPLES 48000ULL

_Static_assert((LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY
                & (LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY - 1U)) == 0U,
               "live parameter audio queue capacity must be a power of two");

typedef struct
{
    uint16_t scheduled_depth;
    uint16_t due_depth;
    uint16_t high_water;
    uint16_t due_high_water;
    uint32_t late_count;
    uint32_t stale_count;
    uint32_t conversion_drop_count;
    uint32_t queue_drop_count;
    uint32_t coalesced_count;
    uint32_t due_drop_count;
    uint32_t bulk_reject_count;
    uint64_t max_lateness_samples;
} live_parameter_audio_queue_diag_t;

void live_parameter_audio_queue_init(void);

/* Control-side handoff: converts capture_tick exactly once and schedules the
 * resulting event by effective sample time. */
uint16_t live_parameter_audio_queue_drain(void);

/* Submit a complete fixed-size transaction.  Conversion, capacity checking
 * and publication are all-or-nothing; the caller retains no queue pointer. */
bool live_parameter_audio_queue_submit_bulk(const live_parameter_audio_bulk_t *bulk);

/* Audio-side deadline and due handoff.  The due FIFO is deliberately separate
 * from the note queue and is the application seam for the next pass. */
uint16_t live_parameter_audio_queue_frames_until_deadline(uint64_t block_start,
                                                          uint16_t max_frames);
uint16_t live_parameter_audio_queue_consume_due(uint64_t now);
bool live_parameter_audio_queue_pop_due(live_parameter_audio_event_t *out_event);

uint16_t live_parameter_audio_queue_scheduled_depth(void);
uint16_t live_parameter_audio_queue_due_depth(void);
void live_parameter_audio_queue_get_diag(live_parameter_audio_queue_diag_t *out_diag);

#endif /* BRICK6_LIVE_PARAMETER_AUDIO_QUEUE_H */
