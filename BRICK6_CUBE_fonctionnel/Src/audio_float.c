/**
 * @file audio_float.c
 * @brief Moteur frontière int24 <-> float, architecture tracks stéréo actives.
 *
 * Rôle du module:
 * - Convertir le flux DMA TDM8 (int24 right-aligned) en buffers float par track.
 * - Exécuter le callback DSP utilisateur sur les tracks.
 * - Réaliser le mix vers buses internes (MAIN/CUE/SEND) et le remappage de sortie TDM.
 *
 * Architecture (appelant -> appelé):
 * - audio.c (IRQ DMA RX) -> audio_process_block_int32().
 * - audio_process_block_int32() -> float_cb(tracks, MAX_TRACKS, frames).
 *
 * Modèle audio track-based:
 * - Track 0 lit slots TDM 0/1 (L/R).
 * - Track 1 lit slots TDM 2/3 (L/R).
 * - Track 2 lit slots TDM 4/5 (L/R).
 * - Les slots 6/7 en entrée ne sont pas exploités.
 *
 * Mapping sortie TDM:
 * - MAIN L/R -> slots 0/1.
 * - CUE L/R (copie MAIN par défaut) -> slots 2/3.
 * - slots 4..7 forcés à 0.
 *
 * Contraintes temps réel:
 * - Fonction principale exécutée en IRQ audio.
 * - Aucune allocation dynamique, buffers statiques uniquement.
 * - Zéro logs / printf / appels bloquants.
 */

#include "audio_float.h"
#include <stdint.h>
#include <string.h>
#include <arm_acle.h>
#include "fx_dj_eq3_cmsis.h"
#include "stm32h743xx.h"
#include "arm_math.h"
#include "fx_saturation.h"

/* ============================================================
   CONFIG
   ============================================================ */

#define AUDIO_TDM_SLOTS 8U

/* ============================================================
   GAIN STAGING (style Daisy)

   postgain_recip : facteur appliqué à l'entrée ADC (1/postgain).
   output_adjust  : correction de sortie commune (postgain * output_comp).
   master_gain    : gain master appliqué après somme des tracks.
   track_gain[t]  : gain individuel de chaque track lors de la somme.
   ============================================================ */

static float postgain_recip = 1.0f;
static float output_adjust = 1.0f;

static float postgain = 1.0f;
static float output_comp = 1.0f;

static fx_dj_eq3_t track0_eq;
static fx_saturation_t saturation;

static volatile uint8_t track0_eq_ui_low = 64U;
static volatile uint8_t track0_eq_ui_mid = 64U;
static volatile uint8_t track0_eq_ui_high = 64U;

static inline uint8_t eq_is_neutral(void)
{
    return (track0_eq_ui_low == 64U) && (track0_eq_ui_mid == 64U) && (track0_eq_ui_high == 64U);
}

/** Voir audio_float.h */
void audio_float_set_postgain(float gain)
{
    if(gain <= 0.0f)
        gain = 1.0f;

    postgain = gain;
    postgain_recip = 1.0f / postgain;

    output_adjust = postgain * output_comp;
}

/** Voir audio_float.h */
void audio_float_set_output_compensation(float comp)
{
    output_comp = comp;
    output_adjust = postgain * output_comp;
}

void audio_float_set_dj_eq_low_db(float db)
{
    fx_dj_eq3_set_low_db(&track0_eq, db);
}

void audio_float_set_dj_eq_mid_db(float db)
{
    fx_dj_eq3_set_mid_db(&track0_eq, db);
}

void audio_float_set_dj_eq_high_db(float db)
{
    fx_dj_eq3_set_high_db(&track0_eq, db);
}

void audio_float_set_dj_eq_ui_params(uint8_t low, uint8_t mid, uint8_t high)
{
    track0_eq_ui_low = low;
    track0_eq_ui_mid = mid;
    track0_eq_ui_high = high;
}

uint8_t audio_float_is_dj_eq_ui_neutral(void)
{
    return eq_is_neutral() ? 1U : 0U;
}

void audio_float_set_saturation_tone_ui(uint8_t tone_0_127)
{
    fx_saturation_set_tone_ui(&saturation, tone_0_127);
}

void audio_float_set_saturation_bias_ui(uint8_t bias_0_127)
{
    fx_saturation_set_bias_ui(&saturation, bias_0_127);
}

void audio_float_set_saturation_drive_ui(uint8_t drive_0_127)
{
    fx_saturation_set_drive_ui(&saturation, drive_0_127);
}

void audio_float_set_saturation_mix_ui(uint8_t mix_0_127)
{
    fx_saturation_set_mix_ui(&saturation, mix_0_127);
}

/* ============================================================
   TRACK + MIX STATE
   ============================================================ */

/* État persistant des tracks (buffers bloc + enabled). */
static StereoTrack tracks[MAX_TRACKS] __attribute__((aligned(32)));

/* Gains track individuels (appliqués au moment de la somme). */
static float track_gain[MAX_TRACKS] __attribute__((aligned(32))) = {1.0f, 1.0f, 1.0f};

/* Gain master global (après somme des tracks). */
static float master_gain = 1.0f;

/* ============================================================
   USER CALLBACK
   ============================================================ */

static audio_dsp_cb float_cb = 0;

/** Voir audio_float.h */
void audio_set_float_callback(audio_dsp_cb cb)
{
    float_cb = cb;
}

/** Voir audio_float.h */
void audio_tracks_init(void)
{
    for(uint32_t t = 0; t < MAX_TRACKS; t++)
    {
        tracks[t].enabled = 0U;
        track_gain[t] = 1.0f;
        memset(tracks[t].L, 0, sizeof(tracks[t].L));
        memset(tracks[t].R, 0, sizeof(tracks[t].R));
    }

    fx_dj_eq3_init(&track0_eq, 48000.0f, 200.0f, 1000.0f, 1.0f, 6000.0f);

    fx_saturation_init(&saturation);

    master_gain = 1.0f;
}

/** Voir audio_float.h */
void track_enable(uint32_t track_id, uint8_t enabled)
{
    if(track_id >= MAX_TRACKS)
        return;

    const uint8_t prev = tracks[track_id].enabled;
    const uint8_t next = enabled ? 1U : 0U;

    tracks[track_id].enabled = next;

    if((prev == 0U) && (next != 0U) && (track_id == 0U))
    {
        fx_dj_eq3_reset(&track0_eq);
    }
}

/** Voir audio_float.h */
uint32_t track_is_enabled(uint32_t track_id)
{
    if(track_id >= MAX_TRACKS)
        return 0U;

    return (uint32_t)tracks[track_id].enabled;
}

/** Voir audio_float.h */
void track_set_gain(uint32_t track_id, float gain)
{
    if(track_id >= MAX_TRACKS)
        return;

    if(gain < 0.0f)
        gain = 0.0f;

    track_gain[track_id] = gain;
}

/** Voir audio_float.h */
void audio_float_set_master_gain(float gain)
{
    if(gain < 0.0f)
        gain = 0.0f;
    if(gain > 2.0f)
        gain = 2.0f;

    master_gain = gain;
}

/** Voir audio_float.h */
float audio_float_get_master_gain(void)
{
    return master_gain;
}

/* ============================================================
   CONVERSION HELPERS : CS42448 + STM32H7 SAI (TDM8, 24-bit)

   Format attendu côté DMA:
   - int24 signé right-aligned dans int32 (bits [23:0]).
   ============================================================ */

/* int24 signé right-aligned -> int32 signé (branchless). */
static inline int32_t s24_sign_extend(int32_t x)
{
    return (x << 8) >> 8;
}

/* int24 signé right-aligned -> float [-1..1). */
static inline float s242f_fast(int32_t x, float gain)
{
    return (float)s24_sign_extend(x) * gain;
}

/* float -> int24 signé right-aligned (branchless clamp + quantif). */
static inline int32_t f2s24_fast(float x)
{
    const float clamped = __builtin_fmaxf(-1.0f, __builtin_fminf(x, 0.9999998807907104f)); /* (2^23-1)/2^23 */
    const int32_t q = (int32_t)(clamped * 8388607.0f);
    return q & 0x00FFFFFF;
}

/* Variante optionnelle DSP/ACLE: saturation via SSAT sur 24 bits. */
static inline int32_t f2s24_fast_ssat(float x)
{
    const int32_t q = (int32_t)(x * 8388608.0f);
    const int32_t sat = __SSAT(q, 24);
    return sat & 0x00FFFFFF;
}

/* ============================================================
   INTERNAL AUDIO PIPELINE HELPERS

   audio_io_unpack():
   - Lit le TDM int24 right-aligned depuis rx
   - Convertit en float dans tracks[]
   - Gère uniquement la logique enabled/clear des tracks

   audio_dsp_process():
   - Appelle le callback DSP utilisateur
   - Réalise la somme tracks -> bus_main
   - Copie bus_main -> bus_cue (par défaut)

   audio_io_pack():
   - Convertit bus MAIN/CUE float -> TDM int24 right-aligned
   - Écrit MAIN/CUE + slots inutilisés à 0
   ============================================================ */

static inline void audio_io_unpack(const int32_t *AUDIO_RESTRICT rx,
                                   StereoTrack *AUDIO_RESTRICT track_buf,
                                   uint32_t frames)
{
    const float in_scale = postgain_recip * (1.0f / 8388608.0f);

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

    const int32_t *AUDIO_RESTRICT prx = rx;

    for(uint32_t n = 0; n < frames; n++)
    {
        /* Lecture contiguë de la frame TDM (slots 0..5 utilisés). */
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

static inline void audio_dsp_process(StereoTrack *AUDIO_RESTRICT track_buf,
                                     float *AUDIO_RESTRICT bus_main_l,
                                     float *AUDIO_RESTRICT bus_main_r,
                                     float *AUDIO_RESTRICT bus_cue_l,
                                     float *AUDIO_RESTRICT bus_cue_r,
                                     uint32_t frames)
{
    if(float_cb)
        float_cb(track_buf, MAX_TRACKS, frames);

    const float mg = master_gain;
    uint32_t active_ids[MAX_TRACKS];
    float active_gains[MAX_TRACKS];
    uint32_t active_count = 0U;

    for(uint32_t t = 0; t < MAX_TRACKS; t++)
    {
        if(track_buf[t].enabled)
        {
            active_ids[active_count] = t;
            active_gains[active_count] = track_gain[t];
            active_count++;
        }
    }

    if(active_count == 0U)
    {
        memset(bus_main_l, 0, frames * sizeof(float));
        memset(bus_main_r, 0, frames * sizeof(float));
        memset(bus_cue_l, 0, frames * sizeof(float));
        memset(bus_cue_r, 0, frames * sizeof(float));
        return;
    }

    /* EQ DJ 3 bandes uniquement sur track 0 (stéréo, en place), après float_cb(). */
    if(track_buf[0].enabled && !eq_is_neutral())
    {
        fx_dj_eq3_process_block(&track0_eq,
                                track_buf[0].L,
                                track_buf[0].R,
                                frames);
    }

    /* Saturation après EQ (avant mix bus). */
    if(track_buf[0].enabled)
    {
        fx_saturation_process_block(&saturation,
                                    track_buf[0].L,
                                    track_buf[0].R,
                                    frames);
    }

    for(uint32_t n = 0; n < frames; n++)
    {
        float sum_l = 0.0f;
        float sum_r = 0.0f;

        for(uint32_t i = 0; i < active_count; i++)
        {
            const uint32_t t = active_ids[i];
            const float g = active_gains[i];
            sum_l += track_buf[t].L[n] * g;
            sum_r += track_buf[t].R[n] * g;
        }

        const float main_l = sum_l * mg;
        const float main_r = sum_r * mg;

        bus_main_l[n] = main_l;
        bus_main_r[n] = main_r;
        bus_cue_l[n] = main_l; /* défaut: CUE = copie MAIN */
        bus_cue_r[n] = main_r;

    }

}

static inline void audio_io_pack(int32_t *AUDIO_RESTRICT tx,
                                 const float *AUDIO_RESTRICT bus_main_l,
                                 const float *AUDIO_RESTRICT bus_main_r,
                                 const float *AUDIO_RESTRICT bus_cue_l,
                                 const float *AUDIO_RESTRICT bus_cue_r,
                                 uint32_t frames)
{
    const float out_gain = output_adjust;
    int32_t *AUDIO_RESTRICT ptx = tx;

    for(uint32_t n = 0; n < frames; n++)
    {
        const float main_l = bus_main_l[n] * out_gain;
        const float main_r = bus_main_r[n] * out_gain;
        const float cue_l = bus_cue_l[n] * out_gain;
        const float cue_r = bus_cue_r[n] * out_gain;

#if defined(USE_F2S24_SSAT)
        ptx[0] = f2s24_fast_ssat(main_l); /* MAIN L */
        ptx[1] = f2s24_fast_ssat(main_r); /* MAIN R */
        ptx[2] = f2s24_fast_ssat(cue_l);  /* CUE L  */
        ptx[3] = f2s24_fast_ssat(cue_r);  /* CUE R  */
#else
        ptx[0] = f2s24_fast(main_l); /* MAIN L */
        ptx[1] = f2s24_fast(main_r); /* MAIN R */
        ptx[2] = f2s24_fast(cue_l);  /* CUE L  */
        ptx[3] = f2s24_fast(cue_r);  /* CUE R  */
#endif
        ptx[4] = 0;
        ptx[5] = 0;
        ptx[6] = 0;
        ptx[7] = 0;
        ptx += AUDIO_TDM_SLOTS;
    }
}

/* ============================================================
   MAIN DSP BLOCK PROCESSOR

   Pipeline temps réel (IRQ):
   1) audio_io_unpack()
   2) audio_dsp_process()
   3) audio_io_pack()
   ============================================================ */

/** Voir audio_float.h */
void audio_process_block_int32(int32_t *AUDIO_RESTRICT rx,
                               int32_t *AUDIO_RESTRICT tx,
                               uint32_t frames)
{
    static float bus_main_l[AUDIO_BLOCK_SIZE] __attribute__((aligned(32)));
    static float bus_main_r[AUDIO_BLOCK_SIZE] __attribute__((aligned(32)));
    static float bus_cue_l[AUDIO_BLOCK_SIZE] __attribute__((aligned(32)));
    static float bus_cue_r[AUDIO_BLOCK_SIZE] __attribute__((aligned(32)));
    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    audio_io_unpack(rx, tracks, frames);
    audio_dsp_process(tracks,
                      bus_main_l,
                      bus_main_r,
                      bus_cue_l,
                      bus_cue_r,
                      frames);
    audio_io_pack(tx, bus_main_l, bus_main_r, bus_cue_l, bus_cue_r, frames);
}
