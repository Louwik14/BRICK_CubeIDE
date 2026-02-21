/**
 * @file audio_float.c
 * @brief Moteur frontière int24 <-> float, architecture tracks stéréo actives.
 *
 * Rôle du module:
 * - Convertir le flux DMA TDM8 (int24 right-aligned) en buffers float par track.
 * - Exécuter le callback DSP utilisateur sur les tracks.
 * - Réaliser la somme/mix master et le remappage de sortie TDM.
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
 * - CUE L/R (copie master) -> slots 2/3.
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
static StereoTrack tracks[MAX_TRACKS];

/* Gains track individuels (appliqués au moment de la somme). */
static float track_gain[MAX_TRACKS] = {1.0f, 1.0f, 1.0f};

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
   MAIN DSP BLOCK PROCESSOR

   Pipeline temps réel (IRQ):
   1) Unpack des tracks actives depuis le bus TDM.
      - Les tracks inactives sont explicitement mises à zéro sur la portion
        du bloc utile (anti-stale, évite la réutilisation d'anciens samples).
   2) Appel du callback DSP utilisateur.
   3) Somme de toutes les tracks actives avec track_gain[] + master_gain.
   4) Pack vers TDM sortie (MAIN/CUE/0).
   ============================================================ */

/** Voir audio_float.h */
void audio_process_block_int32(int32_t *rx, int32_t *tx, uint32_t frames)
{
    static float master_l[AUDIO_BLOCK_SIZE];
    static float master_r[AUDIO_BLOCK_SIZE];

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    /* 1) UNPACK: ne lit que les tracks actives. */
    for(uint32_t t = 0; t < MAX_TRACKS; t++)
    {
        if(tracks[t].enabled)
        {
            const uint32_t slot_l = t * 2U;
            const uint32_t slot_r = slot_l + 1U;

            for(uint32_t n = 0; n < frames; n++)
            {
                const uint32_t base = n * AUDIO_TDM_SLOTS;
                tracks[t].L[n] = s242f(rx[base + slot_l]) * postgain_recip;
                tracks[t].R[n] = s242f(rx[base + slot_r]) * postgain_recip;
            }
        }
        else
        {
            /* Anti-stale audio: clear uniquement la portion du bloc courant. */
            memset(tracks[t].L, 0, frames * sizeof(float));
            memset(tracks[t].R, 0, frames * sizeof(float));
        }
    }

    /* 2) DSP callback utilisateur (doit ignorer tracks désactivées). */
    if(float_cb)
        float_cb(tracks, MAX_TRACKS, frames);

    /* 3) SOMME vers bus master stéréo. */
    for(uint32_t n = 0; n < frames; n++)
    {
        float sum_l = 0.0f;
        float sum_r = 0.0f;

        for(uint32_t t = 0; t < MAX_TRACKS; t++)
        {
            if(tracks[t].enabled)
            {
                sum_l += tracks[t].L[n] * track_gain[t];
                sum_r += tracks[t].R[n] * track_gain[t];
            }
        }

        master_l[n] = sum_l * master_gain;
        master_r[n] = sum_r * master_gain;
    }

    /* 4) PACK vers TDM sortie. */
    for(uint32_t n = 0; n < frames; n++)
    {
        const float out_l = master_l[n] * output_adjust;
        const float out_r = master_r[n] * output_adjust;
        const uint32_t base = n * AUDIO_TDM_SLOTS;

        tx[base + 0U] = f2s24(out_l); /* MAIN L */
        tx[base + 1U] = f2s24(out_r); /* MAIN R */
        tx[base + 2U] = f2s24(out_l); /* CUE L  */
        tx[base + 3U] = f2s24(out_r); /* CUE R  */
        tx[base + 4U] = 0;
        tx[base + 5U] = 0;
        tx[base + 6U] = 0;
        tx[base + 7U] = 0;
    }
}
