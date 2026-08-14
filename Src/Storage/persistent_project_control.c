#include "Storage/persistent_project_control.h"
#include "Storage/persistent_pattern_control.h"
#include "Storage/pattern_live_ram.h"
#include <stddef.h>

void persistent_project_control_capture_metadata(persist_codec_project_metadata_t*out){if(out==NULL)return;out->active_pattern_bank=0U;out->active_pattern=0U;(void)pattern_live_get_active(&out->active_pattern_bank,&out->active_pattern);}
persist_codec_result_t persistent_project_control_apply_working(const persist_codec_project_metadata_t*metadata,const persist_control_pattern_t*pattern,uint8_t resume){if(metadata==NULL||pattern==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;persist_codec_result_t r=persistent_pattern_control_apply(pattern,resume);if(r==PERSIST_CODEC_OK)pattern_live_set_active_state(metadata->active_pattern_bank,metadata->active_pattern,0U,0U,0U);return r;}
