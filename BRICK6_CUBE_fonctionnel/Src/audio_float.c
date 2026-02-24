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
#include "fx_granular.h"
#include "memory_layout.h"
#include "audio_io.h"
#include "dsp_engine.h"
#include "fx_pool.h"
#include "fx_chain.h"
#include "param_store.h"

/* ============================================================
   GAIN STAGING (style Daisy)

   postgain_recip : facteur appliqué à l'entrée ADC (1/postgain).
   output_adjust  : correction de sortie commune (postgain * output_comp).
   master_gain    : gain master appliqué après somme des tracks.
   track_gain[t]  : gain individuel de chaque track lors de la somme.
   ============================================================ */

static AUDIO_HOT float postgain_recip = 1.0f;
static AUDIO_HOT float output_adjust = 1.0f;

static float postgain = 1.0f;
static float output_comp = 1.0f;

static volatile uint8_t track0_eq_ui_low = 64U;
static volatile uint8_t track0_eq_ui_mid = 64U;
static volatile uint8_t track0_eq_ui_high = 64U;

static inline uint8_t eq_is_neutral(void)
{
    return (track0_eq_ui_low == 64U) && (track0_eq_ui_mid == 64U) && (track0_eq_ui_high == 64U);
}


static inline fx_dj_eq3_t *fx_pool_eq_state(void)
{
    fx_slot_t *s = fx_pool_get_slot(0U);
    return (s != 0) ? (fx_dj_eq3_t *)s->state : 0;
}

static inline fx_saturation_t *fx_pool_sat_state(void)
{
    fx_slot_t *s = fx_pool_get_slot(1U);
    return (s != 0) ? (fx_saturation_t *)s->state : 0;
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
    fx_dj_eq3_t *eq = fx_pool_eq_state();
    if(eq) fx_dj_eq3_set_low_db(eq, db);
}

void audio_float_set_dj_eq_mid_db(float db)
{
    fx_dj_eq3_t *eq = fx_pool_eq_state();
    if(eq) fx_dj_eq3_set_mid_db(eq, db);
}

void audio_float_set_dj_eq_high_db(float db)
{
    fx_dj_eq3_t *eq = fx_pool_eq_state();
    if(eq) fx_dj_eq3_set_high_db(eq, db);
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
    fx_saturation_t *sat = fx_pool_sat_state();
    if(sat) fx_saturation_set_tone_ui(sat, tone_0_127);
}

void audio_float_set_saturation_bias_ui(uint8_t bias_0_127)
{
    fx_saturation_t *sat = fx_pool_sat_state();
    if(sat) fx_saturation_set_bias_ui(sat, bias_0_127);
}

void audio_float_set_saturation_drive_ui(uint8_t drive_0_127)
{
    fx_saturation_t *sat = fx_pool_sat_state();
    if(sat) fx_saturation_set_drive_ui(sat, drive_0_127);
}

void audio_float_set_saturation_mix_ui(uint8_t mix_0_127)
{
    fx_saturation_t *sat = fx_pool_sat_state();
    if(sat) fx_saturation_set_mix_ui(sat, mix_0_127);
}

/* ============================================================
   TRACK + MIX STATE
   ============================================================ */

/* État persistant des tracks (buffers bloc + enabled). */
static StereoTrack tracks[MAX_TRACKS] __attribute__((aligned(32)));

/* Gains track individuels (appliqués au moment de la somme). */
static AUDIO_HOT float track_gain[MAX_TRACKS] = {1.0f, 1.0f, 1.0f};

volatile uint32_t g_audio_block_counter = 0U;

/* Gain master global (après somme des tracks). */
static AUDIO_HOT float master_gain = 1.0f;

/* ============================================================
   USER CALLBACK
   ============================================================ */

/** Voir audio_float.h */
void audio_set_float_callback(audio_dsp_cb cb)
{
    dsp_engine_set_callback(cb);
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

    fx_dj_eq3_t *eq = fx_pool_eq_state();
    fx_saturation_t *sat = fx_pool_sat_state();

    if(eq) fx_dj_eq3_init(eq, 48000.0f, 200.0f, 1000.0f, 1.0f, 6000.0f);

    if(sat) fx_saturation_init(sat);
    fx_granular_init(48000.0f);

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
        fx_dj_eq3_t *eq = fx_pool_eq_state();
        if(eq) fx_dj_eq3_reset(eq);
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
   INTERNAL AUDIO PIPELINE HELPERS

   audio_dsp_process():
   - Appelle le callback DSP utilisateur
   - Réalise la somme tracks -> bus_main
   - Copie bus_main -> bus_cue (par défaut)
   ============================================================ */

static inline void audio_dsp_process(StereoTrack *AUDIO_RESTRICT track_buf,
                                     float *AUDIO_RESTRICT bus_main_l,
                                     float *AUDIO_RESTRICT bus_main_r,
                                     float *AUDIO_RESTRICT bus_cue_l,
                                     float *AUDIO_RESTRICT bus_cue_r,
                                     uint32_t frames)
{
    dsp_engine_process_block(track_buf, MAX_TRACKS, frames);

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
    if(track_buf[0].enabled)
    {
        float density = param_store_get_active(PARAM_GRAN_DENSITY);
        float pitch   = param_store_get_active(PARAM_GRAN_PITCH);
        float mix     = param_store_get_active(PARAM_GRAN_MIX);
        float freeze  = param_store_get_active(PARAM_GRAN_FREEZE);
        float spread  = param_store_get_active(PARAM_GRAN_SPREAD);
        float stereo  = param_store_get_active(PARAM_GRAN_STEREO);

        fx_granular_set_density(density);
        fx_granular_set_pitch(pitch);
        fx_granular_set_mix(mix);
        fx_granular_set_freeze(freeze > 0.5f);
        fx_granular_set_spread(spread);
        fx_granular_set_stereo_offset(stereo);

        fx_slot_t *eq_slot = fx_pool_get_slot(0U);
        if(eq_slot)
            eq_slot->active = eq_is_neutral() ? 0U : 1U;

        fx_chain_process_track0(track_buf[0].L,
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
    g_audio_block_counter++;
    static AUDIO_HOT float bus_main_l[AUDIO_BLOCK_SIZE];
    static AUDIO_HOT float bus_main_r[AUDIO_BLOCK_SIZE];
    static AUDIO_HOT float bus_cue_l[AUDIO_BLOCK_SIZE];
    static AUDIO_HOT float bus_cue_r[AUDIO_BLOCK_SIZE];
    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    audio_io_unpack(rx, tracks, frames, postgain_recip * (1.0f / 8388608.0f));
    audio_dsp_process(tracks,
                      bus_main_l,
                      bus_main_r,
                      bus_cue_l,
                      bus_cue_r,
                      frames);
    audio_io_pack(tx, bus_main_l, bus_main_r, bus_cue_l, bus_cue_r, frames, output_adjust);
}
