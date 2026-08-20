#ifndef AUDIO_MODULATION_PROJECTION_H
#define AUDIO_MODULATION_PROJECTION_H

#include <stdint.h>

#include "Seq/seq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t mod_owner_id;
    uint8_t is_group_master;
    uint8_t active;
    uint8_t reserved;
} audio_modulation_topology_entry_t;

_Static_assert(sizeof(audio_modulation_topology_entry_t) == 4U,
               "AUDIO topology projection entry must remain compact");

void audio_modulation_projection_init(void);
void audio_modulation_projection_publish(void);
void audio_modulation_configuration_publish(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MODULATION_PROJECTION_H */
