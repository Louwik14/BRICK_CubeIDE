#include "Storage/persistent_pattern_restore_prepare.h"

#include "Storage/persistent_pattern_control.h"

persist_codec_result_t persistent_pattern_restore_execute(
    const persist_control_pattern_t *pattern, uint8_t resume_transport)
{
    return persistent_pattern_control_install_restored(
        pattern, resume_transport);
}
