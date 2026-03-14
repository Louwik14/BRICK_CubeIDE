#include "ui_param.h"

#include "param_registry.h"

typedef struct
{
    ui_param_bank_t bank;
    uint8_t valid;
} ui_param_state_t;

static ui_param_state_t g_ui_param = {
    .bank = { .params = { PARAM_GRAN_DENSITY, PARAM_GRAN_PITCH, PARAM_GRAN_MIX, PARAM_GRAN_FREEZE } },
    .valid = 0U,
};

static float ui_param_clamp(float v, float min, float max)
{
    if (v < min)
    {
        return min;
    }

    if (v > max)
    {
        return max;
    }

    return v;
}

void ui_param_set_bank(const ui_param_bank_t *bank)
{
    if (bank == 0)
    {
        g_ui_param.valid = 0U;
        return;
    }

    g_ui_param.bank = *bank;
    g_ui_param.valid = 1U;
}

void ui_param_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((g_ui_param.valid == 0U) || (delta == 0) || (encoder >= 4U))
    {
        return;
    }

    const param_id_t param = g_ui_param.bank.params[encoder];
    if (param >= PARAM_COUNT)
    {
        return;
    }

    const param_desc_t *desc = &param_registry[param];

    float value = param_get(param);
    value += (float)delta * desc->step;
    value = ui_param_clamp(value, desc->min, desc->max);

    param_set(param, value);
}
