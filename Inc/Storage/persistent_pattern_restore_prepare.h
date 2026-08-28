#ifndef PERSISTENT_PATTERN_RESTORE_PREPARE_H
#define PERSISTENT_PATTERN_RESTORE_PREPARE_H

#include "Storage/persistent_control_codec.h"
persist_codec_result_t persistent_pattern_restore_execute(
    const persist_control_pattern_t *pattern,uint8_t resume_transport);

#endif
