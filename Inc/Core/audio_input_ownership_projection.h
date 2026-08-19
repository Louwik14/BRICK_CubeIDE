#ifndef AUDIO_INPUT_OWNERSHIP_PROJECTION_H
#define AUDIO_INPUT_OWNERSHIP_PROJECTION_H

#include <stdint.h>

#include "Core/entity_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t owner_entity_id;
    uint8_t valid;
    uint16_t reserved;
} audio_input_ownership_entry_t;

_Static_assert(sizeof(audio_input_ownership_entry_t) == 4U,
               "AUDIO input ownership entry must remain compact");

void audio_input_ownership_projection_init(void);
void audio_input_ownership_projection_publish(void);

void audio_input_ownership_projection_audio_init(void);
void audio_input_ownership_projection_audio_consume(void);
uint8_t audio_input_ownership_projection_audio_get_owner(
    uint8_t input, uint8_t *out_owner_entity_id);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_INPUT_OWNERSHIP_PROJECTION_H */
