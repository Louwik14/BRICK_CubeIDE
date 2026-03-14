/**
 * @file fx_saturation.c
 * @brief Module applicatif fx_saturation.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à fx_saturation.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "fx_saturation.h"

#define FX_SAT_MIN_K 1.0f
#define FX_SAT_K_MAX 25.0f

/**
 * @brief Point d'entrée fx_softclip.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_softclip.
 *
 * @param x Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline float fx_softclip(float x)
{
    const float ax = __builtin_fabsf(x);
    return x / (1.0f + ax);
}

// 🔥 TON waveshaper isolé (important pour normalisation cohérente)
/**
 * @brief Point d'entrée fx_shape.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_shape.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param k Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline float fx_shape(float x, float k)
{
    const float ax = __builtin_fabsf(x);
    return x * (1.0f + k * ax) / (1.0f + k * x * x);
}

/**
 * @brief Point d'entrée fx_saturation_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_saturation_init.
 *
 * @param fx Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_saturation_init(fx_saturation_t *fx)
{
    if(fx == 0)
        return;

    fx->k = FX_SAT_MIN_K;
    fx->tone = 0.0f;
    fx->asym = 0.75f;

    fx->pre_gain = 1.0f;
    fx->post_gain = 1.0f;

    fx->mix = 1.0f;
    fx->dry = 0.0f;

    fx->prev_l = 0.0f;
    fx->prev_r = 0.0f;

    fx->bypass = 1U;
}

/**
 * @brief Point d'entrée fx_saturation_set_drive_ui.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_saturation_set_drive_ui.
 *
 * @param fx Paramètre d'entrée de l'API.
 * @param drive_0_127 Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_saturation_set_drive_ui(fx_saturation_t *fx, uint8_t drive_0_127)
{
    if(fx == 0)
        return;

    if(drive_0_127 == 0U)
    {
        fx->bypass = 1U;
        fx->pre_gain = 1.0f;
        fx->post_gain = 1.0f;
        return;
    }

    const float d = (float)drive_0_127 * (1.0f / 128.0f);

    // 🔥 mapping Daisy (musical + progressif)
    const float d2 = d * d;
    const float drive = 2.0f * d;

    const float pre_a = drive * 0.5f;
    const float pre_b = drive * drive * drive * drive * drive * 24.0f;

    fx->pre_gain = pre_a + (pre_b - pre_a) * d2;

    // 🔥 k pour ton waveshaper
    fx->k = FX_SAT_MIN_K + (FX_SAT_K_MAX * d2);

    // 🔥 normalisation TYPE DAISY mais avec TON shaping
    const float drive_squashed = drive * (2.0f - drive);

    // point de référence (comme Daisy)
    const float ref = 0.33f + drive_squashed * (fx->pre_gain - 0.33f);

    // passage dans TON waveshaper
    float shaped_ref = fx_shape(ref, fx->k);

    // sécurité (évite division explosive)
    const float eps = 1e-6f;
    if(__builtin_fabsf(shaped_ref) < eps)
        shaped_ref = eps;

    fx->post_gain = 1.0f / shaped_ref;

    fx->bypass = 0U;
}

/**
 * @brief Point d'entrée fx_saturation_set_mix_ui.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_saturation_set_mix_ui.
 *
 * @param fx Paramètre d'entrée de l'API.
 * @param mix_0_127 Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_saturation_set_mix_ui(fx_saturation_t *fx, uint8_t mix_0_127)
{
    if(fx == 0)
        return;

    fx->mix = (float)mix_0_127 * (1.0f / 127.0f);
    fx->dry = 1.0f - fx->mix;
}

/**
 * @brief Point d'entrée fx_saturation_set_tone_ui.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_saturation_set_tone_ui.
 *
 * @param fx Paramètre d'entrée de l'API.
 * @param tone_0_127 Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_saturation_set_tone_ui(fx_saturation_t *fx, uint8_t tone_0_127)
{
    if(fx == 0)
        return;

    fx->tone = (float)tone_0_127 * (1.0f / 127.0f);
}

/**
 * @brief Point d'entrée fx_saturation_set_bias_ui.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_saturation_set_bias_ui.
 *
 * @param fx Paramètre d'entrée de l'API.
 * @param bias_0_127 Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_saturation_set_bias_ui(fx_saturation_t *fx, uint8_t bias_0_127)
{
    if(fx == 0)
        return;

    fx->asym = 0.5f + ((float)bias_0_127 * (1.0f / 127.0f)) * 0.5f;
}

/**
 * @brief Point d'entrée fx_saturation_process_block.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_saturation_process_block.
 *
 * @param fx Paramètre d'entrée de l'API.
 * @param inout_l Paramètre d'entrée de l'API.
 * @param inout_r Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_saturation_process_block(fx_saturation_t *fx,
                                 float *inout_l,
                                 float *inout_r,
                                 uint32_t frames)
{
    if((fx == 0) || (inout_l == 0) || (inout_r == 0) || (frames == 0U) || (fx->bypass != 0U))
        return;

    const float k = fx->k;
    const float tone = fx->tone;
    const float asym = fx->asym;
    const float pre = fx->pre_gain;
    const float post = fx->post_gain;
    const float wet = fx->mix;
    const float dry = fx->dry;

    float prev_l = fx->prev_l;
    float prev_r = fx->prev_r;

    float *l = inout_l;
    float *r = inout_r;

    for(uint32_t n = 0U; n < frames; n++)
    {
        const float in_l = l[n];
        const float in_r = r[n];

        float xl = in_l;
        float xr = in_r;

        // 🔥 tone (pré-emphasis)
        const float dxl = xl - prev_l;
        const float dxr = xr - prev_r;
        prev_l = xl;
        prev_r = xr;

        xl += tone * dxl;
        xr += tone * dxr;

        // 🔥 drive
        xl *= pre;
        xr *= pre;

        // 🔥 asymétrie
        if(xl < 0.0f) xl *= asym;
        if(xr < 0.0f) xr *= asym;

        // 🔥 waveshaper
        const float xl2 = xl * xl;
        const float xr2 = xr * xr;

        const float axl = __builtin_fabsf(xl);
        const float axr = __builtin_fabsf(xr);

        float yl = xl * (1.0f + k * axl) / (1.0f + k * xl2);
        float yr = xr * (1.0f + k * axr) / (1.0f + k * xr2);

        // 🔥 post gain (corrigé)
        yl *= post;
        yr *= post;

        // mix
        yl = in_l * dry + yl * wet;
        yr = in_r * dry + yr * wet;

        // soft clamp léger (optionnel mais mieux que hard clamp)
        yl = fx_softclip(yl);
        yr = fx_softclip(yr);

        l[n] = yl;
        r[n] = yr;
    }

    fx->prev_l = prev_l;
    fx->prev_r = prev_r;
}
