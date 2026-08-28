#ifndef BRICK6_LIVE_PARAMETER_AUDIO_PUBLICATION_H
#define BRICK6_LIVE_PARAMETER_AUDIO_PUBLICATION_H

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
    uint16_t flags;
    int32_t value;
} live_parameter_audio_bulk_item_t;

typedef struct
{
    uint32_t capture_tick;
    uint8_t source;
    uint8_t count;
    live_parameter_audio_bulk_item_t item[LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS];
} live_parameter_audio_bulk_t;

_Static_assert(sizeof(live_parameter_audio_bulk_item_t) == 12U,
               "live_parameter_audio_bulk_item_t must remain fixed-width");
_Static_assert(sizeof(live_parameter_audio_bulk_t)
                   == (8U + (LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS * 12U)),
               "live_parameter_audio_bulk_t must remain pointer-free");

#define LIVE_PARAMETER_CONTROL_EVENT_DRAIN_BUDGET 64U

void live_parameter_audio_publication_init(void);

/* Control-side handoff: converts capture_tick exactly once and schedules the
 * resulting event by effective sample time. */
uint16_t live_parameter_audio_publication_drain(void);

/* Submit a complete fixed-size transaction.  Conversion, capacity checking
 * and publication are all-or-nothing; the caller retains no staging pointer. */
bool live_parameter_audio_publication_submit_bulk(const live_parameter_audio_bulk_t *bulk);
bool live_parameter_audio_publication_submit_poly_pair(uint32_t capture_tick,
                                                 uint8_t track,
                                                 float voices,
                                                 float spread);
bool live_parameter_audio_publication_submit_dated(uint64_t effective_sample_time,
                                             uint16_t parameter_id,
                                             uint8_t track,
                                             uint16_t value16);

#endif /* BRICK6_LIVE_PARAMETER_AUDIO_PUBLICATION_H */
