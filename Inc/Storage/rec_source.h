#pragma once

#include <stdint.h>

#include "IPC/rec_source_contract.h"

#define REC_SOURCE_SLOT_COUNT 2U
#define REC_SOURCE_PATH_MAX 96U

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
                                    uint32_t registration_epoch);
void rec_source_abort_building(void);
void rec_source_service(void);
uint8_t rec_source_current_snapshot(rec_source_snapshot_t *out_snapshot);
uint8_t rec_source_current_path(const char **out_path,
                                rec_source_snapshot_t *out_snapshot);
