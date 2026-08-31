#pragma once

#include "Param/param_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

void param_store_init(void);
void param_store_set_active(param_id_t id, float v);
float param_store_get_active(param_id_t id);

#ifdef __cplusplus
}
#endif
