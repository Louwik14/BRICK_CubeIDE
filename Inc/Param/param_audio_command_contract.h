#pragma once

/* Command-domain contract shared by CONTROL projection, validation and the
 * AUDIO consumer.  Persisted MOD FX values remain in their 0..127 CONTROL
 * representation; only FIFO payloads use these engine-domain bounds. */
#define PARAM_AUDIO_MODFX_RATE_MIN_HZ       0.01f
#define PARAM_AUDIO_MODFX_RATE_MAX_HZ      12.0f
#define PARAM_AUDIO_MODFX_DEPTH_MIN        0.0f
#define PARAM_AUDIO_MODFX_DEPTH_MAX        0.93f
#define PARAM_AUDIO_MODFX_FEEDBACK_MIN    -1.0f
#define PARAM_AUDIO_MODFX_FEEDBACK_MAX     1.0f
#define PARAM_AUDIO_MODFX_UNIT_MIN         0.0f
#define PARAM_AUDIO_MODFX_UNIT_MAX         1.0f
