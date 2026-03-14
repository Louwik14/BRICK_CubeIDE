#include "ui_param.h"

#include "param_registry.h"

static ui_param_bank_t current_bank = {
    .params = {PARAM_GRAN_DENSITY, PARAM_GRAN_PITCH, PARAM_GRAN_MIX, PARAM_GRAN_FREEZE}
};

void ui_param_set_bank(const ui_param_bank_t *bank)
{
    if (bank == 0)
    {
        return;
    }

    current_bank = *bank;
}

void ui_param_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((encoder >= 4U) || (delta == 0))
    {
        return;
    }

    const param_id_t param = current_bank.params[encoder];
    if (param >= PARAM_COUNT)
    {
        return;
    }

    const float step = param_registry[param].step;
    const float value = param_get(param) + ((float)delta * step);

    param_set(param, value);
}
