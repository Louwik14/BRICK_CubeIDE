#ifndef BRICK6_LIVE_PARAMETER_AUDIO_PUBLICATION_H
#define BRICK6_LIVE_PARAMETER_AUDIO_PUBLICATION_H

#include <stdbool.h>
#include <stdint.h>

#include "Seq/seq_types.h"
#include "Track/track_types.h"

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
    uint8_t count;
    live_parameter_audio_bulk_item_t item[LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS];
} live_parameter_audio_bulk_t;

_Static_assert(sizeof(live_parameter_audio_bulk_item_t) == 12U,
               "live_parameter_audio_bulk_item_t must remain fixed-width");
_Static_assert(sizeof(live_parameter_audio_bulk_t)
                   == (8U + (LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS * 12U)),
               "live_parameter_audio_bulk_t must remain pointer-free");

/* CONTROL-owned publication boundary; AUDIO only consumes the resulting FIFO. */
void live_parameter_audio_publication_init(void);
bool live_parameter_audio_publication_submit_bulk(
    const live_parameter_audio_bulk_t *bulk);
bool live_parameter_audio_publication_submit_tone_program(
    uint8_t track, track_runtime_type_t type);
bool live_parameter_audio_publication_submit_dated(
    uint64_t effective_sample_time, uint16_t parameter_id,
    uint8_t track, uint16_t value16);

#endif /* BRICK6_LIVE_PARAMETER_AUDIO_PUBLICATION_H */
