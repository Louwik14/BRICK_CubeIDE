#include "Param/audio_fx_param_catalog.h"

#include <stddef.h>

#include "Param/engine_model_catalog.h"

typedef struct
{
    uint8_t count;
    const char *name[AUDIO_FX_PARAM_CATALOG_MAX_PARAMS];
} audio_fx_model_param_catalog_t;

static const audio_fx_model_param_catalog_t g_audio_fx_model_params[] = {
    [AUDIO_FX_MODEL_OFF]       = {0U, {NULL, NULL, NULL}},
    [AUDIO_FX_MODEL_LOFI]      = {3U, {"BIT", "SRR", "ENG"}},
    [AUDIO_FX_MODEL_FOLD]      = {3U, {"FOLD", "BIAS", "XMOD"}},
    [AUDIO_FX_MODEL_DRIVE]     = {3U, {"DRIVE", "INPUT", "LEVEL"}},
    [AUDIO_FX_MODEL_POINT]     = {3U, {"AMOUNT", "POINT", "SPEED"}},
    [AUDIO_FX_MODEL_SUB]       = {3U, {"SUB", "TONE", "MIX"}},
    [AUDIO_FX_MODEL_RING]      = {3U, {"FREQ", "WAVE", "MODEL"}},
    [AUDIO_FX_MODEL_SUB_LIGHT] = {3U, {"SUB", "TONE", "MIX"}},
    [AUDIO_FX_MODEL_VIBE]      = {3U, {"RATE", "DEPTH", "DELAY"}},
    [AUDIO_FX_MODEL_DRIFT]     = {2U, {"DELAY", "FEEDBACK", NULL}}
};

uint8_t audio_fx_param_catalog_param_info(param_id_t id,
                                          uint8_t *out_slot,
                                          uint8_t *out_param_index)
{
    uint8_t slot = 0U;
    uint8_t index = 0U;
    if ((id >= PARAM_AUDIO_FX_P1) && (id <= PARAM_AUDIO_FX_P3))
    {
        index = (uint8_t)(id - PARAM_AUDIO_FX_P1);
    }
    else if ((id >= PARAM_AUDIO_FX_B_P1) && (id <= PARAM_AUDIO_FX_B_P3))
    {
        slot = 1U;
        index = (uint8_t)(id - PARAM_AUDIO_FX_B_P1);
    }
    else
    {
        return 0U;
    }

    if (out_slot != NULL)
    {
        *out_slot = slot;
    }
    if (out_param_index != NULL)
    {
        *out_param_index = index;
    }
    return 1U;
}

uint8_t audio_fx_param_catalog_resolve(uint8_t model,
                                       uint8_t param_index,
                                       const char **out_name)
{
    if ((out_name == NULL)
            || (model >= (uint8_t)(sizeof(g_audio_fx_model_params)
                                   / sizeof(g_audio_fx_model_params[0])))
            || (param_index >= g_audio_fx_model_params[model].count)
            || (g_audio_fx_model_params[model].name[param_index] == NULL))
    {
        return 0U;
    }
    *out_name = g_audio_fx_model_params[model].name[param_index];
    return 1U;
}

uint8_t audio_fx_param_catalog_count(uint8_t model)
{
    return (model < (uint8_t)(sizeof(g_audio_fx_model_params)
                              / sizeof(g_audio_fx_model_params[0])))
        ? g_audio_fx_model_params[model].count : 0U;
}
