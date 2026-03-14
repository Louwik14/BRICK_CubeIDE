#ifndef UI_PARAM_H
#define UI_PARAM_H

#include <stdint.h>

#include "param_store.h"

typedef struct
{
    param_id_t params[4];
} ui_param_bank_t;

void ui_param_set_bank(const ui_param_bank_t *bank);
void ui_param_handle_encoder(uint8_t encoder, int16_t delta);

#endif /* UI_PARAM_H */
