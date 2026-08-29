#ifndef BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H
#define BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H

#include <stdint.h>

void live_parameter_audio_runtime_init(void);
uint8_t live_parameter_audio_runtime_tone_get(uint8_t entity, uint8_t slot,
                                              float *out_normalized);
uint8_t live_parameter_audio_runtime_apply_tone_slot(uint8_t entity,
                                                     uint8_t slot,
                                                     float normalized);

/* Apply one final PARAM command at the current audio sample. */
uint8_t live_parameter_audio_runtime_apply_param(uint8_t entity,
                                                 uint16_t parameter_id,
                                                 uint32_t value_bits,
                                                 uint8_t scope);

#endif /* BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H */
