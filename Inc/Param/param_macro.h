#ifndef PARAM_MACRO_H
#define PARAM_MACRO_H

#include <stdint.h>

#include "Param/param_ids.h"

void param_macro_init(void);
void param_macro_reset(void);
void param_macro_service(void);
uint8_t param_macro_lock_target_is_supported(uint8_t track, param_id_t param);
uint8_t param_macro_sync_sources(void);
void param_macro_note_base_change(uint8_t track, param_id_t param);
uint8_t param_macro_set_amount(uint8_t macro, float amount);
float param_macro_get_amount(uint8_t macro);

#endif /* PARAM_MACRO_H */
