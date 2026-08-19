#ifndef AUDIO_MODULATION_PROJECTION_AUDIO_H
#define AUDIO_MODULATION_PROJECTION_AUDIO_H

#include <stdint.h>

#include "Core/audio_modulation_projection.h"

#ifdef __cplusplus
extern "C" {
#endif

void audio_modulation_projection_audio_init(void);
void audio_modulation_projection_audio_consume(void);
uint8_t audio_modulation_projection_audio_resolve_owner(uint8_t entity_id,
                                                        uint8_t *out_owner_id);
uint8_t audio_modulation_projection_audio_is_group_master(uint8_t entity_id);
uint8_t audio_modulation_projection_audio_is_active(uint8_t entity_id);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MODULATION_PROJECTION_AUDIO_H */
