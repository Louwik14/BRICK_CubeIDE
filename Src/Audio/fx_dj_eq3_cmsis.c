/**
 * @file fx_dj_eq3_cmsis.c
 * @brief Module applicatif fx_dj_eq3_cmsis.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à fx_dj_eq3_cmsis.
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

#include "fx_dj_eq3_cmsis.h"

#include <math.h>
#include <string.h>

#define FX_DJ_EQ3_NUM_STAGES 3U
#define FX_DJ_EQ3_MIN_DB    (-80.0f)
#define FX_DJ_EQ3_MAX_DB    (12.0f)
#define FX_DJ_EQ3_SHELF_S   (1.0f)
#define FX_DJ_EQ3_MIN_FREQ  (10.0f)
#define FX_DJ_EQ3_PARAM_DB_EPS  (1.0e-5f)
#define FX_DJ_EQ3_SANITIZE_OUTPUT 0
#define FX_DJ_EQ3_LUT_INTERVALS 128U

/*
 * The DJ EQ uses fixed crossover frequencies in the mixer. Its complete RBJ
 * coefficient curves are therefore prepared once during audio initialization.
 * IRQ-side parameter changes only interpolate this table.
 */
static float g_fx_dj_eq3_coeff_lut[FX_DJ_EQ3_NUM_STAGES]
                                      [FX_DJ_EQ3_LUT_INTERVALS + 1U][5U];
static uint8_t g_fx_dj_eq3_coeff_lut_ready = 0U;

/**
 * @brief Point d'entrée fx_clamp.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_clamp.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param lo Paramètre d'entrée de l'API.
 * @param hi Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline float fx_clamp(float x, float lo, float hi)
{
    if(x < lo)
    {
        return lo;
    }
    if(x > hi)
    {
        return hi;
    }
    return x;
}

/**
 * @brief Point d'entrée fx_safe.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_safe.
 *
 * @param x Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline float fx_safe(float x)
{
    return isfinite(x) ? x : 0.0f;
}

/**
 * @brief Point d'entrée fx_clamp_db.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_clamp_db.
 *
 * @param db Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline float fx_clamp_db(float db)
{
    return fx_clamp(db, FX_DJ_EQ3_MIN_DB, FX_DJ_EQ3_MAX_DB);
}

/**
 * @brief Point d'entrée fx_clamp_freq.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_clamp_freq.
 *
 * @param f Paramètre d'entrée de l'API.
 * @param sample_rate Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline float fx_clamp_freq(float f, float sample_rate)
{
    const float nyquist_margin = sample_rate * 0.49f;
    return fx_clamp(f, FX_DJ_EQ3_MIN_FREQ, nyquist_margin);
}



#if (FX_DJ_EQ3_SANITIZE_OUTPUT != 0)
/**
 * @brief Point d'entrée fx_sanitize_sample.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_sanitize_sample.
 *
 * @param x Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline float fx_sanitize_sample(float x)
{
    if(!isfinite(x))
    {
        return 0.0f;
    }

    if(fabsf(x) < 1.0e-20f)
    {
        return 0.0f;
    }

    return x;
}
#endif

/**
 * @brief Point d'entrée rbj_low_shelf.
 *
 * Rôle:
 * - Exécuter le traitement associé à rbj_low_shelf.
 *
 * @param fs Paramètre d'entrée de l'API.
 * @param f0 Paramètre d'entrée de l'API.
 * @param gain_db Paramètre d'entrée de l'API.
 * @param s Paramètre d'entrée de l'API.
 * @param c Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void rbj_low_shelf(float fs, float f0, float gain_db, float s, float *c)
{
    const float a = powf(10.0f, gain_db * 0.025f);
    const float w0 = 2.0f * PI * f0 / fs;
    const float cos_w0 = cosf(w0);
    const float sin_w0 = sinf(w0);
    const float root_a = sqrtf(a);
    const float t = (a + (1.0f / a)) * ((1.0f / s) - 1.0f) + 2.0f;
    const float alpha = 0.5f * sin_w0 * sqrtf(fmaxf(t, 0.0f));

    const float ap1 = a + 1.0f;
    const float am1 = a - 1.0f;
    const float two_root_alpha = 2.0f * root_a * alpha;

    const float b0 = a * (ap1 - am1 * cos_w0 + two_root_alpha);
    const float b1 = 2.0f * a * (am1 - ap1 * cos_w0);
    const float b2 = a * (ap1 - am1 * cos_w0 - two_root_alpha);
    const float a0 = ap1 + am1 * cos_w0 + two_root_alpha;
    const float a1 = -2.0f * (am1 + ap1 * cos_w0);
    const float a2 = ap1 + am1 * cos_w0 - two_root_alpha;

    const float inv_a0 = (fabsf(a0) > 1.0e-12f) ? (1.0f / a0) : 0.0f;
    c[0] = fx_safe(b0 * inv_a0);
    c[1] = fx_safe(b1 * inv_a0);
    c[2] = fx_safe(b2 * inv_a0);
    c[3] = fx_safe((-a1) * inv_a0);
    c[4] = fx_safe((-a2) * inv_a0);
}

/**
 * @brief Point d'entrée rbj_peaking.
 *
 * Rôle:
 * - Exécuter le traitement associé à rbj_peaking.
 *
 * @param fs Paramètre d'entrée de l'API.
 * @param f0 Paramètre d'entrée de l'API.
 * @param q Paramètre d'entrée de l'API.
 * @param gain_db Paramètre d'entrée de l'API.
 * @param c Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void rbj_peaking(float fs, float f0, float q, float gain_db, float *c)
{
    const float a = powf(10.0f, gain_db * 0.025f);
    const float w0 = 2.0f * PI * f0 / fs;
    const float alpha = sinf(w0) / (2.0f * q);
    const float cos_w0 = cosf(w0);

    const float b0 = 1.0f + alpha * a;
    const float b1 = -2.0f * cos_w0;
    const float b2 = 1.0f - alpha * a;
    const float a0 = 1.0f + alpha / a;
    const float a1 = -2.0f * cos_w0;
    const float a2 = 1.0f - alpha / a;

    const float inv_a0 = (fabsf(a0) > 1.0e-12f) ? (1.0f / a0) : 0.0f;
    c[0] = fx_safe(b0 * inv_a0);
    c[1] = fx_safe(b1 * inv_a0);
    c[2] = fx_safe(b2 * inv_a0);
    c[3] = fx_safe((-a1) * inv_a0);
    c[4] = fx_safe((-a2) * inv_a0);
}

/**
 * @brief Point d'entrée rbj_high_shelf.
 *
 * Rôle:
 * - Exécuter le traitement associé à rbj_high_shelf.
 *
 * @param fs Paramètre d'entrée de l'API.
 * @param f0 Paramètre d'entrée de l'API.
 * @param gain_db Paramètre d'entrée de l'API.
 * @param s Paramètre d'entrée de l'API.
 * @param c Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void rbj_high_shelf(float fs, float f0, float gain_db, float s, float *c)
{
    const float a = powf(10.0f, gain_db * 0.025f);
    const float w0 = 2.0f * PI * f0 / fs;
    const float cos_w0 = cosf(w0);
    const float sin_w0 = sinf(w0);
    const float root_a = sqrtf(a);
    const float t = (a + (1.0f / a)) * ((1.0f / s) - 1.0f) + 2.0f;
    const float alpha = 0.5f * sin_w0 * sqrtf(fmaxf(t, 0.0f));

    const float ap1 = a + 1.0f;
    const float am1 = a - 1.0f;
    const float two_root_alpha = 2.0f * root_a * alpha;

    const float b0 = a * (ap1 + am1 * cos_w0 + two_root_alpha);
    const float b1 = -2.0f * a * (am1 + ap1 * cos_w0);
    const float b2 = a * (ap1 + am1 * cos_w0 - two_root_alpha);
    const float a0 = ap1 - am1 * cos_w0 + two_root_alpha;
    const float a1 = 2.0f * (am1 - ap1 * cos_w0);
    const float a2 = ap1 - am1 * cos_w0 - two_root_alpha;

    const float inv_a0 = (fabsf(a0) > 1.0e-12f) ? (1.0f / a0) : 0.0f;
    c[0] = fx_safe(b0 * inv_a0);
    c[1] = fx_safe(b1 * inv_a0);
    c[2] = fx_safe(b2 * inv_a0);
    c[3] = fx_safe((-a1) * inv_a0);
    c[4] = fx_safe((-a2) * inv_a0);
}

static void fx_dj_eq3_prepare_coeff_lut(float sample_rate)
{
    if(g_fx_dj_eq3_coeff_lut_ready != 0U)
    {
        return;
    }

    const float fs = (sample_rate > 1000.0f) ? sample_rate : 48000.0f;
    for(uint32_t i = 0U; i <= FX_DJ_EQ3_LUT_INTERVALS; ++i)
    {
        const float t = (float)i * (1.0f / (float)FX_DJ_EQ3_LUT_INTERVALS);
        const float db = FX_DJ_EQ3_MIN_DB
                       + ((FX_DJ_EQ3_MAX_DB - FX_DJ_EQ3_MIN_DB) * t);
        rbj_low_shelf(fs, 300.0f, db, FX_DJ_EQ3_SHELF_S,
                      g_fx_dj_eq3_coeff_lut[0U][i]);
        rbj_peaking(fs, 1000.0f, 0.8f, db,
                    g_fx_dj_eq3_coeff_lut[1U][i]);
        rbj_high_shelf(fs, 4000.0f, db, FX_DJ_EQ3_SHELF_S,
                       g_fx_dj_eq3_coeff_lut[2U][i]);
    }
    g_fx_dj_eq3_coeff_lut_ready = 1U;
}

static void fx_dj_eq3_lookup_coeffs(uint32_t stage, float gain_db, float *out)
{
    const float db = fx_clamp_db(gain_db);
    const float pos = (db - FX_DJ_EQ3_MIN_DB)
                    * ((float)FX_DJ_EQ3_LUT_INTERVALS
                       / (FX_DJ_EQ3_MAX_DB - FX_DJ_EQ3_MIN_DB));
    uint32_t index = (uint32_t)pos;
    if(index >= FX_DJ_EQ3_LUT_INTERVALS)
    {
        memcpy(out, g_fx_dj_eq3_coeff_lut[stage][FX_DJ_EQ3_LUT_INTERVALS],
               5U * sizeof(float));
        return;
    }

    const float frac = pos - (float)index;
    for(uint32_t i = 0U; i < 5U; ++i)
    {
        const float a = g_fx_dj_eq3_coeff_lut[stage][index][i];
        out[i] = a + ((g_fx_dj_eq3_coeff_lut[stage][index + 1U][i] - a) * frac);
    }
}


/**
 * @brief Point d'entrée fx_dj_eq3_reset.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_dj_eq3_reset.
 *
 * @param eq Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_dj_eq3_reset(fx_dj_eq3_t *eq)
{
    if(eq == NULL)
    {
        return;
    }

    memset(eq->state_l, 0, sizeof(eq->state_l));
    memset(eq->state_r, 0, sizeof(eq->state_r));
}

/**
 * @brief Point d'entrée fx_dj_eq3_update_coeffs.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_dj_eq3_update_coeffs.
 *
 * @param eq Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_dj_eq3_update_coeffs(fx_dj_eq3_t *eq)
{
    if(eq == NULL)
    {
        return;
    }

    float coeffs_tmp[3U * 5U];

    eq->low_db = fx_clamp_db(eq->low_db);
    eq->mid_db = fx_clamp_db(eq->mid_db);
    eq->high_db = fx_clamp_db(eq->high_db);
    eq->sample_rate = (eq->sample_rate > 1000.0f) ? eq->sample_rate : 48000.0f;

    fx_dj_eq3_lookup_coeffs(0U, eq->low_db, &coeffs_tmp[0]);
    fx_dj_eq3_lookup_coeffs(1U, eq->mid_db, &coeffs_tmp[5]);
    fx_dj_eq3_lookup_coeffs(2U, eq->high_db, &coeffs_tmp[10]);

    eq->coeffs_pending_update = 0U;
    __DMB();
    memcpy(eq->coeffs_pending, coeffs_tmp, sizeof(coeffs_tmp));
    __DMB();
    eq->coeffs_pending_update = 1U;
}

/**
 * @brief Point d'entrée fx_dj_eq3_set_low_db.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_dj_eq3_set_low_db.
 *
 * @param eq Paramètre d'entrée de l'API.
 * @param gain_db Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_dj_eq3_set_low_db(fx_dj_eq3_t *eq, float gain_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float clamped = fx_clamp_db(gain_db);
    if(fabsf(clamped - eq->low_db) >= FX_DJ_EQ3_PARAM_DB_EPS)
    {
        eq->low_db = clamped;
        fx_dj_eq3_update_coeffs(eq);
    }
}

/**
 * @brief Point d'entrée fx_dj_eq3_set_mid_db.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_dj_eq3_set_mid_db.
 *
 * @param eq Paramètre d'entrée de l'API.
 * @param gain_db Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_dj_eq3_set_mid_db(fx_dj_eq3_t *eq, float gain_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float clamped = fx_clamp_db(gain_db);
    if(fabsf(clamped - eq->mid_db) >= FX_DJ_EQ3_PARAM_DB_EPS)
    {
        eq->mid_db = clamped;
        fx_dj_eq3_update_coeffs(eq);
    }
}

/**
 * @brief Point d'entrée fx_dj_eq3_set_high_db.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_dj_eq3_set_high_db.
 *
 * @param eq Paramètre d'entrée de l'API.
 * @param gain_db Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_dj_eq3_set_high_db(fx_dj_eq3_t *eq, float gain_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float clamped = fx_clamp_db(gain_db);
    if(fabsf(clamped - eq->high_db) >= FX_DJ_EQ3_PARAM_DB_EPS)
    {
        eq->high_db = clamped;
        fx_dj_eq3_update_coeffs(eq);
    }
}

void fx_dj_eq3_set_gains_db(fx_dj_eq3_t *eq,
                            float low_db,
                            float mid_db,
                            float high_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float low = fx_clamp_db(low_db);
    const float mid = fx_clamp_db(mid_db);
    const float high = fx_clamp_db(high_db);
    if((fabsf(low - eq->low_db) < FX_DJ_EQ3_PARAM_DB_EPS)
            && (fabsf(mid - eq->mid_db) < FX_DJ_EQ3_PARAM_DB_EPS)
            && (fabsf(high - eq->high_db) < FX_DJ_EQ3_PARAM_DB_EPS))
    {
        return;
    }

    eq->low_db = low;
    eq->mid_db = mid;
    eq->high_db = high;
    fx_dj_eq3_update_coeffs(eq);
}

/**
 * @brief Point d'entrée fx_dj_eq3_set_bypass.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_dj_eq3_set_bypass.
 *
 * @param eq Paramètre d'entrée de l'API.
 * @param bypass Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_dj_eq3_set_bypass(fx_dj_eq3_t *eq, uint8_t bypass)
{
    if(eq == NULL)
    {
        return;
    }

    eq->bypass = (bypass != 0U) ? 1U : 0U;
}

/**
 * @brief Point d'entrée fx_dj_eq3_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_dj_eq3_init.
 *
 * @param eq Paramètre d'entrée de l'API.
 * @param sample_rate Paramètre d'entrée de l'API.
 * @param low_freq Paramètre d'entrée de l'API.
 * @param mid_freq Paramètre d'entrée de l'API.
 * @param mid_q Paramètre d'entrée de l'API.
 * @param high_freq Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_dj_eq3_init(fx_dj_eq3_t *eq,
                    float sample_rate,
                    float low_freq,
                    float mid_freq,
                    float mid_q,
                    float high_freq)
{
    if(eq == NULL)
    {
        return;
    }

    memset(eq, 0, sizeof(*eq));

    eq->sample_rate = sample_rate;
    eq->low_freq = 300.0f;
    eq->mid_freq = 1000.0f;
    eq->high_freq = 4000.0f;
    eq->mid_q = 0.8f;

    fx_dj_eq3_prepare_coeff_lut(eq->sample_rate);

    arm_biquad_cascade_df1_init_f32(&eq->inst_l, FX_DJ_EQ3_NUM_STAGES, eq->coeffs, eq->state_l);
    arm_biquad_cascade_df1_init_f32(&eq->inst_r, FX_DJ_EQ3_NUM_STAGES, eq->coeffs, eq->state_r);

    fx_dj_eq3_update_coeffs(eq);
    if(eq->coeffs_pending_update != 0U)
    {
        memcpy(eq->coeffs, eq->coeffs_pending, sizeof(eq->coeffs));
        eq->coeffs_pending_update = 0U;
    }
    fx_dj_eq3_reset(eq);
}

/**
 * @brief Point d'entrée fx_dj_eq3_process_block.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_dj_eq3_process_block.
 *
 * @param eq Paramètre d'entrée de l'API.
 * @param inout_l Paramètre d'entrée de l'API.
 * @param inout_r Paramètre d'entrée de l'API.
 * @param block_size Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_dj_eq3_process_block(fx_dj_eq3_t *eq,
                             float *inout_l,
                             float *inout_r,
                             uint32_t block_size)
{
	if((eq == NULL) || (inout_l == NULL) || (inout_r == NULL) || (block_size == 0U))
	{
	    return;
	}

	if(eq->bypass != 0U)
	{
	    return;
	}

    if(eq->coeffs_pending_update != 0U)
    {
        memcpy(eq->coeffs, eq->coeffs_pending, sizeof(eq->coeffs));
        __DMB();
        eq->coeffs_pending_update = 0U;
    }

    arm_biquad_cascade_df1_f32(&eq->inst_l, inout_l, inout_l, block_size);
    arm_biquad_cascade_df1_f32(&eq->inst_r, inout_r, inout_r, block_size);


#if (FX_DJ_EQ3_SANITIZE_OUTPUT != 0)
    for(uint32_t n = 0U; n < block_size; n++)
    {
        inout_l[n] = fx_sanitize_sample(inout_l[n]);
        inout_r[n] = fx_sanitize_sample(inout_r[n]);
    }
#endif
}

void fx_dj_eq3_mono_reset(fx_dj_eq3_mono_t *eq)
{
    if(eq == NULL)
    {
        return;
    }

    memset(eq->state, 0, sizeof(eq->state));
}

void fx_dj_eq3_mono_update_coeffs(fx_dj_eq3_mono_t *eq)
{
    if(eq == NULL)
    {
        return;
    }

    float coeffs_tmp[3U * 5U];

    eq->low_db = fx_clamp_db(eq->low_db);
    eq->mid_db = fx_clamp_db(eq->mid_db);
    eq->high_db = fx_clamp_db(eq->high_db);
    eq->sample_rate = (eq->sample_rate > 1000.0f) ? eq->sample_rate : 48000.0f;

    fx_dj_eq3_lookup_coeffs(0U, eq->low_db, &coeffs_tmp[0]);
    fx_dj_eq3_lookup_coeffs(1U, eq->mid_db, &coeffs_tmp[5]);
    fx_dj_eq3_lookup_coeffs(2U, eq->high_db, &coeffs_tmp[10]);

    eq->coeffs_pending_update = 0U;
    __DMB();
    memcpy(eq->coeffs_pending, coeffs_tmp, sizeof(coeffs_tmp));
    __DMB();
    eq->coeffs_pending_update = 1U;
}

void fx_dj_eq3_mono_set_low_db(fx_dj_eq3_mono_t *eq, float gain_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float clamped = fx_clamp_db(gain_db);
    if(fabsf(clamped - eq->low_db) >= FX_DJ_EQ3_PARAM_DB_EPS)
    {
        eq->low_db = clamped;
        fx_dj_eq3_mono_update_coeffs(eq);
    }
}

void fx_dj_eq3_mono_set_mid_db(fx_dj_eq3_mono_t *eq, float gain_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float clamped = fx_clamp_db(gain_db);
    if(fabsf(clamped - eq->mid_db) >= FX_DJ_EQ3_PARAM_DB_EPS)
    {
        eq->mid_db = clamped;
        fx_dj_eq3_mono_update_coeffs(eq);
    }
}

void fx_dj_eq3_mono_set_high_db(fx_dj_eq3_mono_t *eq, float gain_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float clamped = fx_clamp_db(gain_db);
    if(fabsf(clamped - eq->high_db) >= FX_DJ_EQ3_PARAM_DB_EPS)
    {
        eq->high_db = clamped;
        fx_dj_eq3_mono_update_coeffs(eq);
    }
}

void fx_dj_eq3_mono_set_gains_db(fx_dj_eq3_mono_t *eq,
                                 float low_db,
                                 float mid_db,
                                 float high_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float low = fx_clamp_db(low_db);
    const float mid = fx_clamp_db(mid_db);
    const float high = fx_clamp_db(high_db);
    if((fabsf(low - eq->low_db) < FX_DJ_EQ3_PARAM_DB_EPS)
            && (fabsf(mid - eq->mid_db) < FX_DJ_EQ3_PARAM_DB_EPS)
            && (fabsf(high - eq->high_db) < FX_DJ_EQ3_PARAM_DB_EPS))
    {
        return;
    }

    eq->low_db = low;
    eq->mid_db = mid;
    eq->high_db = high;
    fx_dj_eq3_mono_update_coeffs(eq);
}

void fx_dj_eq3_mono_set_bypass(fx_dj_eq3_mono_t *eq, uint8_t bypass)
{
    if(eq == NULL)
    {
        return;
    }

    eq->bypass = (bypass != 0U) ? 1U : 0U;
}

void fx_dj_eq3_mono_init(fx_dj_eq3_mono_t *eq,
                         float sample_rate,
                         float low_freq,
                         float mid_freq,
                         float mid_q,
                         float high_freq)
{
    (void)low_freq;
    (void)mid_freq;
    (void)mid_q;
    (void)high_freq;
    if(eq == NULL)
    {
        return;
    }

    memset(eq, 0, sizeof(*eq));

    eq->sample_rate = sample_rate;
    eq->low_freq = 300.0f;
    eq->mid_freq = 1000.0f;
    eq->high_freq = 4000.0f;
    eq->mid_q = 0.8f;

    fx_dj_eq3_prepare_coeff_lut(eq->sample_rate);
    arm_biquad_cascade_df1_init_f32(&eq->inst, FX_DJ_EQ3_NUM_STAGES, eq->coeffs, eq->state);

    fx_dj_eq3_mono_update_coeffs(eq);
    if(eq->coeffs_pending_update != 0U)
    {
        memcpy(eq->coeffs, eq->coeffs_pending, sizeof(eq->coeffs));
        eq->coeffs_pending_update = 0U;
    }
    fx_dj_eq3_mono_reset(eq);
}

void fx_dj_eq3_mono_process_block(fx_dj_eq3_mono_t *eq,
                                  float *inout,
                                  uint32_t block_size)
{
    if((eq == NULL) || (inout == NULL) || (block_size == 0U))
    {
        return;
    }

    if(eq->bypass != 0U)
    {
        return;
    }

    if(eq->coeffs_pending_update != 0U)
    {
        memcpy(eq->coeffs, eq->coeffs_pending, sizeof(eq->coeffs));
        __DMB();
        eq->coeffs_pending_update = 0U;
    }

    arm_biquad_cascade_df1_f32(&eq->inst, inout, inout, block_size);
}
