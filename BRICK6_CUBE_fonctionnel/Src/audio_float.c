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

    master_gain = 1.0f;
}

/** Voir audio_float.h */
void track_enable(uint32_t track_id, uint8_t enabled)
{
    if(track_id >= MAX_TRACKS)
        return;

    tracks[track_id].enabled = enabled ? 1U : 0U;
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

/* int24 signé right-aligned -> float [-1..1] */
static inline float s242f(int32_t x)
{
    if(x & 0x00800000)
        x |= 0xFF000000;

    return (float)x * (1.0f / 8388608.0f); /* 2^23 */
}

/* float [-1..1] -> int24 signé right-aligned */
static inline int32_t f2s24(float x)
{
    if(x > 0.999999f)
        x = 0.999999f;
    if(x < -1.0f)
        x = -1.0f;

    return ((int32_t)(x * 8388607.0f)) & 0x00FFFFFF;
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
   - Initialise send0/send1 (et retours) à zéro

   audio_io_pack():
   - Convertit bus MAIN/CUE float -> TDM int24 right-aligned
   - Écrit MAIN/CUE + slots inutilisés à 0
   ============================================================ */

static inline void audio_io_unpack(const int32_t *AUDIO_RESTRICT rx,
                                   StereoTrack *AUDIO_RESTRICT track_buf,
                                   uint32_t frames)
{
    const float in_gain = postgain_recip;

    for(uint32_t t = 0; t < MAX_TRACKS; t++)
    {
        float *AUDIO_RESTRICT tr_l = track_buf[t].L;
        float *AUDIO_RESTRICT tr_r = track_buf[t].R;

        if(track_buf[t].enabled)
        {
            const uint32_t slot_l = t * 2U;
            const uint32_t slot_r = slot_l + 1U;
            const int32_t *AUDIO_RESTRICT prx_l = rx + slot_l;
            const int32_t *AUDIO_RESTRICT prx_r = rx + slot_r;

            for(uint32_t n = 0; n < frames; n++)
            {
                tr_l[n] = s242f(*prx_l) * in_gain;
                tr_r[n] = s242f(*prx_r) * in_gain;
                prx_l += AUDIO_TDM_SLOTS;
                prx_r += AUDIO_TDM_SLOTS;
            }
        }
        else
        {
            /* Contrat callback: tracks inactives remises à zéro pour le bloc courant. */
            memset(tr_l, 0, frames * sizeof(float));
            memset(tr_r, 0, frames * sizeof(float));
        }
    }
}

static inline void audio_dsp_process(StereoTrack *AUDIO_RESTRICT track_buf,
                                     float *AUDIO_RESTRICT bus_main_l,
                                     float *AUDIO_RESTRICT bus_main_r,
                                     float *AUDIO_RESTRICT bus_cue_l,
                                     float *AUDIO_RESTRICT bus_cue_r,
                                     float *AUDIO_RESTRICT send0_l,
                                     float *AUDIO_RESTRICT send0_r,
                                     float *AUDIO_RESTRICT send1_l,
                                     float *AUDIO_RESTRICT send1_r,
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
        memset(send0_l, 0, frames * sizeof(float));
        memset(send0_r, 0, frames * sizeof(float));
        memset(send1_l, 0, frames * sizeof(float));
        memset(send1_r, 0, frames * sizeof(float));
        return;
    }

    /* Préparation sends/returns (FX non branchés pour l'instant). */
    memset(send0_l, 0, frames * sizeof(float));
    memset(send0_r, 0, frames * sizeof(float));
    memset(send1_l, 0, frames * sizeof(float));
    memset(send1_r, 0, frames * sizeof(float));

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

        ptx[0] = f2s24(main_l); /* MAIN L */
        ptx[1] = f2s24(main_r); /* MAIN R */
        ptx[2] = f2s24(cue_l);  /* CUE L  */
        ptx[3] = f2s24(cue_r);  /* CUE R  */
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
    static float send0_l[AUDIO_BLOCK_SIZE] __attribute__((aligned(32)));
    static float send0_r[AUDIO_BLOCK_SIZE] __attribute__((aligned(32)));
    static float send1_l[AUDIO_BLOCK_SIZE] __attribute__((aligned(32)));
    static float send1_r[AUDIO_BLOCK_SIZE] __attribute__((aligned(32)));

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    audio_io_unpack(rx, tracks, frames);
    audio_dsp_process(tracks,
                      bus_main_l,
                      bus_main_r,
                      bus_cue_l,
                      bus_cue_r,
                      send0_l,
                      send0_r,
                      send1_l,
                      send1_r,
                      frames);
    audio_io_pack(tx, bus_main_l, bus_main_r, bus_cue_l, bus_cue_r, frames);
}
