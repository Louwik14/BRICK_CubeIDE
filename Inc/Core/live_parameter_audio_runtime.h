#ifndef BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H
#define BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H

#include <stdint.h>

#define LIVE_PARAMETER_AUDIO_RUNTIME_SLOT_CAPACITY 64U

void live_parameter_audio_runtime_init(void);

/* Apply all events already due at the current audio sample. */
uint16_t live_parameter_audio_runtime_apply_due(uint64_t now);

/* Kept as an audio-span seam; DSP-owned ramps advance in their own engines. */
void live_parameter_audio_runtime_process(uint64_t block_start,
                                          uint16_t frames);

#endif /* BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H */
