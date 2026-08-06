#ifndef BRICK6_LIVE_PARAMETER_MIGRATION_H
#define BRICK6_LIVE_PARAMETER_MIGRATION_H

#include "param_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns non-zero for parameters whose live value is owned by the audio
 * timeline rather than by the UI/control task. */
uint8_t live_parameter_is_audio_owned(param_id_t parameter);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_LIVE_PARAMETER_MIGRATION_H */
