#pragma once

/* Immutable model identifiers shared by CONTROL catalogs and AUDIO engines. */
#define BRICK6_PRISM_MODEL_COUNT 33U
#define BRICK6_PRISM_LAST_MODEL (BRICK6_PRISM_MODEL_COUNT - 1U)

#define BRICK6_STACK_SLOT_COUNT 3U

typedef enum
{
    BRICK6_STACK_MODEL_SINE = 0,
    BRICK6_STACK_MODEL_TRI,
    BRICK6_STACK_MODEL_SQUARE,
    BRICK6_STACK_MODEL_SAW,
    BRICK6_STACK_MODEL_SHAPE,
    BRICK6_STACK_MODEL_TRIPLE_SAW,
    BRICK6_STACK_MODEL_COUNT
} brick6_stack_model_t;

typedef enum
{
    BRICK6_STACK_FAMILY_PHASE = 0,
    BRICK6_STACK_FAMILY_ENSEMBLE,
    BRICK6_STACK_FAMILY_DELUGE
} brick6_stack_family_t;

typedef enum
{
    BRICK6_STACK_KERNEL_PHASE_BASIC = 0,
    BRICK6_STACK_KERNEL_TRIPLE_ANALOG,
    BRICK6_STACK_KERNEL_DELUGE,
    BRICK6_STACK_KERNEL_COUNT
} brick6_stack_kernel_id_t;

typedef enum {
    FX_MODFX_OFF = 0,
    FX_MODFX_RETIRED_VIBE,
    FX_MODFX_RETIRED_DRIFT,
    FX_MODFX_DAISY_STEREO,
    FX_MODFX_JUNOLOGUE,
    FX_MODFX_MODEL_COUNT
} fx_modfx_model_t;

enum
{
    AUDIO_FX_MODEL_OFF = 0U,
    AUDIO_FX_MODEL_LOFI = 1U,
    AUDIO_FX_MODEL_FOLD = 2U,
    AUDIO_FX_MODEL_DRIVE = 3U,
    AUDIO_FX_MODEL_POINT = 5U,
    AUDIO_FX_MODEL_SUB = 8U,
    AUDIO_FX_MODEL_RING = 10U,
    AUDIO_FX_MODEL_SUB_LIGHT = 11U,
    AUDIO_FX_MODEL_VIBE = 12U,
    AUDIO_FX_MODEL_DRIFT = 13U
};

typedef enum
{
    AUDIO_FX_SLOT_A = 0U,
    AUDIO_FX_SLOT_B,
    AUDIO_FX_SLOT_COUNT
} audio_fx_slot_t;

typedef enum
{
    AUDIO_FX_FILTER_POS_PRE = 0U,
    AUDIO_FX_FILTER_POS_MID,
    AUDIO_FX_FILTER_POS_POST,
    AUDIO_FX_FILTER_POS_COUNT
} audio_fx_filter_pos_t;

typedef enum
{
    AUDIO_FX_ORDER_A_B = 0U,
    AUDIO_FX_ORDER_B_A,
    AUDIO_FX_ORDER_COUNT
} audio_fx_order_t;

typedef enum
{
    AUDIO_FX_SPATIAL_MONO = 0U,
    AUDIO_FX_SPATIAL_STEREO,
    AUDIO_FX_SPATIAL_MID,
    AUDIO_FX_SPATIAL_SIDE,
    AUDIO_FX_SPATIAL_COUNT
} audio_fx_spatial_mode_t;

typedef enum
{
    AUDIO_FX_PLACEMENT_PRE_FILTER = 0U,
    AUDIO_FX_PLACEMENT_POST_FILTER = 1U
} audio_fx_placement_t;

typedef enum
{
    MD_MODEL_TRX_BD = 0,
    MD_MODEL_TRX_SD,
    MD_MODEL_TRX_CH,
    MD_MODEL_EFM_BD,
    MD_MODEL_EFM_SD,
    MD_MODEL_EFM_CB,
    MD_MODEL_COUNT
} md_model_t;

typedef enum
{
    BRICK6_FM_OPERATOR_LEVEL = 0,
    BRICK6_FM_OPERATOR_FREQ,
    BRICK6_FM_OPERATOR_DETUNE,
    BRICK6_FM_OPERATOR_ENV_ATTACK,
    BRICK6_FM_OPERATOR_ENV_DECAY,
    BRICK6_FM_OPERATOR_ENV_SUSTAIN,
    BRICK6_FM_OPERATOR_ENV_RELEASE,
    BRICK6_FM_OPERATOR_ON,
    BRICK6_FM_OPERATOR_MODE,
    BRICK6_FM_OPERATOR_VEL,
    BRICK6_FM_OPERATOR_KEY,
    BRICK6_FM_OPERATOR_PARAM_COUNT
} brick6_fm_operator_param_t;

static inline uint8_t audio_fx_lofi_model_index_from_control(uint8_t value)
{
    const uint16_t index = ((uint16_t)value * 3U) >> 7U;
    return (index < 3U) ? (uint8_t)index : 2U;
}

static inline uint8_t audio_fx_ring_wave_index_from_control(uint8_t value)
{
    const uint16_t index = ((uint16_t)value * 4U) >> 7U;
    return (index < 4U) ? (uint8_t)index : 3U;
}

static inline uint8_t audio_fx_ring_model_index_from_control(uint8_t value)
{
    const uint16_t index = ((uint16_t)value * 6U) >> 7U;
    return (index < 6U) ? (uint8_t)index : 5U;
}
