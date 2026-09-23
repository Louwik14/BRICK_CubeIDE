#ifndef PERSISTENT_PATTERN_CONTROL_H
#define PERSISTENT_PATTERN_CONTROL_H
#include <stdint.h>
#include "Storage/persistent_control_codec.h"
#include "Mod/mod_destination_contract.h"
#include "NoteFx/note_fx_state.h"
#include "Seq/seq_model.h"

typedef struct
{
    param_global_control_state_t global_audio;
    uint32_t groove_seed;
} persistent_pattern_default_context_t;

persist_codec_result_t persistent_pattern_control_build_defaults(
    persist_control_pattern_t *destination,
    const persistent_pattern_default_context_t *context);
persist_codec_result_t persistent_pattern_control_capture(persist_control_pattern_t *out_pattern);
persist_codec_result_t persistent_pattern_control_validate(const persist_control_pattern_t *pattern);
persist_codec_result_t persistent_pattern_control_apply(const persist_control_pattern_t *pattern,uint8_t resume_transport);
persist_codec_result_t persistent_pattern_control_apply_with_seq_workspace(
    const persist_control_pattern_t *pattern, uint8_t resume_transport,
    seq_groove_compiled_t workspace[SEQ_TIMING_TRACK_COUNT]);
/* Project restore already owns the outer AUDIO_STATE transaction. */
persist_codec_result_t persistent_pattern_control_install_into_active_snapshot(
    const persist_control_pattern_t *pattern,
    uint8_t resume_transport);
void persistent_pattern_control_sync_ui_after_commit(void);
#endif
