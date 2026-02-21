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
 * - main loop -> audio_dsp_main_process() -> float_cb(...).
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

/* État persistant des tracks (plan de contrôle: enabled uniquement). */
static StereoTrack tracks[MAX_TRACKS];

/* Ping-pong entrée DSP (IRQ écrit, main lit). */
static StereoTrack in_buf[2][MAX_TRACKS];

/* Ping-pong sortie DSP master (main écrit, IRQ lit). */
static float outL[2][AUDIO_BLOCK_SIZE];
static float outR[2][AUDIO_BLOCK_SIZE];

static volatile uint8_t in_write_idx = 0U;
static volatile uint8_t in_read_idx = 1U;
static volatile uint8_t in_ready = 0U;

static volatile uint8_t out_write_idx = 0U;
static volatile uint8_t out_read_idx = 1U;
static volatile uint8_t out_ready = 0U;

/* Compteurs debug IRQ<->main (pertes / silences). */
static volatile uint32_t in_overrun_count = 0U;
static volatile uint32_t out_underflow_count = 0U;

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

        in_buf[0][t].enabled = 0U;
        in_buf[1][t].enabled = 0U;
        memset(in_buf[0][t].L, 0, sizeof(in_buf[0][t].L));
        memset(in_buf[0][t].R, 0, sizeof(in_buf[0][t].R));
        memset(in_buf[1][t].L, 0, sizeof(in_buf[1][t].L));
        memset(in_buf[1][t].R, 0, sizeof(in_buf[1][t].R));
    }

    memset(outL[0], 0, sizeof(outL[0]));
    memset(outL[1], 0, sizeof(outL[1]));
    memset(outR[0], 0, sizeof(outR[0]));
    memset(outR[1], 0, sizeof(outR[1]));

    in_write_idx = 0U;
    in_read_idx = 1U;
    in_ready = 0U;

    out_write_idx = 0U;
    out_read_idx = 1U;
    out_ready = 0U;

    in_overrun_count = 0U;
    out_underflow_count = 0U;

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

uint32_t audio_float_get_in_overrun_count(void)
{
    uint32_t value;

    __disable_irq();
    value = in_overrun_count;
    __enable_irq();

    return value;
}

uint32_t audio_float_get_out_underflow_count(void)
{
    uint32_t value;

    __disable_irq();
    value = out_underflow_count;
    __enable_irq();

    return value;
}

void audio_float_reset_xrun_counters(void)
{
    __disable_irq();
    in_overrun_count = 0U;
    out_underflow_count = 0U;
    __enable_irq();
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
   - Réalise la somme tracks -> master avec track_gain/master_gain

   audio_io_pack():
   - Convertit master float -> TDM int24 right-aligned
   - Écrit MAIN/CUE + slots inutilisés à 0
   ============================================================ */

static void audio_io_unpack(int32_t *rx, StereoTrack *track_buf, uint32_t frames)
{
    for(uint32_t t = 0; t < MAX_TRACKS; t++)
    {
        if(track_buf[t].enabled)
        {
            const uint32_t slot_l = t * 2U;
            const uint32_t slot_r = slot_l + 1U;

            for(uint32_t n = 0; n < frames; n++)
            {
                const uint32_t base = n * AUDIO_TDM_SLOTS;
                track_buf[t].L[n] = s242f(rx[base + slot_l]) * postgain_recip;
                track_buf[t].R[n] = s242f(rx[base + slot_r]) * postgain_recip;
            }
        }
        else
        {
            /* Anti-stale audio: clear uniquement la portion du bloc courant. */
            memset(track_buf[t].L, 0, frames * sizeof(float));
            memset(track_buf[t].R, 0, frames * sizeof(float));
        }
    }
}

static void audio_dsp_process(StereoTrack *track_buf,
                              float *master_l,
                              float *master_r,
                              uint32_t frames)
{
    if(float_cb)
        float_cb(track_buf, MAX_TRACKS, frames);

    for(uint32_t n = 0; n < frames; n++)
    {
        float sum_l = 0.0f;
        float sum_r = 0.0f;

        for(uint32_t t = 0; t < MAX_TRACKS; t++)
        {
            if(track_buf[t].enabled)
            {
                sum_l += track_buf[t].L[n] * track_gain[t];
                sum_r += track_buf[t].R[n] * track_gain[t];
            }
        }

        master_l[n] = sum_l * master_gain;
        master_r[n] = sum_r * master_gain;
    }
}

static void audio_io_pack(int32_t *tx,
                          const float *master_l,
                          const float *master_r,
                          uint32_t frames)
{
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

/* ============================================================
   MAIN DSP BLOCK PROCESSOR

   Pipeline IRQ:
   1) audio_io_unpack()
   2) publication vers buffer DSP partagé
   3) audio_io_pack() depuis la dernière sortie DSP disponible
   ============================================================ */

/** Voir audio_float.h */
void audio_process_block_int32(int32_t *rx, int32_t *tx, uint32_t frames)
{
    static float silent_l[AUDIO_BLOCK_SIZE] = {0};
    static float silent_r[AUDIO_BLOCK_SIZE] = {0};

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    uint8_t write_idx;
    uint8_t can_publish;
    uint8_t pack_idx;
    uint8_t has_output;

    __disable_irq();
    write_idx = in_write_idx;
    can_publish = (in_ready == 0U) ? 1U : 0U;

    pack_idx = out_read_idx;
    has_output = out_ready;
    if(has_output)
        out_ready = 0U;
    __enable_irq();

    if(can_publish)
    {
        StereoTrack *write_buf = in_buf[write_idx];

        for(uint32_t t = 0; t < MAX_TRACKS; t++)
            write_buf[t].enabled = tracks[t].enabled;

        audio_io_unpack(rx, write_buf, frames);

        __disable_irq();
        in_read_idx = write_idx;
        in_write_idx = (uint8_t)(write_idx ^ 1U);
        in_ready = 1U;
        __enable_irq();
    }
    else
    {
        in_overrun_count++;
    }

    if(has_output)
    {
        audio_io_pack(tx, outL[pack_idx], outR[pack_idx], frames);
    }
    else
    {
        out_underflow_count++;
        audio_io_pack(tx, silent_l, silent_r, frames);
    }
}

void audio_dsp_main_process(uint32_t frames)
{
    static float local_master_l[AUDIO_BLOCK_SIZE];
    static float local_master_r[AUDIO_BLOCK_SIZE];

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    uint8_t read_idx;
    uint8_t write_idx;

    __disable_irq();
    if(!in_ready)
    {
        __enable_irq();
        return;
    }

    read_idx = in_read_idx;
    write_idx = out_write_idx;
    in_ready = 0U;
    __enable_irq();

    audio_dsp_process(in_buf[read_idx], local_master_l, local_master_r, frames);

    memcpy(outL[write_idx], local_master_l, frames * sizeof(float));
    memcpy(outR[write_idx], local_master_r, frames * sizeof(float));

    __disable_irq();
    out_read_idx = write_idx;
    out_write_idx = (uint8_t)(write_idx ^ 1U);
    out_ready = 1U;
    __enable_irq();
}
