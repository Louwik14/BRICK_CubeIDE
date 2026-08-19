#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AUDIO-side consumer of the complete CONTROL global command payload. */
uint8_t audio_global_runtime_apply(uint16_t parameter_id, float command_value);

#ifdef __cplusplus
}
#endif
