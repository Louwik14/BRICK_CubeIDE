#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SAMPLE_AUDIO_DOMAIN_CLASSIC = 0,
    SAMPLE_AUDIO_DOMAIN_LOOPER,
    SAMPLE_AUDIO_DOMAIN_MULTI
} sample_audio_domain_t;

typedef struct
{
    sample_audio_domain_t domain;
    uint16_t object_id;
} sample_audio_key_t;

#ifdef __cplusplus
}
#endif
