/**
 * @file audio_io.c
 * @brief Conversion rapide TDM int24 <-> buffers float tracks stéréo.
 *
 * Rôle du module:
 * - Dépaqueter les slots TDM RX en buffers float par track active.
 * - Repaqueter les bus MAIN/CUE float vers TX TDM int24.
 *
 * Architecture:
 * - Appelé par: audio_float.c.
 * - Appelle: aucun module applicatif.
 *
 * Contraintes temps réel:
 * - IRQ: oui (appelé dans audio_process_block_int32).
 * - Hard realtime: oui.
 * - malloc: interdit.
 *
 * Notes:
 * - Mapping entrée: tracks 0..2 sur slots (0/1, 2/3, 4/5).
 * - Track 3 est une source interne et n'est pas alimentée par l'entrée TDM.
 * - Mapping sortie: MAIN->0/1, CUE->2/3, slots 4..7 à 0.
 */

#include "audio_io.h"

#include <string.h>
#include <arm_acle.h>
#include "stm32h743xx.h"

#define AUDIO_TDM_SLOTS 8U

/**
 * @brief Étend le signe d'un int24 stocké dans un int32.
 *
 * @param x Mot 24-bit right-aligned.
 * @return Valeur signée étendue en 32 bits.
 */
/**
 * @brief Point d'entrée s24_sign_extend.
 *
 * Rôle:
 * - Exécuter le traitement associé à s24_sign_extend.
 *
 * @param x Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline int32_t s24_sign_extend(int32_t x)
{
    return (x << 8) >> 8;
}

/**
 * @brief Convertit un échantillon int24 en float avec gain.
 *
 * @param x Échantillon int24 packed.
 * @param gain Facteur de mise à l'échelle.
 * @return Échantillon float.
 */
/**
 * @brief Point d'entrée s242f_fast.
 *
 * Rôle:
 * - Exécuter le traitement associé à s242f_fast.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param gain Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline float s242f_fast(int32_t x, float gain)
{
    return (float)s24_sign_extend(x) * gain;
}

/**
 * @brief Convertit un float en int24 saturé logiciel.
 *
 * @param x Échantillon float.
 * @return Échantillon int24 packed.
 */
/**
 * @brief Point d'entrée f2s24_fast.
 *
 * Rôle:
 * - Exécuter le traitement associé à f2s24_fast.
 *
 * @param x Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline int32_t f2s24_fast(float x)
{
    const float clamped = __builtin_fmaxf(-1.0f, __builtin_fminf(x, 0.9999998807907104f));
    const int32_t q = (int32_t)(clamped * 8388607.0f);
    return q & 0x00FFFFFF;
}

/**
 * @brief Convertit un float en int24 avec saturation matérielle (__SSAT).
 *
 * @param x Échantillon float.
 * @return Échantillon int24 packed.
 */
/**
 * @brief Point d'entrée f2s24_fast_ssat.
 *
 * Rôle:
 * - Exécuter le traitement associé à f2s24_fast_ssat.
 *
 * @param x Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline int32_t f2s24_fast_ssat(float x)
{
    const int32_t q = (int32_t)(x * 8388608.0f);
    const int32_t sat = __SSAT(q, 24);
    return sat & 0x00FFFFFF;
}

/**
 * @brief Dépaquette un bloc RX TDM en tracks float actives.
 *
 * @param rx Buffer RX TDM int32.
 * @param track_buf Tableau des tracks float.
 * @param frames Nombre de frames à traiter.
 * @param in_scale Facteur d'échelle d'entrée.
 *
 * Contexte d'appel:
 * - IRQ audio.
 */
/**
 * @brief Point d'entrée audio_io_unpack.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_io_unpack.
 *
 * @param rx Paramètre d'entrée de l'API.
 * @param track_buf Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 * @param in_scale Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_io_unpack(const int32_t *AUDIO_RESTRICT rx,
                     StereoTrack *AUDIO_RESTRICT track_buf,
                     uint32_t frames,
                     float in_scale)
{
    const uint32_t tr0_on = (uint32_t)track_buf[0].enabled;
    const uint32_t tr1_on = (uint32_t)track_buf[1].enabled;
    const uint32_t tr2_on = (uint32_t)track_buf[2].enabled;

    float *AUDIO_RESTRICT tr0_l = track_buf[0].L;
    float *AUDIO_RESTRICT tr0_r = track_buf[0].R;
    float *AUDIO_RESTRICT tr1_l = track_buf[1].L;
    float *AUDIO_RESTRICT tr1_r = track_buf[1].R;
    float *AUDIO_RESTRICT tr2_l = track_buf[2].L;
    float *AUDIO_RESTRICT tr2_r = track_buf[2].R;
    if(tr0_on == 0U)
    {
        memset(tr0_l, 0, frames * sizeof(float));
        memset(tr0_r, 0, frames * sizeof(float));
    }
    if(tr1_on == 0U)
    {
        memset(tr1_l, 0, frames * sizeof(float));
        memset(tr1_r, 0, frames * sizeof(float));
    }
    if(tr2_on == 0U)
    {
        memset(tr2_l, 0, frames * sizeof(float));
        memset(tr2_r, 0, frames * sizeof(float));
    }

    if((tr0_on | tr1_on | tr2_on) == 0U)
    {
        return;
    }

    const int32_t *AUDIO_RESTRICT prx = rx;

    for(uint32_t n = 0; n < frames; n++)
    {
        const int32_t s0 = prx[0];
        const int32_t s1 = prx[1];
        const int32_t s2 = prx[2];
        const int32_t s3 = prx[3];
        const int32_t s4 = prx[4];
        const int32_t s5 = prx[5];

        if(tr0_on)
        {
            tr0_l[n] = s242f_fast(s0, in_scale);
            tr0_r[n] = s242f_fast(s1, in_scale);
        }
        if(tr1_on)
        {
            tr1_l[n] = s242f_fast(s2, in_scale);
            tr1_r[n] = s242f_fast(s3, in_scale);
        }
        if(tr2_on)
        {
            tr2_l[n] = s242f_fast(s4, in_scale);
            tr2_r[n] = s242f_fast(s5, in_scale);
        }

        prx += AUDIO_TDM_SLOTS;
    }
}

/**
 * @brief Repaquette les bus float MAIN/CUE vers le buffer TX TDM.
 *
 * @param tx Buffer TX TDM int32.
 * @param bus_main_l Bus MAIN gauche.
 * @param bus_main_r Bus MAIN droit.
 * @param bus_cue_l Bus CUE gauche.
 * @param bus_cue_r Bus CUE droit.
 * @param frames Nombre de frames.
 * @param out_gain Gain sortie global.
 *
 * Contexte d'appel:
 * - IRQ audio.
 */
/**
 * @brief Point d'entrée audio_io_pack.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_io_pack.
 *
 * @param tx Paramètre d'entrée de l'API.
 * @param bus_main_l Paramètre d'entrée de l'API.
 * @param bus_main_r Paramètre d'entrée de l'API.
 * @param bus_cue_l Paramètre d'entrée de l'API.
 * @param bus_cue_r Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 * @param out_gain Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_io_pack(int32_t *AUDIO_RESTRICT tx,
                   const float *AUDIO_RESTRICT bus_main_l,
                   const float *AUDIO_RESTRICT bus_main_r,
                   const float *AUDIO_RESTRICT bus_cue_l,
                   const float *AUDIO_RESTRICT bus_cue_r,
                   uint32_t frames,
                   float out_gain)
{
    if(frames == 0U)
    {
        return;
    }

    if(out_gain == 0.0f)
    {
        memset(tx, 0, frames * AUDIO_TDM_SLOTS * sizeof(int32_t));
        return;
    }

    int32_t *AUDIO_RESTRICT ptx = tx;

    for(uint32_t n = 0; n < frames; n++)
    {
        const float main_l = bus_main_l[n] * out_gain;
        const float main_r = bus_main_r[n] * out_gain;
        const float cue_l = bus_cue_l[n] * out_gain;
        const float cue_r = bus_cue_r[n] * out_gain;

#if defined(USE_F2S24_SSAT)
        ptx[0] = f2s24_fast_ssat(main_l);
        ptx[1] = f2s24_fast_ssat(main_r);
        ptx[2] = f2s24_fast_ssat(cue_l);
        ptx[3] = f2s24_fast_ssat(cue_r);
#else
        ptx[0] = f2s24_fast(main_l);
        ptx[1] = f2s24_fast(main_r);
        ptx[2] = f2s24_fast(cue_l);
        ptx[3] = f2s24_fast(cue_r);
#endif
        ptx[4] = 0;
        ptx[5] = 0;
        ptx[6] = 0;
        ptx[7] = 0;
        ptx += AUDIO_TDM_SLOTS;
    }
}
