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
    uint8_t matrix_operation;
    uint8_t reserved[3];
} live_parameter_audio_event_t;

typedef enum
{
    LIVE_PARAMETER_MATRIX_OPERATION_NONE = 0U,
    LIVE_PARAMETER_MATRIX_OPERATION_OVERRIDE_SET = 1U,
    LIVE_PARAMETER_MATRIX_OPERATION_OVERRIDE_CLEAR = 2U,
    /* AUDIO-only temporary LFO release; it must not touch Matrix state. */
    LIVE_PARAMETER_MATRIX_OPERATION_LFO_TEMP_CLEAR = 3U
} live_parameter_matrix_operation_t;

_Static_assert(sizeof(live_parameter_audio_event_t) == 32U,
               "live_parameter_audio_event_t must remain a fixed 32-byte event");

#define LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY 64U
#define LIVE_PARAMETER_AUDIO_DRAIN_BUDGET LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY

_Static_assert((LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY
                & (LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY - 1U)) == 0U,
               "live parameter audio queue capacity must be a power of two");

void live_parameter_audio_queue_init(void);

/* Control-side handoff: converts capture_tick exactly once and schedules the
 * resulting event by effective sample time. */
uint16_t live_parameter_audio_queue_drain(void);

/* Submit a complete fixed-size transaction.  Conversion, capacity checking
 * and publication are all-or-nothing; the caller retains no queue pointer. */
bool live_parameter_audio_queue_submit_bulk(const live_parameter_audio_bulk_t *bulk);
bool live_parameter_audio_queue_submit_poly_pair(uint32_t capture_tick,
                                                 uint8_t track,
                                                 float voices,
                                                 float spread);
uint32_t live_parameter_audio_queue_publish_failure_count(void);

/* Audio-side deadline and direct scheduled-prefix handoff.  A claim keeps the
 * prefix immutable while CONTROL continues to publish into the suffix. */
uint16_t live_parameter_audio_queue_frames_until_deadline(uint64_t block_start,
                                                          uint16_t max_frames);
uint16_t live_parameter_audio_queue_claim_due(uint64_t now);
bool live_parameter_audio_queue_read_claimed(
    uint16_t index, live_parameter_audio_event_t *out_event);
void live_parameter_audio_queue_release_claimed(void);

#endif /* BRICK6_LIVE_PARAMETER_AUDIO_QUEUE_H */
