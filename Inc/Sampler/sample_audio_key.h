#pragma once

#include <stdint.h>
#include <stddef.h>

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

static inline sample_audio_key_t sample_audio_key_classic(uint16_t object_id)
{
    const sample_audio_key_t key = { SAMPLE_AUDIO_DOMAIN_CLASSIC, object_id };
    return key;
}

static inline sample_audio_key_t sample_audio_key_looper(uint16_t object_id)
{
    const sample_audio_key_t key = { SAMPLE_AUDIO_DOMAIN_LOOPER, object_id };
    return key;
}

static inline sample_audio_key_t sample_audio_key_multi(uint16_t object_id)
{
    const sample_audio_key_t key = { SAMPLE_AUDIO_DOMAIN_MULTI, object_id };
    return key;
}

static inline uint8_t sample_audio_key_equal(const sample_audio_key_t *a,
                                             const sample_audio_key_t *b)
{
    return ((a != NULL) && (b != NULL)
            && (a->domain == b->domain)
            && (a->object_id == b->object_id)) ? 1U : 0U;
}

static inline uint32_t sample_audio_key_page_hash(sample_audio_key_t key,
                                                  uint32_t page_index,
                                                  uint32_t table_size)
{
    if (table_size == 0U) return 0U;
    return ((((uint32_t)key.object_id * 2654435761UL)
             ^ ((uint32_t)key.domain * 40503UL)
             ^ (page_index * 2246822519UL)) % table_size);
}

#ifdef __cplusplus
}
#endif
