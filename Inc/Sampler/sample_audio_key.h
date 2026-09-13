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
    SAMPLE_AUDIO_DOMAIN_MULTI,
    SAMPLE_AUDIO_DOMAIN_REC
} sample_audio_domain_t;

typedef struct
{
    uint8_t domain;
    uint8_t reserved;
    uint16_t object_id;
    uint32_t generation;
} sample_audio_key_t;

static inline sample_audio_key_t sample_audio_key_classic(uint16_t object_id)
{
    const sample_audio_key_t key = { (uint8_t)SAMPLE_AUDIO_DOMAIN_CLASSIC, 0U, object_id, 0U };
    return key;
}

static inline sample_audio_key_t sample_audio_key_looper(uint16_t object_id)
{
    const sample_audio_key_t key = { (uint8_t)SAMPLE_AUDIO_DOMAIN_LOOPER, 0U, object_id, 0U };
    return key;
}

static inline sample_audio_key_t sample_audio_key_multi(uint16_t object_id)
{
    const sample_audio_key_t key = { (uint8_t)SAMPLE_AUDIO_DOMAIN_MULTI, 0U, object_id, 0U };
    return key;
}

static inline sample_audio_key_t sample_audio_key_rec(uint16_t object_id,
                                                       uint32_t generation)
{
    const sample_audio_key_t key = {
        (uint8_t)SAMPLE_AUDIO_DOMAIN_REC, 0U, object_id, generation
    };
    return key;
}

static inline uint8_t sample_audio_key_equal(const sample_audio_key_t *a,
                                             const sample_audio_key_t *b)
{
    return ((a != NULL) && (b != NULL)
            && (a->domain == b->domain)
            && (a->object_id == b->object_id)
            && (a->generation == b->generation)) ? 1U : 0U;
}

static inline uint32_t sample_audio_key_page_hash(sample_audio_key_t key,
                                                  uint32_t page_index,
                                                  uint32_t table_size)
{
    if (table_size == 0U) return 0U;
    return ((((uint32_t)key.object_id * 2654435761UL)
             ^ ((uint32_t)key.domain * 40503UL)
             ^ (key.generation * 3266489917UL)
             ^ (page_index * 2246822519UL)) % table_size);
}

#ifdef __cplusplus
}
#endif
