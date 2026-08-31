#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

void sample_page_cache_audio_init(void);
sample_page_state_t sample_page_cache_audio_get_page_state_key(
    sample_audio_key_t key, uint32_t page_index);
uint8_t sample_page_cache_audio_resolve_page_key(
    sample_audio_key_t key, uint32_t page_index, sample_page_span_t *out_span);
uint8_t sample_page_cache_audio_resolve_page(
    uint16_t sample_id, uint32_t page_index, sample_page_span_t *out_span);
uint8_t sample_page_cache_audio_resolve_page_ref_key(
    sample_audio_key_t key, const sample_page_ref_t *ref,
    sample_page_span_t *out_span);
uint8_t sample_page_cache_audio_resolve_page_ref(
    uint16_t sample_id, const sample_page_ref_t *ref,
    sample_page_span_t *out_span);
#ifdef __cplusplus
}
#endif
