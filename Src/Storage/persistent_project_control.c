#include "Storage/persistent_project_control.h"
#include "Storage/pattern_live_ram.h"
#include <stddef.h>

void persistent_project_control_capture_metadata(persist_codec_project_metadata_t*out){if(out==NULL)return;out->active_pattern_bank=0U;out->active_pattern=0U;(void)pattern_live_get_active(&out->active_pattern_bank,&out->active_pattern);}
