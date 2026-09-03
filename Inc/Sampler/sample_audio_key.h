#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SAMPLE_AUDIO_DOMAIN_CLASSIC = 0,
    SAMPLE_AUDIO_DOMAIN_LOOPER = 1,
    SAMPLE_AUDIO_DOMAIN_MULTI = 2
} sample_audio_domain_t;

typedef struct
{
    /* Fixed-width ABI code; the semantic enum remains for local call sites. */
    uint8_t domain;
    uint8_t reserved;
    uint16_t object_id;
} sample_audio_key_t;

_Static_assert(sizeof(sample_audio_key_t) == 4U,
               "Sample audio key ABI changed");
_Static_assert(offsetof(sample_audio_key_t, domain) == 0U,
               "Sample audio key domain offset changed");
_Static_assert(offsetof(sample_audio_key_t, object_id) == 2U,
               "Sample audio key object offset changed");

static inline sample_audio_key_t sample_audio_key_classic(uint16_t object_id)
{
    const sample_audio_key_t key = {
        .domain = SAMPLE_AUDIO_DOMAIN_CLASSIC, .object_id = object_id
    };
    return key;
}

static inline sample_audio_key_t sample_audio_key_looper(uint16_t object_id)
{
    const sample_audio_key_t key = {
        .domain = SAMPLE_AUDIO_DOMAIN_LOOPER, .object_id = object_id
    };
    return key;
}

static inline sample_audio_key_t sample_audio_key_multi(uint16_t object_id)
{
    const sample_audio_key_t key = {
        .domain = SAMPLE_AUDIO_DOMAIN_MULTI, .object_id = object_id
    };
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
