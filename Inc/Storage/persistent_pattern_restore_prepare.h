#ifndef PERSISTENT_PATTERN_RESTORE_PREPARE_H
#define PERSISTENT_PATTERN_RESTORE_PREPARE_H

#include "Storage/persistent_control_codec.h"
#include "Storage/restore_plan_contract.h"

persist_codec_result_t persistent_pattern_restore_prepare(
    const persist_control_pattern_t *pattern, restore_audio_plan_t *out_plan);
uint32_t persistent_pattern_restore_plan_crc32(const restore_audio_plan_t *plan);
persist_codec_result_t persistent_pattern_restore_execute(
    const persist_control_pattern_t *pattern,uint8_t resume_transport);

#endif
