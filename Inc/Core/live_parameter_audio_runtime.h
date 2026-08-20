#ifndef BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H
#define BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H

#include <stdint.h>

void live_parameter_audio_runtime_init(void);

/* Apply all events already due at the current audio sample. */
uint16_t live_parameter_audio_runtime_apply_due(uint64_t now);

#endif /* BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H */
