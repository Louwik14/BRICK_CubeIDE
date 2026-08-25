#ifndef BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H
#define BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H

#include <stdint.h>

#include "Param/param_store.h"

void live_parameter_audio_runtime_init(void);

/* Apply all events already due at the current audio sample. */
uint16_t live_parameter_audio_runtime_apply_due(uint64_t now);

/* H743 local AUDIO seam used by the AUDIO-owned boundary engine.  It applies
 * the same pointer-free command without turning the shared ring into MPSC. */
uint8_t live_parameter_audio_runtime_apply_temp(param_id_t parameter,
                                                uint8_t track,
                                                float value,
                                                uint8_t matrix_operation);

#endif /* BRICK6_LIVE_PARAMETER_AUDIO_RUNTIME_H */
