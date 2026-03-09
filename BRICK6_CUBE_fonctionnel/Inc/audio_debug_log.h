#ifndef AUDIO_DEBUG_LOG_H
#define AUDIO_DEBUG_LOG_H

/*
 * Compile-time audio debug logging switch.
 * Set AUDIO_DEBUG_LOG_ENABLED to 1 to re-enable logs.
 */
#ifndef AUDIO_DEBUG_LOG_ENABLED
#define AUDIO_DEBUG_LOG_ENABLED 1
#endif

#if AUDIO_DEBUG_LOG_ENABLED
#include <stdio.h>
#define AUDIO_DEBUG_LOG(...) printf(__VA_ARGS__)
#else
#define AUDIO_DEBUG_LOG(...) do {} while(0)
#endif

#endif /* AUDIO_DEBUG_LOG_H */
