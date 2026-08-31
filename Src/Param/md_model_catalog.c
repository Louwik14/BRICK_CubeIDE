#include "Param/md_model_catalog.h"

static const md_model_profile_t g_md_profiles[MD_MODEL_COUNT] = {
    { "TRX-BD", { "PTCH", "DEC", "RAMP", "RDEC", "STRT", "NOIS", "HARM", "CLIP" },
      { 64U, 64U, 32U, 48U, 64U, 0U, 0U, 0U }, 8U },
    { "TRX-SD", { "PTCH", "DEC", "BUMP", "BENV", "SNAP", "TONE", "TUNE", "CLIP" },
      { 64U, 64U, 32U, 48U, 48U, 64U, 64U, 0U }, 8U },
    { "TRX-CH", { "GAP", "DEC", "HPF", "LPF", "MTAL", 0, 0, 0 },
      { 64U, 48U, 32U, 96U, 64U, 0U, 0U, 0U }, 5U },
    { "EFM-BD", { "PTCH", "DEC", "RAMP", "RDEC", "MOD", "MFRQ", "MDEC", "MFB" },
      { 64U, 64U, 32U, 48U, 48U, 64U, 48U, 0U }, 8U },
    { "EFM-SD", { "PTCH", "DEC", "NOISE", "NDEC", "MOD", "MFRQ", "MDEC", "HPF" },
      { 64U, 64U, 48U, 48U, 48U, 64U, 48U, 32U }, 8U },
    { "EFM-CB", { "PTCH", "DEC", "SNAP", "FB", "MOD", "MFRQ", "MDEC", 0 },
      { 64U, 64U, 64U, 0U, 48U, 64U, 48U, 0U }, 7U }
};

const md_model_profile_t *md_model_profile_get(uint8_t model)
{
    return (model < (uint8_t)MD_MODEL_COUNT) ? &g_md_profiles[model] : &g_md_profiles[0];
}

uint8_t md_model_validate(float value)
{
    const uint8_t model = (uint8_t)((value < 0.0f) ? 0.0f : value + 0.5f);
    return (model < (uint8_t)MD_MODEL_COUNT) ? model : (uint8_t)MD_MODEL_TRX_BD;
}
