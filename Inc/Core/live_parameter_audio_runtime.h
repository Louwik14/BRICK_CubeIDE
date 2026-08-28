#ifndef BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H
#define BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H

#include <stdint.h>

void live_parameter_audio_runtime_init(void);

/* Apply one final PARAM command at the current audio sample. */
uint8_t live_parameter_audio_runtime_apply_param(uint8_t entity,
                                                 uint16_t parameter_id,
                                                 uint32_t value_bits,
                                                 uint8_t scope);

void live_parameter_audio_runtime_initialize_program(uint8_t entity);

#endif /* BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H */
