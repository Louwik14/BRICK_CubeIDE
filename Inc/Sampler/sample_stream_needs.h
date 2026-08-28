#pragma once

#include <stdint.h>

#include "Sampler/sample_stream_arch_contract.h"
#include "Sampler/sample_stream_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the persistent, pointer-free need list without touching the cache. */
uint8_t sample_stream_needs_build(
    const sample_stream_snapshot_t *snapshot,
    sample_stream_target_voice_registry_entry_t *out_entry);

/* Update/read/drop the fixed registry that owns all runtime voice needs. */
void sample_stream_needs_registry_reset(void);
uint8_t sample_stream_needs_registry_update(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    const sample_stream_snapshot_t *snapshot);
uint8_t sample_stream_needs_registry_read(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    sample_stream_target_voice_registry_entry_t *out_entry);
void sample_stream_needs_registry_drop(sample_stream_snapshot_source_t source,
                                       uint8_t voice_id);
uint8_t sample_stream_needs_registry_drop_owner(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    uint32_t owner_token);
uint8_t sample_stream_needs_registry_contains(sample_stream_snapshot_source_t source,
                                               uint8_t voice_id,
                                               sample_audio_key_t key,
                                               uint32_t page_index,
                                               uint32_t registration_epoch,
                                               uint32_t owner_token);
uint8_t sample_stream_needs_registry_contains_any(sample_audio_key_t key,
                                                  uint32_t page_index,
                                                  uint32_t registration_epoch);
uint8_t sample_stream_needs_registry_contains_key(sample_audio_key_t key);
uint8_t sample_stream_needs_entry_page_at(
    const sample_stream_target_voice_registry_entry_t *entry,
    uint8_t index,
    uint32_t *out_page_index);
uint8_t sample_stream_needs_registry_has_active(void);
uint32_t sample_stream_needs_registry_count_active(void);

#ifdef __cplusplus
}
#endif
