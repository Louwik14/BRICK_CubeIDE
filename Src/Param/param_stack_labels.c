#include "Param/param_stack_labels.h"

#include <stddef.h>

#include "Core/brick6_stack_runtime.h"
#include "Param/param_registry.h"

typedef struct
{
    uint8_t count;
    const char *label[2];
} param_stack_model_params_t;

static const param_stack_model_params_t g_stack_model_params[BRICK6_STACK_MODEL_COUNT] = {
    [BRICK6_STACK_MODEL_SINE]       = {0U, {NULL, NULL}},
    [BRICK6_STACK_MODEL_TRI]        = {0U, {NULL, NULL}},
    [BRICK6_STACK_MODEL_SQUARE]     = {1U, {"PWM", NULL}},
    [BRICK6_STACK_MODEL_SAW]        = {0U, {NULL, NULL}},
    [BRICK6_STACK_MODEL_SHAPE]      = {2U, {"SHAPE", "MORPH"}},
    [BRICK6_STACK_MODEL_TRIPLE_SAW] = {2U, {"OSC2", "OSC3"}}
};

_Static_assert((sizeof(g_stack_model_params) / sizeof(g_stack_model_params[0]))
                   == BRICK6_STACK_MODEL_COUNT,
               "Stack parameter labels and model count must stay aligned");

uint8_t param_stack_model_param_resolve(uint8_t model,
                                        uint8_t param_index,
                                        const char **out_label)
{
    if ((out_label == NULL)
            || (model >= (uint8_t)BRICK6_STACK_MODEL_COUNT)
            || (param_index >= g_stack_model_params[model].count)
            || (g_stack_model_params[model].label[param_index] == NULL))
    {
        return 0U;
    }
    *out_label = g_stack_model_params[model].label[param_index];
    return 1U;
}

uint8_t param_stack_dynamic_param_info(param_id_t id,
                                       uint8_t *out_slot,
                                       uint8_t *out_param_index)
{
    if ((id < PARAM_STACK_OSC1_TIMBRE) || (id > PARAM_STACK_OSC3_COLOR))
    {
        return 0U;
    }
    const uint8_t rel = (uint8_t)(id - PARAM_STACK_OSC1_MODEL);
    const uint8_t slot_param = (uint8_t)(rel % 4U);
    if ((slot_param < 2U) || (slot_param > 3U))
    {
        return 0U;
    }
    if (out_slot != NULL)
    {
        *out_slot = (uint8_t)(rel / 4U);
    }
    if (out_param_index != NULL)
    {
        *out_param_index = (uint8_t)(slot_param - 2U);
    }
    return 1U;
}

uint8_t param_stack_label_for_track_param(uint8_t track,
                                          param_id_t id,
                                          const char **out_label)
{
    uint8_t slot = 0U;
    uint8_t param_index = 0U;
    if ((param_stack_dynamic_param_info(
                id, &slot, &param_index) == 0U)
            || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return 0U;
    }
    static const param_id_t model_ids[BRICK6_STACK_SLOT_COUNT] = {
        PARAM_STACK_OSC1_MODEL, PARAM_STACK_OSC2_MODEL,
        PARAM_STACK_OSC3_MODEL };
    float model_value = 0.0f;
    if (!param_registry_get_track_value(model_ids[slot], track, &model_value))
        return 0U;
    const uint8_t model = (uint8_t)(model_value + 0.5f);
    return param_stack_model_param_resolve(model, param_index, out_label);
}
