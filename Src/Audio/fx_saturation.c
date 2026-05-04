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
#define FX_DECIMATOR_BITS_MAX 15U
#define FX_RATE2_FREQ_MIN 0.0f
#define FX_RATE2_FREQ_MAX 1.0f

static inline float fx_clampf(float v, float lo, float hi)
{
    if(v < lo)
    {
        return lo;
    }
    if(v > hi)
    {
        return hi;
    }
    return v;
}

static inline float fx_rate2_this_blep(float t)
{
    return (2.0f * t) - (t * t) - 1.0f;
}

static inline float fx_rate2_next_blep(float t)
{
    return t * t;
}

static inline float fx_saturation_rate2_process_sample(float in,
                                                       float frequency,
                                                       float *phase,
                                                       float *sample,
                                                       float *next_sample,
                                                       float *previous_sample)
{
    float this_sample = *next_sample;
    *next_sample = 0.0f;
    *phase += frequency;
    if(*phase >= 1.0f)
    {
        *phase -= 1.0f;
        const float t = (frequency > 0.0f) ? (*phase / frequency) : 0.0f;
        const float new_sample = *previous_sample + (in - *previous_sample) * (1.0f - t);
        const float discontinuity = new_sample - *sample;
        this_sample += discontinuity * fx_rate2_this_blep(t);
        *next_sample = discontinuity * fx_rate2_next_blep(t);
        *sample = new_sample;
    }
    *next_sample += *sample;
    *previous_sample = in;

    return this_sample;
}

static void fx_saturation_update_bypass(fx_saturation_t *fx)
{
    if (fx == 0)
    {
        return;
    }

    fx->bypass = ((fx->pre_gain <= 1.0f) && (fx->decimator_enabled == 0U) && (fx->decimator_rate2_enabled == 0U)) ? 1U : 0U;
}

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

    fx->decimator_rate = 1.0f;
    fx->decimator_bits_to_crush = 0U;
    fx->decimator_inc_l = 0U;
    fx->decimator_inc_r = 0U;
    fx->decimator_threshold = 0U;
    fx->decimator_downsampled_l = 0.0f;
    fx->decimator_downsampled_r = 0.0f;
    fx->decimator_enabled = 0U;
    fx->decimator_rate2_enabled = 0U;
    fx->decimator_rate2_frequency = 0.0f;
    fx->decimator_rate2_phase_l = 0.0f;
    fx->decimator_rate2_sample_l = 0.0f;
    fx->decimator_rate2_next_sample_l = 0.0f;
    fx->decimator_rate2_previous_sample_l = 0.0f;
    fx->decimator_rate2_phase_r = 0.0f;
    fx->decimator_rate2_sample_r = 0.0f;
    fx->decimator_rate2_next_sample_r = 0.0f;
    fx->decimator_rate2_previous_sample_r = 0.0f;
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
        fx->pre_gain = 1.0f;
        fx->post_gain = 1.0f;
        fx_saturation_update_bypass(fx);
        return;
    }

    const uint8_t drive_eff = (drive_0_127 == 0U)
                            ? 0U
                            : (uint8_t)(25U + (((uint32_t)(drive_0_127 - 1U) * 112U) / 126U));

    const float d = (float)drive_eff * (1.0f / 128.0f);

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

    fx_saturation_update_bypass(fx);
}

void fx_saturation_set_decimator_bits_ui(fx_saturation_t *fx, uint8_t bits_0_127)
{
    if (fx == 0)
    {
        return;
    }

    fx->decimator_bits_to_crush = (uint8_t)(((uint32_t)bits_0_127 * (uint32_t)FX_DECIMATOR_BITS_MAX) / 127U);
    fx->decimator_enabled = ((fx->decimator_bits_to_crush > 0U) || (fx->decimator_threshold > 0U)) ? 1U : 0U;
    fx_saturation_update_bypass(fx);
}

void fx_saturation_set_decimator_rate_ui(fx_saturation_t *fx, uint8_t rate_0_127)
{
    if (fx == 0)
    {
        return;
    }

    fx->decimator_rate = (float)rate_0_127 * (1.0f / 127.0f);
    fx->decimator_threshold = (uint8_t)((fx->decimator_rate * fx->decimator_rate) * 96.0f);
    fx->decimator_enabled = ((fx->decimator_bits_to_crush > 0U) || (fx->decimator_threshold > 0U)) ? 1U : 0U;
    fx_saturation_update_bypass(fx);
}

void fx_saturation_set_decimator_rate2_ui(fx_saturation_t *fx, uint8_t rate_0_127)
{
    if(fx == 0)
    {
        return;
    }

    const float norm = (float)rate_0_127 * (1.0f / 127.0f);
    fx->decimator_rate2_frequency = fx_clampf(norm * norm, FX_RATE2_FREQ_MIN, FX_RATE2_FREQ_MAX);
    fx->decimator_rate2_enabled = (rate_0_127 > 0U) ? 1U : 0U;
    fx_saturation_update_bypass(fx);
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
    uint8_t decimator_inc_l = fx->decimator_inc_l;
    uint8_t decimator_inc_r = fx->decimator_inc_r;
    float decimator_downsampled_l = fx->decimator_downsampled_l;
    float decimator_downsampled_r = fx->decimator_downsampled_r;
    const uint8_t decimator_threshold = fx->decimator_threshold;
    const uint8_t decimator_bits = fx->decimator_bits_to_crush;
    const uint8_t decimator_enabled = fx->decimator_enabled;
    const uint8_t decimator_rate2_enabled = fx->decimator_rate2_enabled;
    const float decimator_rate2_frequency = fx->decimator_rate2_frequency;
    float decimator_rate2_phase_l = fx->decimator_rate2_phase_l;
    float decimator_rate2_sample_l = fx->decimator_rate2_sample_l;
    float decimator_rate2_next_sample_l = fx->decimator_rate2_next_sample_l;
    float decimator_rate2_previous_sample_l = fx->decimator_rate2_previous_sample_l;
    float decimator_rate2_phase_r = fx->decimator_rate2_phase_r;
    float decimator_rate2_sample_r = fx->decimator_rate2_sample_r;
    float decimator_rate2_next_sample_r = fx->decimator_rate2_next_sample_r;
    float decimator_rate2_previous_sample_r = fx->decimator_rate2_previous_sample_r;

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

        if (decimator_enabled != 0U)
        {
            int32_t yl_i = 0;
            int32_t yr_i = 0;

            decimator_inc_l = (uint8_t)(decimator_inc_l + 1U);
            if (decimator_inc_l > decimator_threshold)
            {
                decimator_inc_l = 0U;
                decimator_downsampled_l = yl;
            }

            decimator_inc_r = (uint8_t)(decimator_inc_r + 1U);
            if (decimator_inc_r > decimator_threshold)
            {
                decimator_inc_r = 0U;
                decimator_downsampled_r = yr;
            }

            yl_i = (int32_t)(decimator_downsampled_l * 65536.0f);
            yr_i = (int32_t)(decimator_downsampled_r * 65536.0f);

            yl_i >>= decimator_bits;
            yl_i <<= decimator_bits;
            yr_i >>= decimator_bits;
            yr_i <<= decimator_bits;

            yl = (float)yl_i / 65536.0f;
            yr = (float)yr_i / 65536.0f;
        }

        if (decimator_rate2_enabled != 0U)
        {
            yl = fx_saturation_rate2_process_sample(yl,
                                                    decimator_rate2_frequency,
                                                    &decimator_rate2_phase_l,
                                                    &decimator_rate2_sample_l,
                                                    &decimator_rate2_next_sample_l,
                                                    &decimator_rate2_previous_sample_l);
            yr = fx_saturation_rate2_process_sample(yr,
                                                    decimator_rate2_frequency,
                                                    &decimator_rate2_phase_r,
                                                    &decimator_rate2_sample_r,
                                                    &decimator_rate2_next_sample_r,
                                                    &decimator_rate2_previous_sample_r);
        }

        l[n] = yl;
        r[n] = yr;
    }

    fx->prev_l = prev_l;
    fx->prev_r = prev_r;
    fx->decimator_inc_l = decimator_inc_l;
    fx->decimator_inc_r = decimator_inc_r;
    fx->decimator_downsampled_l = decimator_downsampled_l;
    fx->decimator_downsampled_r = decimator_downsampled_r;
    fx->decimator_rate2_phase_l = decimator_rate2_phase_l;
    fx->decimator_rate2_sample_l = decimator_rate2_sample_l;
    fx->decimator_rate2_next_sample_l = decimator_rate2_next_sample_l;
    fx->decimator_rate2_previous_sample_l = decimator_rate2_previous_sample_l;
    fx->decimator_rate2_phase_r = decimator_rate2_phase_r;
    fx->decimator_rate2_sample_r = decimator_rate2_sample_r;
    fx->decimator_rate2_next_sample_r = decimator_rate2_next_sample_r;
    fx->decimator_rate2_previous_sample_r = decimator_rate2_previous_sample_r;
}

void fx_saturation_process_mono_block(fx_saturation_t *fx,
                                      float *inout,
                                      uint32_t frames)
{
    if((fx == 0) || (inout == 0) || (frames == 0U) || (fx->bypass != 0U))
        return;

    const float k = fx->k;
    const float tone = fx->tone;
    const float asym = fx->asym;
    const float pre = fx->pre_gain;
    const float post = fx->post_gain;
    const float wet = fx->mix;
    const float dry = fx->dry;

    float prev = fx->prev_l;
    uint8_t decimator_inc = fx->decimator_inc_l;
    float decimator_downsampled = fx->decimator_downsampled_l;
    const uint8_t decimator_threshold = fx->decimator_threshold;
    const uint8_t decimator_bits = fx->decimator_bits_to_crush;
    const uint8_t decimator_enabled = fx->decimator_enabled;
    const uint8_t decimator_rate2_enabled = fx->decimator_rate2_enabled;
    const float decimator_rate2_frequency = fx->decimator_rate2_frequency;
    float decimator_rate2_phase = fx->decimator_rate2_phase_l;
    float decimator_rate2_sample = fx->decimator_rate2_sample_l;
    float decimator_rate2_next_sample = fx->decimator_rate2_next_sample_l;
    float decimator_rate2_previous_sample = fx->decimator_rate2_previous_sample_l;

    for(uint32_t n = 0U; n < frames; n++)
    {
        const float in = inout[n];
        float x = in;

        const float dx = x - prev;
        prev = x;
        x += tone * dx;
        x *= pre;
        if(x < 0.0f) x *= asym;

        const float x2 = x * x;
        const float ax = __builtin_fabsf(x);
        float y = x * (1.0f + k * ax) / (1.0f + k * x2);
        y *= post;
        y = in * dry + y * wet;
        y = fx_softclip(y);

        if (decimator_enabled != 0U)
        {
            int32_t y_i = 0;

            decimator_inc = (uint8_t)(decimator_inc + 1U);
            if (decimator_inc > decimator_threshold)
            {
                decimator_inc = 0U;
                decimator_downsampled = y;
            }

            y_i = (int32_t)(decimator_downsampled * 65536.0f);
            y_i >>= decimator_bits;
            y_i <<= decimator_bits;
            y = (float)y_i / 65536.0f;
        }

        if (decimator_rate2_enabled != 0U)
        {
            y = fx_saturation_rate2_process_sample(y,
                                                   decimator_rate2_frequency,
                                                   &decimator_rate2_phase,
                                                   &decimator_rate2_sample,
                                                   &decimator_rate2_next_sample,
                                                   &decimator_rate2_previous_sample);
        }

        inout[n] = y;
    }

    fx->prev_l = prev;
    fx->prev_r = prev;
    fx->decimator_inc_l = decimator_inc;
    fx->decimator_inc_r = decimator_inc;
    fx->decimator_downsampled_l = decimator_downsampled;
    fx->decimator_downsampled_r = decimator_downsampled;
    fx->decimator_rate2_phase_l = decimator_rate2_phase;
    fx->decimator_rate2_phase_r = decimator_rate2_phase;
    fx->decimator_rate2_sample_l = decimator_rate2_sample;
    fx->decimator_rate2_sample_r = decimator_rate2_sample;
    fx->decimator_rate2_next_sample_l = decimator_rate2_next_sample;
    fx->decimator_rate2_next_sample_r = decimator_rate2_next_sample;
    fx->decimator_rate2_previous_sample_l = decimator_rate2_previous_sample;
    fx->decimator_rate2_previous_sample_r = decimator_rate2_previous_sample;
}
