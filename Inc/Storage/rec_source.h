#pragma once

#include <stdint.h>

#include "IPC/rec_source_contract.h"

#include "Storage/audio_recorder_storage.h"
#include "Audio/audio_recorder_capture_audio.h"

/* RETIRED + UNDO + CURRENT + BUILDING is the bounded worst case. */
#define REC_SOURCE_SLOT_COUNT 4U
#define REC_SOURCE_PATH_MAX 96U

typedef enum
{
    REC_SOURCE_STATE_FREE_PREPARED = 0,
    REC_SOURCE_STATE_BUILDING,
    REC_SOURCE_STATE_CURRENT,
    REC_SOURCE_STATE_UNDO,
    REC_SOURCE_STATE_RETIRED
} rec_source_state_t;

typedef enum
{
    REC_SOURCE_OWNERSHIP_TEMPORARY = 0,
    REC_SOURCE_OWNERSHIP_PERSISTENT
} rec_source_ownership_t;

void rec_source_init(void);
/* Caller owns the Recorder SD gate and has mounted 0:. */
uint8_t rec_source_begin_build(const char **temporary_path,
                               const char **final_path,
                               sample_audio_key_t *key);
sample_audio_key_t rec_source_building_key(void);
const char *rec_source_building_temporary_path(void);
const char *rec_source_building_final_path(void);
uint8_t rec_source_building_active(void);
uint8_t rec_source_publish_building(uint32_t frame_count,
                                    uint32_t registration_epoch,
                                    const audio_recorder_storage_map_copy_t *map);
void rec_source_abort_building(void);
void rec_source_service(void);
uint8_t rec_source_switch_current(uint32_t generation);
uint8_t rec_source_clear_current(void);
void rec_source_release_history_pair(uint32_t before_generation,
                                     uint32_t after_generation);
uint8_t rec_source_promote_current(const char *persistent_path);
uint8_t rec_source_current_snapshot(rec_source_snapshot_t *out_snapshot);
uint8_t rec_source_current_path(const char **out_path,
                                rec_source_snapshot_t *out_snapshot);
const audio_recorder_waveform_summary_t *rec_source_current_waveform(void);
